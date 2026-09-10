// The agent channel's transport: a loopback socket, newline-delimited JSON
// both ways, and the switch that keeps all of it out of a frame that did not
// ask for it.
//
// Requests arrive on a thread of their own and wait in a queue; the engine
// drains it at one point in its frame. Nothing here knows what a request
// means -- that belongs in src/ae3d/agent, where the scene is.

#include "ae3d.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
typedef SOCKET ae3d_socket;
typedef CRITICAL_SECTION ae3d_lock;
#  define AE3D_INVALID_SOCKET INVALID_SOCKET
#  define ae3d_close_socket closesocket
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <pthread.h>
#  include <sys/socket.h>
#  include <unistd.h>
typedef int ae3d_socket;
typedef pthread_mutex_t ae3d_lock;
#  define AE3D_INVALID_SOCKET (-1)
#  define ae3d_close_socket close
#endif

typedef struct ae3d_agent_line {
    char *text;
    struct ae3d_agent_line *next;
} ae3d_agent_line;

static int  g_active;

static ae3d_socket g_listener = AE3D_INVALID_SOCKET;
static ae3d_socket g_client   = AE3D_INVALID_SOCKET;
static int   g_port;
static int   g_stopping;
static char  g_error[256];

static ae3d_lock g_lock;
static int       g_lock_ready;

static ae3d_agent_line *g_head;
static ae3d_agent_line *g_tail;
static char            *g_current;   // handed out by next_request, freed on the next call

static void ae3d_agent_lock_init(void) {
#if defined(_WIN32)
    InitializeCriticalSection(&g_lock);
#else
    pthread_mutex_init(&g_lock, NULL);
#endif
    g_lock_ready = 1;
}

static void ae3d_agent_lock_destroy(void) {
    if (!g_lock_ready) return;
#if defined(_WIN32)
    DeleteCriticalSection(&g_lock);
#else
    pthread_mutex_destroy(&g_lock);
#endif
    g_lock_ready = 0;
}

static void ae3d_agent_lock(void) {
#if defined(_WIN32)
    EnterCriticalSection(&g_lock);
#else
    pthread_mutex_lock(&g_lock);
#endif
}

static void ae3d_agent_unlock(void) {
#if defined(_WIN32)
    LeaveCriticalSection(&g_lock);
#else
    pthread_mutex_unlock(&g_lock);
#endif
}

static int ae3d_agent_fail(const char *reason) {
    snprintf(g_error, sizeof(g_error), "%s", reason);
    return 0;
}

const char *ae3d_agent_error(void) { return g_error; }
int ae3d_agent_active(void) { return g_active; }
int ae3d_agent_port(void) { return g_port; }

static void ae3d_agent_push(const char *text, size_t length) {
    ae3d_agent_line *node = (ae3d_agent_line *)calloc(1, sizeof(*node));
    if (!node) return;
    node->text = (char *)malloc(length + 1);
    if (!node->text) {
        free(node);
        return;
    }
    memcpy(node->text, text, length);
    node->text[length] = '\0';

    ae3d_agent_lock();
    if (g_tail) g_tail->next = node;
    else        g_head = node;
    g_tail = node;
    ae3d_agent_unlock();
}

const char *ae3d_agent_next_request(void) {
    ae3d_agent_line *node;

    if (!g_active) return NULL;

    free(g_current);
    g_current = NULL;

    ae3d_agent_lock();
    node = g_head;
    if (node) {
        g_head = node->next;
        if (!g_head) g_tail = NULL;
    }
    ae3d_agent_unlock();

    if (!node) return NULL;
    g_current = node->text;
    free(node);
    return g_current;
}

static void ae3d_agent_drain_queue(void) {
    ae3d_agent_line *node;
    ae3d_agent_lock();
    node = g_head;
    g_head = NULL;
    g_tail = NULL;
    ae3d_agent_unlock();
    while (node) {
        ae3d_agent_line *next = node->next;
        free(node->text);
        free(node);
        node = next;
    }
}

