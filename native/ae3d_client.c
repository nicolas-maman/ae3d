/* The other end of the agent channel.
 *
 * ae3d_agent.c is the server a running scene puts on a loopback port. This is
 * the side that asks it questions, so that a tool which measures or judges a
 * scene can be written in Aether against the same engine, rather than in
 * another language against a hand-written copy of the protocol.
 *
 * Line-oriented, one request and one response per line, because that is what
 * the server speaks. Nothing here parses JSON: the caller has a JSON module
 * already and a socket that splits lines is the whole of what it was missing.
 */

#include "ae3d.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
typedef SOCKET ae3d_client_socket;
#  define AE3D_CLIENT_INVALID INVALID_SOCKET
#  define ae3d_client_close_socket closesocket
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <errno.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <unistd.h>
typedef int ae3d_client_socket;
#  define AE3D_CLIENT_INVALID (-1)
#  define ae3d_client_close_socket close
#endif

/* Handles are small integers rather than the socket itself: a Windows SOCKET
   is pointer-sized and does not survive the trip through an int. */
#define AE3D_CLIENT_MAX 8

typedef struct {
    ae3d_client_socket fd;
    char   *pending;      /* bytes read past the end of the last line */
    size_t  used;
    size_t  capacity;
} ae3d_client_conn;

static ae3d_client_conn g_conn[AE3D_CLIENT_MAX];
static char *g_line;
static char  g_error[256];

static void ae3d_client_fail(const char *why) {
    snprintf(g_error, sizeof(g_error), "%s", why);
}

const char *ae3d_client_error(void) { return g_error; }

static ae3d_client_conn *ae3d_client_of(int handle) {
    if (handle < 1 || handle > AE3D_CLIENT_MAX) return NULL;
    if (g_conn[handle - 1].fd == AE3D_CLIENT_INVALID) return NULL;
    return &g_conn[handle - 1];
}

int ae3d_client_connect(const char *host, int port) {
    struct sockaddr_in address;
    ae3d_client_socket fd;
    int slot, flag = 1;
    char service[16];

    g_error[0] = '\0';
    if (!host || port <= 0 || port > 65535) {
        ae3d_client_fail("a host and a port between 1 and 65535");
        return 0;
    }

    for (slot = 0; slot < AE3D_CLIENT_MAX; slot++)
        if (g_conn[slot].fd == AE3D_CLIENT_INVALID || !g_conn[slot].capacity) break;
    if (slot == AE3D_CLIENT_MAX) {
        ae3d_client_fail("no free connection slot");
        return 0;
    }

#if defined(_WIN32)
    {
        static int started = 0;
        if (!started) {
            WSADATA data;
            if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
                ae3d_client_fail("WSAStartup failed");
                return 0;
            }
            started = 1;
        }
    }
#endif

    snprintf(service, sizeof(service), "%d", port);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &address.sin_addr) != 1) {
        ae3d_client_fail("the host must be a numeric IPv4 address");
        return 0;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == AE3D_CLIENT_INVALID) {
        ae3d_client_fail("could not make a socket");
        return 0;
    }
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        ae3d_client_close_socket(fd);
        ae3d_client_fail("nothing answered on that port");
        return 0;
    }
    /* Every request is one small line and the answer is wanted now, so the
       forty milliseconds Nagle would spend gathering a bigger packet is forty
       milliseconds added to every question. */
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(flag));

    g_conn[slot].fd = fd;
    g_conn[slot].used = 0;
    if (!g_conn[slot].pending) {
        g_conn[slot].capacity = 8192;
        g_conn[slot].pending = (char *)malloc(g_conn[slot].capacity);
        if (!g_conn[slot].pending) {
            ae3d_client_close_socket(fd);
            g_conn[slot].fd = AE3D_CLIENT_INVALID;
            g_conn[slot].capacity = 0;
            ae3d_client_fail("out of memory");
            return 0;
        }
    }
    return slot + 1;
}

int ae3d_client_send(int handle, const char *line) {
    ae3d_client_conn *conn = ae3d_client_of(handle);
    size_t remaining;
    const char *cursor;

    g_error[0] = '\0';
    if (!conn || !line) {
        ae3d_client_fail("not a connection");
        return 0;
    }
    cursor = line;
    remaining = strlen(line);
    while (remaining > 0) {
        int sent = (int)send(conn->fd, cursor, (int)remaining, 0);
        if (sent <= 0) {
            ae3d_client_fail("the connection closed while sending");
            return 0;
        }
        cursor += sent;
        remaining -= (size_t)sent;
    }
    if (send(conn->fd, "\n", 1, 0) != 1) {
        ae3d_client_fail("the connection closed while sending");
        return 0;
    }
    return 1;
}

/* One line, without its terminator. The returned pointer is good until the
   next read, which is the same bargain ae3d_agent_next_request makes. */
const char *ae3d_client_read(int handle) {
    ae3d_client_conn *conn = ae3d_client_of(handle);

    g_error[0] = '\0';
    free(g_line);
    g_line = NULL;
    if (!conn) {
        ae3d_client_fail("not a connection");
        return NULL;
    }

    for (;;) {
        size_t i;
        int got;

        for (i = 0; i < conn->used; i++) {
            if (conn->pending[i] != '\n') continue;
            {
                size_t length = i;
                if (length > 0 && conn->pending[length - 1] == '\r') length--;
                g_line = (char *)malloc(length + 1);
                if (!g_line) {
                    ae3d_client_fail("out of memory");
                    return NULL;
                }
                memcpy(g_line, conn->pending, length);
                g_line[length] = '\0';
            }
            memmove(conn->pending, conn->pending + i + 1, conn->used - i - 1);
            conn->used -= i + 1;
            return g_line;
        }

        if (conn->used + 4096 > conn->capacity) {
            size_t wanted = conn->capacity * 2;
            char *grown;
            while (wanted < conn->used + 4096) wanted *= 2;
            /* A single answer larger than this is a protocol fault, not a big
               answer: the grid read is capped well below it. */
            if (wanted > (size_t)1 << 24) {
                ae3d_client_fail("the answer never ended");
                return NULL;
            }
            grown = (char *)realloc(conn->pending, wanted);
            if (!grown) {
                ae3d_client_fail("out of memory");
                return NULL;
            }
            conn->pending = grown;
            conn->capacity = wanted;
        }

        got = (int)recv(conn->fd, conn->pending + conn->used, 4096, 0);
        if (got <= 0) {
            ae3d_client_fail("the connection closed while reading");
            return NULL;
        }
        conn->used += (size_t)got;
    }
}

void ae3d_client_close(int handle) {
    ae3d_client_conn *conn = ae3d_client_of(handle);
    if (!conn) return;
    ae3d_client_close_socket(conn->fd);
    conn->fd = AE3D_CLIENT_INVALID;
    conn->used = 0;
}

/* Called once before any connection, so the invalid-socket value is whatever
   the platform's is rather than zero, which on POSIX is standard input. */
void ae3d_client_init(void) {
    static int done = 0;
    int slot;
    if (done) return;
    for (slot = 0; slot < AE3D_CLIENT_MAX; slot++) {
        g_conn[slot].fd = AE3D_CLIENT_INVALID;
        g_conn[slot].pending = NULL;
        g_conn[slot].used = 0;
        g_conn[slot].capacity = 0;
    }
    done = 1;
}
