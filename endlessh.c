#define _POSIX_C_SOURCE  200112L
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <poll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define DEFAULT_MAX_CLIENTS   4096
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

struct client {
    long long send_next; // epoch milliseconds for next line
    struct client *next;
    int fd;
};

static struct client *
client_new(int fd, long long send_next)
{
    struct client *c = malloc(sizeof(*c));
    if (c) {
        c->send_next = send_next;
        c->next = 0;
        c->fd = fd;
    }
    return c;
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
check(int r)
{
    if (r == -1) {
        fprintf(stderr, "endlessh: fatal: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

static int
randline(char *line)
{
    int len = 2 + rand() % 251;
    for (int i = 0; i < len - 2; i++)
        line[i] = 32 + rand() % 95;
    line[len - 2] = 13;
    line[len - 1] = 10;
    if (memcmp(line, "SSH-", 4) == 0)
        line[0] = 'X';
    return len;
}

static void
logmsg(const char *format, ...)
{
    long long now = uepoch();
    fprintf(stderr, "%lld.%03lld: ", now / 1000, now % 1000);
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void
usage(FILE *f)
{
    fprintf(f, "Usage: endlessh [-vh] [-d MSECS] [-m LIMIT] [-p PORT]\n");
    fprintf(f, "  -d INT    Message millisecond delay ["
            XSTR(DEFAULT_DELAY) "]\n");
    fprintf(f, "  -h        Print this help message and exit\n");
    fprintf(f, "  -m INT    Maximum number of clients ["
            XSTR(DEFAULT_MAX_CLIENTS) "]\n");
    fprintf(f, "  -p INT    Listening port [" XSTR(DEFAULT_PORT) "]\n");
    fprintf(f, "  -v        Print diagnostics to standard output "
            "(repeatable)\n");
}

int
main(int argc, char **argv)
{
    int verbose = 0;
    int port = DEFAULT_PORT;
    long delay = DEFAULT_DELAY;
    long max_clients = DEFAULT_MAX_CLIENTS;

    int option;
    while ((option = getopt(argc, argv, "d:hm:p:v")) != -1) {
        long tmp;
        char *end;
        switch (option) {
            case 'd':
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
            case 'm':
                tmp = strtol(optarg, &end, 10);
                if (errno || *end || tmp < 0) {
                    fprintf(stderr, "endlessh: Invalid port: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                max_clients = tmp;
                break;
            case 'p':
                tmp = strtol(optarg, &end, 10);
                if (errno || *end || tmp < 0 || tmp > 65535) {
                    fprintf(stderr, "endlessh: Invalid port: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                port = tmp;
                break;
            case 'v':
                verbose++;
                break;
            default:
                usage(stderr);
                exit(EXIT_FAILURE);
        }
    }

    long nclients = 0;

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

    if (verbose >= 1)
        logmsg("listen(port=%d)", port);

    srand(time(0));
    for (;;) {
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
        if (verbose >= 2)
            logmsg("poll(%zu, %d)%s", pollvec->fill, timeout,
                   nclients == max_clients ? " (no accept)" : "");
        int r = poll(pollvec->fds, pollvec->fill, timeout);
        if (verbose >= 2)
            logmsg("= %d", r);
        if (r == -1) {
            switch (errno) {
                case EINTR:
                    if (verbose >= 1)
                        logmsg("EINTR");
                    continue;
                default:
                    fprintf(stderr, "endlessh: fatal: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
            }
        }

        /* Check for new incoming connections */
        if (pollvec->fds[0].revents & POLLIN) {
            int fd = accept(server, 0, 0);
            if (verbose >= 1)
                logmsg("accept() = %d", fd);
            if (fd == -1) {
                const char *msg = strerror(errno);
                switch (errno) {
                    case ECONNABORTED:
                    case EINTR:
                    case EMFILE:
                    case ENFILE:
                    case ENOBUFS:
                    case ENOMEM:
                    case EPROTO:
                        fprintf(stderr, "endlessh: warning: %s\n", msg);
                        continue;
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
                queue_append(queue, client);
                nclients++;
            }
        }

        /* Write lines to ready clients */
        for (size_t i = 1; i < pollvec->fill; i++) {
            short fd = pollvec->fds[i].fd;
            short revents = pollvec->fds[i].revents;
            struct client *client = queue_remove(queue, fd);

            if (revents & POLLHUP) {
                if (verbose >= 1)
                    logmsg("close(%d)", fd);
                close(fd);
                free(client);
                nclients--;

            } else if (revents & POLLOUT) {
                char line[255];
                int len = randline(line);
                /* Don't really care if this send fails */
                send(fd, line, len, MSG_DONTWAIT);
                client->send_next = uepoch() + delay;
                queue_append(queue, client);
            }
        }
    }
}
