#define _POSIX_C_SOURCE  200112L
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <poll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define DEFAULT_MAX_CLIENTS   4096
#define DEFAULT_LINE_LENGTH     32
#define DEFAULT_PORT          2222
#define DEFAULT_DELAY        10000  // milliseconds

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
        long long now = uepoch();
        printf("%lld.%03lld: ", now / 1000, now % 1000);
        va_list ap;
        va_start(ap, format);
        vprintf(format, ap);
        va_end(ap);
        fputc('\n', stdout);
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
        c->connect_time = uepoch();
        c->send_next = send_next;
        c->bytes_sent = 0;
        c->next = 0;
        c->fd = fd;

        /* Get IP address */
        struct sockaddr_storage addr;
        socklen_t len = sizeof(addr);
        if (getpeername(fd, (struct sockaddr*)&addr, &len) == -1) {
            c->ipaddr[0] = 0;
        } else {
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
            "CLOSE host=%s:%d fd=%d "
            "time=%lld.%03lld bytes=%lld",
            client->ipaddr, client->port, client->fd,
            dt / 1000, dt % 1000,
            client->bytes_sent);
    close(client->fd);
    free(client);
}

struct queue {
    struct client *head;
    struct client *tail;
};

static void
queue_init(struct queue *q)
{
    q->head = q->tail = 0;
}

static struct client *
queue_remove(struct queue *q, int fd)
{
    struct client *c;
    struct client **prev = &q->head;
    for (c = q->head; c; prev = &c->next, c = c->next) {
        if (c->fd == fd) {
            if (q->tail == c)
                q->tail = 0;
            *prev = c->next;
            c->next = 0;
            break;
        }
    }
    return c;
}

static void
queue_append(struct queue *q, struct client *c)
{
    if (!q->tail) {
        q->head = q->tail = c;
    } else {
        q->tail->next = c;
        q->tail = c;
    }
}

static void
queue_destroy(struct queue *q)
{
    struct client *c = q->head;
    while (c) {
        struct client *dead = c;
        c = c->next;
        client_destroy(dead);
    }
    q->head = q->tail = 0;
}

struct pollvec {
    size_t cap;
    size_t fill;
    struct pollfd *fds;
};

static void
pollvec_init(struct pollvec *v)
{
    v->cap = 4;
    v->fill = 0;
    v->fds = malloc(v->cap * sizeof(v->fds[0]));
    if (!v->fds) {
        fprintf(stderr, "endlessh: pollvec initialization failed\n");
        exit(EXIT_FAILURE);
    }
}

static void
pollvec_clear(struct pollvec *v)
{
    v->fill = 0;
}

static int
pollvec_push(struct pollvec *v, int fd, short events)
{
    if (v->cap == v->fill) {
        size_t size = v->cap * 2 * sizeof(v->fds[0]);
        if (size < v->cap * sizeof(v->fds[0]))
            return 0; // overflow
        struct pollfd *grow = realloc(v->fds, size);
        if (!grow)
            return 0;
        v->fds = grow;
        v->cap *= 2;
    }
    v->fds[v->fill].fd = fd;
    v->fds[v->fill].events = events;
    v->fill++;
    return 1;
}

static void
pollvec_free(struct pollvec *v)
{
    free(v->fds);
    v->fds = 0;
}

