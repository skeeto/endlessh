#define _POSIX_C_SOURCE 200112L
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define ENDLESSH_VERSION           0.1

#define DEFAULT_PORT              2222
#define DEFAULT_DELAY            10000  /* milliseconds */
#define DEFAULT_MAX_LINE_LENGTH     32
#define DEFAULT_MAX_CLIENTS       4096
#define DEFAULT_CONFIG_FILE      "/etc/endlessh/config"

#define XSTR(s) STR(s)
#define STR(s) #s

static long long
uepoch(void)
{
    struct timespec tv;
    clock_gettime(CLOCK_REALTIME, &tv);
    return tv.tv_sec * 1000ULL + tv.tv_nsec / 1000000ULL;
}

static enum loglevel {
    LOG_NONE,
    LOG_INFO,
    LOG_DEBUG
} loglevel = LOG_NONE;

static void
logmsg(enum loglevel level, const char *format, ...)
{
    if (loglevel >= level) {
        int save = errno;

        /* Print a timestamp */
        long long now = uepoch();
        time_t t = now / 1000;
        char date[64];
        struct tm tm[1];
        strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", gmtime_r(&t, tm));
        printf("%s.%03lldZ ", date, now % 1000);

        /* Print the rest of the log message */
        va_list ap;
        va_start(ap, format);
        vprintf(format, ap);
        va_end(ap);
        fputc('\n', stdout);

        errno = save;
    }
}

struct client {
    char ipaddr[INET6_ADDRSTRLEN];
    long long connect_time;
    long long send_next;
    long long bytes_sent;
    struct client *next;
    int port;
    int fd;
};

static struct client *
client_new(int fd, long long send_next)
{
    struct client *c = malloc(sizeof(*c));
    if (c) {
        c->ipaddr[0] = 0;
        c->connect_time = uepoch();
        c->send_next = send_next;
        c->bytes_sent = 0;
        c->next = 0;
        c->fd = fd;

        /* Set the smallest possible recieve buffer. This reduces local
         * resource usage and slows down the remote end.
         */
        int value = 1;
        int r = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value));
        logmsg(LOG_DEBUG, "setsockopt(%d, SO_RCVBUF, %d) = %d", fd, value, r);
        if (r == -1)
            logmsg(LOG_DEBUG, "errno = %d, %s", errno, strerror(errno));

        /* Get IP address */
        struct sockaddr_storage addr;
        socklen_t len = sizeof(addr);
        if (getpeername(fd, (struct sockaddr *)&addr, &len) != -1) {
            if (addr.ss_family == AF_INET) {
                struct sockaddr_in *s = (struct sockaddr_in *)&addr;
                c->port = ntohs(s->sin_port);
                inet_ntop(AF_INET, &s->sin_addr,
                          c->ipaddr, sizeof(c->ipaddr));
            } else {
                struct sockaddr_in6 *s = (struct sockaddr_in6 *)&addr;
                c->port = ntohs(s->sin6_port);
                inet_ntop(AF_INET6, &s->sin6_addr,
                          c->ipaddr, sizeof(c->ipaddr));
            }
        }
    }
    return c;
}

static void
client_destroy(struct client *client)
{
    logmsg(LOG_DEBUG, "close(%d)", client->fd);
    long long dt = uepoch() - client->connect_time;
    logmsg(LOG_INFO,
            "CLOSE host=%s port=%d fd=%d "
            "time=%lld.%03lld bytes=%lld",
            client->ipaddr, client->port, client->fd,
            dt / 1000, dt % 1000,
            client->bytes_sent);
    close(client->fd);
    free(client);
}

struct fifo {
    struct client *head;
    struct client *tail;
    int length;
};

static void
fifo_init(struct fifo *q)
{
    q->head = q->tail = 0;
    q->length = 0;
}

static struct client *
fifo_pop(struct fifo *q)
{
    struct client *removed = q->head;
    q->head = q->head->next;
    removed->next = 0;
    if (!--q->length)
        q->tail = 0;
    return removed;
}

static void
fifo_append(struct fifo *q, struct client *c)
{
    if (!q->tail) {
        q->head = q->tail = c;
    } else {
        q->tail->next = c;
        q->tail = c;
    }
    q->length++;
}

static void
fifo_destroy(struct fifo *q)
{
    struct client *c = q->head;
    while (c) {
        struct client *dead = c;
        c = c->next;
        client_destroy(dead);
    }
    q->head = q->tail = 0;
    q->length = 0;
}