void ae3d_agent_respond(const char *line) {
    size_t remaining;
    const char *cursor;
    ae3d_socket client;

    if (!g_active || !line) return;

    ae3d_agent_lock();
    client = g_client;
    ae3d_agent_unlock();
    if (client == AE3D_INVALID_SOCKET) return;

    cursor = line;
    remaining = strlen(line);
    while (remaining > 0) {
        int sent = (int)send(client, cursor, (int)remaining, 0);
        if (sent <= 0) return;
        cursor += sent;
        remaining -= (size_t)sent;
    }
    send(client, "\n", 1, 0);
}

static void ae3d_agent_serve(ae3d_socket client) {
    char   chunk[4096];
    char  *buffer = NULL;
    size_t used = 0, capacity = 0;

    for (;;) {
        int got = (int)recv(client, chunk, (int)sizeof(chunk), 0);
        size_t start;
        size_t i;

        if (got <= 0) break;

        if (used + (size_t)got + 1 > capacity) {
            size_t wanted = (capacity ? capacity * 2 : 8192);
            char  *grown;
            while (wanted < used + (size_t)got + 1) wanted *= 2;
            if (wanted > (size_t)1 << 22) break;
            grown = (char *)realloc(buffer, wanted);
            if (!grown) break;
            buffer = grown;
            capacity = wanted;
        }
        memcpy(buffer + used, chunk, (size_t)got);
        used += (size_t)got;

        start = 0;
        for (i = 0; i < used; i++) {
            if (buffer[i] != '\n') continue;
            {
                size_t length = i - start;
                if (length > 0 && buffer[start + length - 1] == '\r') length--;
                if (length > 0) ae3d_agent_push(buffer + start, length);
            }
            start = i + 1;
        }
        if (start > 0) {
            memmove(buffer, buffer + start, used - start);
            used -= start;
        }
    }

    free(buffer);
}

static void ae3d_agent_accept_loop(void) {
    for (;;) {
        ae3d_socket client = accept(g_listener, NULL, NULL);
        int flag = 1;

        if (g_stopping) {
            if (client != AE3D_INVALID_SOCKET) ae3d_close_socket(client);
            return;
        }
        if (client == AE3D_INVALID_SOCKET) return;

        setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(flag));

        ae3d_agent_lock();
        if (g_client != AE3D_INVALID_SOCKET) {
            ae3d_agent_unlock();
            send(client, "{\"ok\":false,\"error\":\"another client is attached\"}\n", 50, 0);
            ae3d_close_socket(client);
            continue;
        }
        g_client = client;
        ae3d_agent_unlock();

        ae3d_agent_serve(client);

        ae3d_agent_lock();
        g_client = AE3D_INVALID_SOCKET;
        ae3d_agent_unlock();
        ae3d_close_socket(client);
    }
}

#if defined(_WIN32)
static DWORD WINAPI ae3d_agent_thread(LPVOID unused) {
    (void)unused;
    ae3d_agent_accept_loop();
    return 0;
}
static HANDLE g_thread;
#else
static void *ae3d_agent_thread(void *unused) {
    (void)unused;
    ae3d_agent_accept_loop();
    return NULL;
}
static pthread_t g_thread;
static int       g_thread_ready;
#endif

int ae3d_agent_start(void) {
    const char *spec = getenv("AE3D_AGENT");
    const char *port_file;
    struct sockaddr_in address;
    socklen_t address_length = sizeof(address);
    int requested = 0;
    int reuse = 1;

    if (!spec || !*spec) return 0;
    if (strcmp(spec, "auto") != 0) {
        requested = atoi(spec);
        if (requested <= 0 || requested > 65535) {
            return ae3d_agent_fail("AE3D_AGENT must be a port number or \"auto\"");
        }
    }

#if defined(_WIN32)
    {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return ae3d_agent_fail("WSAStartup failed");
        }
    }
