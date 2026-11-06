/*
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

//
// net/web.h -- UDP for the browser build (Emscripten)
//
// A web page can't send UDP, so the client socket is fake: each packet goes to
// the page (Module.q2net), which sends it over a WebSocket to a relay that
// forwards it to the game server as real UDP, and back.
//
//   Module.q2net.enviar(ip, puerto, datos)  ip: Uint8Array(4), puerto: host order
//   Module.q2net.cola                       [{ ip, puerto, datos }] received
//
// Only IPv4 UDP sockets exist. TCP (listen, connect, accept) always fails,
// the browser client doesn't need it.
//

#include <emscripten.h>

EM_JS(int, web_udp_send, (const uint8_t *ip, int port, const void *data, int len), {
    var n = Module['q2net'];
    if (!n || !n.enviar)
        return -1;
    return n.enviar(HEAPU8.slice(ip, ip + 4), port, HEAPU8.slice(data, data + len)) ? len : 0;
});

EM_JS(int, web_udp_recv, (uint8_t *ip, int *port, void *data, int maxlen), {
    var n = Module['q2net'];
    if (!n || !n.cola || !n.cola.length)
        return -1;
    var p = n.cola.shift();
    var len = Math.min(p.datos.length, maxlen);
    HEAPU8.set(p.datos.subarray(0, len), data);
    HEAPU8.set(p.ip, ip);
    HEAP32[port >> 2] = p.puerto;
    return len;
});

EM_JS(int, web_udp_pending, (void), {
    var n = Module['q2net'];
    return n && n.cola && n.cola.length ? 1 : 0;
});

#define WEB_FIRST_FD    100
#define WEB_MAX_FDS     8

static bool web_fds[WEB_MAX_FDS];

static bool web_valid(qsocket_t sock)
{
    return sock >= WEB_FIRST_FD && sock < WEB_FIRST_FD + WEB_MAX_FDS && web_fds[sock - WEB_FIRST_FD];
}

static const char *os_error_string(int err)
{
    return strerror(err);
}

static int os_udp_recv(qsocket_t sock, void *data,
                       size_t len, netadr_t *from)
{
    uint8_t ip[4];
    int port = 0;
    int ret;

    if (!web_valid(sock)) {
        net_error = EBADF;
        return NET_ERROR;
    }

    ret = web_udp_recv(ip, &port, data, len);
    if (ret < 0)
        return NET_AGAIN;

    memset(from, 0, sizeof(*from));
    from->type = NA_IP;
    memcpy(from->ip.u8, ip, 4);
    from->port = htons(port);
    return ret;
}

static int os_udp_send(qsocket_t sock, const void *data,
                       size_t len, const netadr_t *to)
{
    int ret;

    if (!web_valid(sock) || to->type == NA_IP6) {
        net_error = EAFNOSUPPORT;
        return NET_ERROR;
    }

    ret = web_udp_send(to->ip.u8, ntohs(to->port), data, len);
    if (ret < 0) {
        net_error = ENOTCONN;
        return NET_ERROR;
    }
    if (ret == 0)
        return NET_AGAIN;   // relay not connected yet: packet dropped, like UDP

    return ret;
}

static neterr_t os_get_error(void)
{
    net_error = ENOTSUP;
    return NET_ERROR;
}

static int os_recv(qsocket_t sock, void *data, size_t len, int flags)
{
    return os_get_error();
}

static int os_send(qsocket_t sock, const void *data, size_t len, int flags)
{
    return os_get_error();
}

static neterr_t os_listen(qsocket_t sock, int backlog)
{
    return os_get_error();
}

static neterr_t os_accept(qsocket_t sock, qsocket_t *newsock, netadr_t *from)
{
    *newsock = -1;
    return os_get_error();
}

static neterr_t os_connect(qsocket_t sock, const netadr_t *to)
{
    return os_get_error();
}

static neterr_t os_make_nonblock(qsocket_t sock, int val)
{
    return NET_OK;
}

static neterr_t os_setsockopt(qsocket_t sock, int level, int name, int val)
{
    return NET_OK;
}

static neterr_t os_getsockopt(qsocket_t sock, int level, int name, int *val)
{
    *val = 0;
    return NET_OK;
}

static neterr_t os_bind(qsocket_t sock, const struct sockaddr *addr, size_t addrlen)
{
    return NET_OK;
}

static neterr_t os_getsockname(qsocket_t sock, netadr_t *name)
{
    memset(name, 0, sizeof(*name));
    name->type = NA_IP;
    name->port = htons(sock);   // any stable fake port
    return NET_OK;
}

static void os_closesocket(qsocket_t sock)
{
    if (web_valid(sock))
        web_fds[sock - WEB_FIRST_FD] = false;
}

static qsocket_t os_socket(int domain, int type, int protocol)
{
    int i;

    if (domain != AF_INET || type != SOCK_DGRAM) {
        net_error = EAFNOSUPPORT;
        return -1;
    }

    for (i = 0; i < WEB_MAX_FDS; i++) {
        if (!web_fds[i]) {
            web_fds[i] = true;
            return WEB_FIRST_FD + i;
        }
    }

    net_error = EMFILE;
    return -1;
}

// never blocks: the browser runs one frame at a time
static int os_poll(struct pollfd *fds, int nfds, int timeout)
{
    bool pending = web_udp_pending();
    int i, ret = 0;

    for (i = 0; i < nfds; i++) {
        fds[i].revents = 0;
        if (web_valid(fds[i].fd) && (fds[i].events & POLLIN) && pending) {
            fds[i].revents = POLLIN;
            ret++;
        }
    }

    return ret;
}

static neterr_t os_connect_hack(struct pollfd *e)
{
    return NET_OK;
}

static void os_net_init(void)
{
}

static void os_net_shutdown(void)
{
}