static void
die(void)
{
    fprintf(stderr, "endlessh: fatal: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
}

static unsigned
rand16(unsigned long s[1])
{
    s[0] = s[0] * 1103515245UL + 12345UL;
    return (s[0] >> 16) & 0xffff;
}

static int
randline(char *line, int maxlen, unsigned long s[1])
{
    int len = 3 + rand16(s) % (maxlen - 2);
    for (int i = 0; i < len - 2; i++)
        line[i] = 32 + rand16(s) % 95;
    line[len - 2] = 13;
    line[len - 1] = 10;
    if (memcmp(line, "SSH-", 4) == 0)
        line[0] = 'X';
    return len;
}

static volatile sig_atomic_t running = 1;

static void
sigterm_handler(int signal)
{
    (void)signal;
    running = 0;
}

static volatile sig_atomic_t reload = 0;

static void
sighup_handler(int signal)
{
    (void)signal;
    reload = 1;
}

struct config {
    int port;
    int delay;
    int max_line_length;
    int max_clients;
};

#define CONFIG_DEFAULT { \
    .port            = DEFAULT_PORT, \
    .delay           = DEFAULT_DELAY, \
    .max_line_length = DEFAULT_MAX_LINE_LENGTH, \
    .max_clients     = DEFAULT_MAX_CLIENTS, \
}

static void
config_set_port(struct config *c, const char *s, int hardfail)
{
    errno = 0;
    char *end;
    long tmp = strtol(s, &end, 10);
    if (errno || *end || tmp < 1 || tmp > 65535) {
        fprintf(stderr, "endlessh: Invalid port: %s\n", s);
        if (hardfail)
            exit(EXIT_FAILURE);
    } else {
        c->port = tmp;
    }
}

static void
config_set_delay(struct config *c, const char *s, int hardfail)
{
    errno = 0;
    char *end;
    long tmp = strtol(s, &end, 10);
    if (errno || *end || tmp < 1 || tmp > INT_MAX) {
        fprintf(stderr, "endlessh: Invalid delay: %s\n", s);
        if (hardfail)
            exit(EXIT_FAILURE);
    } else {
        c->delay = tmp;
    }
}

static void
config_set_max_clients(struct config *c, const char *s, int hardfail)
{
    errno = 0;
    char *end;
    long tmp = strtol(s, &end, 10);
    if (errno || *end || tmp < 1 || tmp > INT_MAX) {
        fprintf(stderr, "endlessh: Invalid max clients: %s\n", s);
        if (hardfail)
            exit(EXIT_FAILURE);
    } else {
        c->max_clients = tmp;
    }
}

static void
config_set_max_line_length(struct config *c, const char *s, int hardfail)
{
    errno = 0;
    char *end;
    long tmp = strtol(s, &end, 10);
    if (errno || *end || tmp < 3 || tmp > 255) {
        fprintf(stderr, "endlessh: Invalid line length: %s\n", s);
        if (hardfail)
            exit(EXIT_FAILURE);
    } else {
        c->max_line_length = tmp;
    }
}

enum config_key {
    KEY_INVALID,
    KEY_PORT,
    KEY_DELAY,
    KEY_MAX_LINE_LENGTH,
    KEY_MAX_CLIENTS,
    KEY_LOG_LEVEL,
};

static enum config_key
config_key_parse(const char *tok)
{
    static const char *const table[] = {
        [KEY_PORT]            = "Port",
        [KEY_DELAY]           = "Delay",
        [KEY_MAX_LINE_LENGTH] = "MaxLineLength",
        [KEY_MAX_CLIENTS]     = "MaxClients",
        [KEY_LOG_LEVEL]       = "LogLevel",
    };
    for (size_t i = 1; i < sizeof(table) / sizeof(*table); i++)
        if (!strcmp(tok, table[i]))
            return i;
    return KEY_INVALID;
}