#endif

    g_listener = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listener == AE3D_INVALID_SOCKET) return ae3d_agent_fail("cannot create the socket");

    setsockopt(g_listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons((unsigned short)requested);

    if (bind(g_listener, (struct sockaddr *)&address, sizeof(address)) != 0) {
        ae3d_close_socket(g_listener);
        g_listener = AE3D_INVALID_SOCKET;
        return ae3d_agent_fail("cannot bind the port");
    }
    if (listen(g_listener, 4) != 0) {
        ae3d_close_socket(g_listener);
        g_listener = AE3D_INVALID_SOCKET;
        return ae3d_agent_fail("cannot listen on the port");
    }
    if (getsockname(g_listener, (struct sockaddr *)&address, &address_length) == 0) {
        g_port = ntohs(address.sin_port);
    } else {
        g_port = requested;
    }

    ae3d_agent_lock_init();

#if defined(_WIN32)
    g_thread = CreateThread(NULL, 0, ae3d_agent_thread, NULL, 0, NULL);
    if (!g_thread) {
        ae3d_close_socket(g_listener);
        g_listener = AE3D_INVALID_SOCKET;
        ae3d_agent_lock_destroy();
        return ae3d_agent_fail("cannot start the agent thread");
    }
#else
    if (pthread_create(&g_thread, NULL, ae3d_agent_thread, NULL) != 0) {
        ae3d_close_socket(g_listener);
        g_listener = AE3D_INVALID_SOCKET;
        ae3d_agent_lock_destroy();
        return ae3d_agent_fail("cannot start the agent thread");
    }
    g_thread_ready = 1;
#endif

    g_active = 1;

    printf("ae3d: agent channel on 127.0.0.1:%d\n", g_port);
    fflush(stdout);
    port_file = getenv("AE3D_AGENT_PORT_FILE");
    if (port_file && *port_file) {
        FILE *out = fopen(port_file, "w");
        if (out) {
            fprintf(out, "%d\n", g_port);
            fclose(out);
        }
    }
    return 1;
}

/* Wake a thread parked in accept(), by giving it the connection it is waiting
   for. Closing the listening socket does not do this on Linux: the descriptor
   goes and the blocked accept stays blocked, so the join below waited for a
   thread that was never going to return -- forever on POSIX, and for the two
   seconds Windows allows before it gives up. The connection lands in the
   backlog whether or not the thread has reached accept yet, so there is no
   window this can be called in where it does not work. */
static void ae3d_agent_wake_listener(void) {
    struct sockaddr_in address;
    ae3d_socket waker;

    if (g_listener == AE3D_INVALID_SOCKET) return;
    waker = socket(AF_INET, SOCK_STREAM, 0);
    if (waker == AE3D_INVALID_SOCKET) return;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons((unsigned short)g_port);
    connect(waker, (struct sockaddr *)&address, sizeof(address));
    ae3d_close_socket(waker);
}

void ae3d_agent_stop(void) {
    ae3d_socket client;

    if (!g_active) return;
    g_active = 0;
    g_stopping = 1;

    ae3d_agent_wake_listener();
    if (g_listener != AE3D_INVALID_SOCKET) {
        ae3d_close_socket(g_listener);
        g_listener = AE3D_INVALID_SOCKET;
    }
    ae3d_agent_lock();
    client = g_client;
    g_client = AE3D_INVALID_SOCKET;
    ae3d_agent_unlock();
    if (client != AE3D_INVALID_SOCKET) {
        char discard[256];
        int drained = 0;
#if defined(_WIN32)
        shutdown(client, SD_SEND);
#else
        shutdown(client, SHUT_WR);
#endif
        while (drained < 64 && recv(client, discard, (int)sizeof(discard), 0) > 0) {
            drained++;
        }
        ae3d_close_socket(client);
    }

#if defined(_WIN32)
    if (g_thread) {
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
#else
    if (g_thread_ready) {
        pthread_join(g_thread, NULL);
        g_thread_ready = 0;
    }
#endif

    ae3d_agent_drain_queue();
    free(g_current);
    g_current = NULL;
    ae3d_agent_lock_destroy();

#if defined(_WIN32)
    WSACleanup();
#endif
    g_stopping = 0;
}