static void
check(int r)
{
    if (r == -1) {
        fprintf(stderr, "endlessh: fatal: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

static int
randline(char *line, int maxlen)
{
    int len = 3 + rand() % (maxlen - 2);
    for (int i = 0; i < len - 2; i++)
        line[i] = 32 + rand() % 95;
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

static void
usage(FILE *f)
{
    fprintf(f, "Usage: endlessh [-vh] [-d MSECS] [-l LEN] "
                               "[-m LIMIT] [-p PORT]\n");
    fprintf(f, "  -d INT    Message millisecond delay ["
            XSTR(DEFAULT_DELAY) "]\n");
    fprintf(f, "  -h        Print this help message and exit\n");
    fprintf(f, "  -l INT    Maximum banner line length (3-255) ["
            XSTR(DEFAULT_LINE_LENGTH) "]\n");
    fprintf(f, "  -m INT    Maximum number of clients ["
            XSTR(DEFAULT_MAX_CLIENTS) "]\n");
    fprintf(f, "  -p INT    Listening port [" XSTR(DEFAULT_PORT) "]\n");
    fprintf(f, "  -v        Print diagnostics to standard output "
            "(repeatable)\n");
}

int
main(int argc, char **argv)
{
    int port = DEFAULT_PORT;
    int max_length = DEFAULT_LINE_LENGTH;
    int max_clients = DEFAULT_MAX_CLIENTS;
    long delay = DEFAULT_DELAY;

    int option;
    while ((option = getopt(argc, argv, "d:hl:m:p:v")) != -1) {
        long tmp;
        char *end;
        switch (option) {
            case 'd':
                errno = 0;
                delay = strtol(optarg, &end, 10);
                if (errno || *end || delay < 0) {
                    fprintf(stderr, "endlessh: Invalid delay: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'h':
                usage(stdout);
                exit(EXIT_SUCCESS);
                break;
            case 'l':
                errno = 0;
                tmp = strtol(optarg, &end, 10);
                if (errno || *end || tmp < 3 || tmp > 255) {
                    fprintf(stderr, "endlessh: Invalid line length: %s\n",
                            optarg);
                    exit(EXIT_FAILURE);
                }
                max_length = tmp;
                break;
            case 'm':
                errno = 0;
                tmp = strtol(optarg, &end, 10);
                if (errno || *end || tmp < 1 || tmp > INT_MAX) {
                    fprintf(stderr, "endlessh: Invalid max clients: %s\n",
                            optarg);
                    exit(EXIT_FAILURE);
                }
                max_clients = tmp;
                break;
            case 'p':
                errno = 0;
                tmp = strtol(optarg, &end, 10);
                if (errno || *end || tmp < 1 || tmp > 65535) {
                    fprintf(stderr, "endlessh: Invalid port: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                port = tmp;
                break;
            case 'v':
                if (!loglevel++)
                    setvbuf(stdout, 0, _IOLBF, 0);
                break;
            default:
                usage(stderr);
                exit(EXIT_FAILURE);
        }
    }

    struct sigaction sa = {.sa_handler = sigterm_handler};
    check(sigaction(SIGTERM, &sa, 0));

    int nclients = 0;

    struct queue queue[1];
    queue_init(queue);

    struct pollvec pollvec[1];
    pollvec_init(pollvec);

    int server = socket(AF_INET, SOCK_STREAM, 0);
    check(server);

    int dummy = 1;
    check(setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &dummy, sizeof(dummy)));

    struct sockaddr_in addr = {AF_INET, htons(port), {htonl(INADDR_ANY)}};
    check(bind(server, (void *)&addr, sizeof(addr)));
    check(listen(server, INT_MAX));

    logmsg(LOG_DEBUG, "listen(port=%d)", port);

    srand(time(0));
    while (running) {
        pollvec_clear(pollvec);
        pollvec_push(pollvec, nclients < max_clients ? server : -1, POLLIN);

        /* Poll clients that are due for another message */
        int timeout = -1;
        long long now = uepoch();
        for (struct client *c = queue->head; c; c = c->next) {
            if (c->send_next <= now) {
                pollvec_push(pollvec, c->fd, POLLOUT);
            } else {
                timeout = c->send_next - now;
                break;
            }
        }

        /* Wait for next event */
        logmsg(LOG_DEBUG, "poll(%zu, %d)%s", pollvec->fill, timeout,
                nclients >= max_clients ? " (no accept)" : "");
        int r = poll(pollvec->fds, pollvec->fill, timeout);
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
        if (pollvec->fds[0].revents & POLLIN) {
            int fd = accept(server, 0, 0);
            logmsg(LOG_DEBUG, "accept() = %d", fd);
            if (fd == -1) {
                const char *msg = strerror(errno);
                switch (errno) {
                    case EMFILE:
                    case ENFILE:
                        max_clients = nclients;
                        logmsg(LOG_INFO,
                                "maximum number of clients reduced to %d",
                                nclients);
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
                struct client *client = client_new(fd, uepoch() + delay / 2);
                if (!client) {
                    fprintf(stderr, "endlessh: warning: out of memory\n");
                    close(fd);
                }
                nclients++;
                logmsg(LOG_INFO, "ACCEPT host=%s:%d fd=%d n=%d/%d",
                        client->ipaddr, client->port, client->fd,
                        nclients, max_clients);
                queue_append(queue, client);
            }
        }

        /* Write lines to ready clients */
        for (size_t i = 1; i < pollvec->fill; i++) {
            short fd = pollvec->fds[i].fd;
            short revents = pollvec->fds[i].revents;
            struct client *client = queue_remove(queue, fd);

            if (revents & POLLHUP) {
                client_destroy(client);
                nclients--;

            } else if (revents & POLLOUT) {
                char line[256];
                int len = randline(line, max_length);
                /* Don't really care if send is short */
                ssize_t out = send(fd, line, len, MSG_DONTWAIT);
                if (out < 0)
                    client_destroy(client);
                else {
                    logmsg(LOG_DEBUG, "send(%d) = %d", fd, (int)out);
                    client->bytes_sent += out;
                    client->send_next = uepoch() + delay;
                    queue_append(queue, client);
                }
            }
        }
    }

    pollvec_free(pollvec);
    queue_destroy(queue);
}