static void
config_load(struct config *c, const char *file, int hardfail)
{
    long lineno = 0;
    FILE *f = fopen(file, "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            lineno++;

            /* Remove comments */
            char *comment = strchr(line, '#');
            if (comment)
                *comment = 0;

            /* Parse tokes on line */
            char *save = 0;
            char *tokens[3];
            int ntokens = 0;
            for (; ntokens < 3; ntokens++) {
                char *tok = strtok_r(ntokens ? 0 : line, " \r\n", &save);
                if (!tok)
                    break;
                tokens[ntokens] = tok;
            }

            switch (ntokens) {
                case 0: /* Empty line */
                    continue;
                case 1:
                    fprintf(stderr, "%s:%ld: Missing value\n", file, lineno);
                    if (hardfail) exit(EXIT_FAILURE);
                    continue;
                case 2: /* Expected */
                    break;
                case 3:
                    fprintf(stderr, "%s:%ld: Too many values\n", file, lineno);
                    if (hardfail) exit(EXIT_FAILURE);
                    continue;
            }

            enum config_key key = config_key_parse(tokens[0]);
            switch (key) {
                case KEY_INVALID:
                    fprintf(stderr, "%s:%ld: Unknown option '%s'\n",
                            file, lineno, tokens[0]);
                    break;
                case KEY_PORT:
                    config_set_port(c, tokens[1], hardfail);
                    break;
                case KEY_DELAY:
                    config_set_delay(c, tokens[1], hardfail);
                    break;
                case KEY_MAX_LINE_LENGTH:
                    config_set_max_line_length(c, tokens[1], hardfail);
                    break;
                case KEY_MAX_CLIENTS:
                    config_set_max_clients(c, tokens[1], hardfail);
                    break;
                case KEY_LOG_LEVEL: {
                    errno = 0;
                    char *end;
                    long v = strtol(tokens[1], &end, 10);
                    if (errno || *end || v < LOG_NONE || v > LOG_DEBUG) {
                        fprintf(stderr, "%s:%ld: Invalid log level '%s'\n",
                                file, lineno, tokens[1]);
                        if (hardfail) exit(EXIT_FAILURE);
                    } else {
                        loglevel = v;
                    }
                } break;
            }
        }

        fclose(f);
    }
}

static void
config_log(const struct config *c)
{
    logmsg(LOG_INFO, "Port %d", c->port);
    logmsg(LOG_INFO, "Delay %ld", c->delay);
    logmsg(LOG_INFO, "MaxLineLength %d", c->max_line_length);
    logmsg(LOG_INFO, "MaxClients %d", c->max_clients);
}

static void
usage(FILE *f)
{
    fprintf(f, "Usage: endlessh [-vh] [-d MS] [-f CONFIG] [-l LEN] "
                               "[-m LIMIT] [-p PORT]\n");
    fprintf(f, "  -d INT    Message millisecond delay ["
            XSTR(DEFAULT_DELAY) "]\n");
    fprintf(f, "  -f        Set and load config file ["
            DEFAULT_CONFIG_FILE "]\n");
    fprintf(f, "  -h        Print this help message and exit\n");
    fprintf(f, "  -l INT    Maximum banner line length (3-255) ["
            XSTR(DEFAULT_MAX_LINE_LENGTH) "]\n");
    fprintf(f, "  -m INT    Maximum number of clients ["
            XSTR(DEFAULT_MAX_CLIENTS) "]\n");
    fprintf(f, "  -p INT    Listening port [" XSTR(DEFAULT_PORT) "]\n");
    fprintf(f, "  -v        Print diagnostics to standard output "
            "(repeatable)\n");
    fprintf(f, "  -V        Print version information and exit\n");
}

static void
print_version(void)
{
    puts("Endlessh " XSTR(ENDLESSH_VERSION));
}

static int
server_create(int port)
{
    int r, s, value;

    s = socket(AF_INET6, SOCK_STREAM, 0);
    logmsg(LOG_DEBUG, "socket() = %d", s);
    if (s == -1) die();

    /* Socket options are best effort, allowed to fail */
    value = 1;
    r = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));
    logmsg(LOG_DEBUG, "setsockopt(%d, SO_REUSEADDR, true) = %d", s, r);
    if (r == -1)
        logmsg(LOG_DEBUG, "errno = %d, %s", errno, strerror(errno));

    struct sockaddr_in6 addr = {
        .sin6_family = AF_INET6,
        .sin6_port = htons(port),
        .sin6_addr = in6addr_any
    };
    r = bind(s, (void *)&addr, sizeof(addr));
    logmsg(LOG_DEBUG, "bind(%d, port=%d) = %d", s, port, r);
    if (r == -1) die();

    r = listen(s, INT_MAX);
    logmsg(LOG_DEBUG, "listen(%d) = %d", s, r);
    if (r == -1) die();

    return s;
}

/* Write a line to a client, returning client if it's still up. */
static struct client *
sendline(struct client *client, int max_line_length, unsigned long *rng)
{
    char line[256];
    int len = randline(line, max_line_length, rng);
    for (;;) {
        ssize_t out = write(client->fd, line, len);
        logmsg(LOG_DEBUG, "write(%d) = %d", client->fd, (int)out);
        if (out == -1) {
            if (errno == EINTR) {
                continue;      /* try again */
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return client; /* don't care */
            } else {
                client_destroy(client);
                return 0;
            }
        } else {
            client->bytes_sent += out;
            return client;
        }
    }
}

int
main(int argc, char **argv)
{
    struct config config = CONFIG_DEFAULT;
    const char *config_file = DEFAULT_CONFIG_FILE;
    config_load(&config, config_file, 1);

    int option;
    while ((option = getopt(argc, argv, "d:f:hl:m:p:vV")) != -1) {
        switch (option) {
            case 'd':
                config_set_delay(&config, optarg, 1);
                break;
            case 'f':
                config_file = optarg;
                config_load(&config, optarg, 1);
                break;
            case 'h':
                usage(stdout);
                exit(EXIT_SUCCESS);
                break;
            case 'l':
                config_set_max_line_length(&config, optarg, 1);
                break;
            case 'm':
                config_set_max_clients(&config, optarg, 1);
                break;
            case 'p':
                config_set_port(&config, optarg, 1);
                break;
            case 'v':
                if (!loglevel++)
                    setvbuf(stdout, 0, _IOLBF, 0);
                break;
            case 'V':
                print_version();
                exit(EXIT_SUCCESS);
                break;
            default:
                usage(stderr);
                exit(EXIT_FAILURE);
        }
    }

    if (argv[optind]) {
        fprintf(stderr, "endlessh: too many arguments\n");
        exit(EXIT_FAILURE);
    }

    /* Log configuration */
    config_log(&config);

    /* Install the signal handlers */
    signal(SIGPIPE, SIG_IGN);
    {
        struct sigaction sa = {.sa_handler = sigterm_handler};
        int r = sigaction(SIGTERM, &sa, 0);
        if (r == -1)
            die();
    }
    {
        struct sigaction sa = {.sa_handler = sighup_handler};
        int r = sigaction(SIGHUP, &sa, 0);
        if (r == -1)
            die();
    }

    struct fifo fifo[1];
    fifo_init(fifo);

    unsigned long rng = uepoch();

    int server = server_create(config.port);

    while (running) {
        if (reload) {
            /* Configuration reload requested (SIGHUP) */
            int oldport = config.port;
            config_load(&config, config_file, 0);
            config_log(&config);
            if (oldport != config.port) {
                close(server);
                server = server_create(config.port);
            }
            reload = 0;
        }

        /* Enqueue clients that are due for another message */
        int timeout = -1;
        long long now = uepoch();
        while (fifo->head) {
            if (fifo->head->send_next <= now) {
                struct client *c = fifo_pop(fifo);
                if (sendline(c, config.max_line_length, &rng)) {
                    c->send_next = now + config.delay;
                    fifo_append(fifo, c);
                }
            } else {
                timeout = fifo->head->send_next - now;
                break;
            }
        }

        /* Wait for next event */
        struct pollfd fds = {server, POLLIN, 0};
        int nfds = fifo->length < config.max_clients;
        logmsg(LOG_DEBUG, "poll(%d, %d)", nfds, timeout);
        int r = poll(&fds, nfds, timeout);
        logmsg(LOG_DEBUG, "= %d", r);
        if (r == -1) {
            switch (errno) {
                case EINTR:
                    logmsg(LOG_DEBUG, "EINTR");
                    continue;
                default:
                    fprintf(stderr, "endlessh: fatal: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
            }
        }

        /* Check for new incoming connections */
        if (fds.revents & POLLIN) {
            int fd = accept(server, 0, 0);
            logmsg(LOG_DEBUG, "accept() = %d", fd);
            if (fd == -1) {
                const char *msg = strerror(errno);
                switch (errno) {
                    case EMFILE:
                    case ENFILE:
                        config.max_clients = fifo->length;
                        logmsg(LOG_INFO,
                                "MaxClients %d",
                                fifo->length);
                        break;
                    case ECONNABORTED:
                    case EINTR:
                    case ENOBUFS:
                    case ENOMEM:
                    case EPROTO:
                        fprintf(stderr, "endlessh: warning: %s\n", msg);
                        break;
                    default:
                        fprintf(stderr, "endlessh: fatal: %s\n", msg);
                        exit(EXIT_FAILURE);
                }
            } else {
                long long send_next = uepoch() + config.delay;
                struct client *client = client_new(fd, send_next);
                int flags = fcntl(fd, F_GETFL, 0);      /* cannot fail */
                fcntl(fd, F_SETFL, flags | O_NONBLOCK); /* cannot fail */
                if (!client) {
                    fprintf(stderr, "endlessh: warning: out of memory\n");
                    close(fd);
                }
                fifo_append(fifo, client);
                logmsg(LOG_INFO, "ACCEPT host=%s port=%d fd=%d n=%d/%d",
                        client->ipaddr, client->port, client->fd,
                        fifo->length, config.max_clients);
            }
        }
    }

    fifo_destroy(fifo);
}
