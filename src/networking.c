/*
networking.c - Network sockets, listeners, multiplexing
Copyright (C) 2021  LekKit <github.com/LekKit>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "networking.h"
#include "utils.h"
#include <string.h>

#ifdef _WIN32

#define FD_SETSIZE 1024

#include <winsock2.h>
typedef SOCKET nethandle_t;
typedef int netaddrlen_t;
#define NET_HANDLE_INVALID INVALID_SOCKET

static void net_init()
{
    static bool wsa_init = false;
    if (!wsa_init) {
        wsa_init = true;
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
            rvvm_warn("Failed to initialize WinSock");
        }
    }
}

#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <unistd.h>
typedef int nethandle_t;
typedef socklen_t netaddrlen_t;
#define NET_HANDLE_INVALID -1

static inline void net_init() {}

#endif

struct net_selector {
    fd_set sockets;
    fd_set ready;
    int max_handle;
    int count;
};

netsocket_t net_create_udp()
{
    net_init();
    nethandle_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock != NET_HANDLE_INVALID) {
        return (netsocket_t)sock;
    } else {
        return NET_SOCK_INVALID;
    }
}

netselector_t net_create_selector()
{
    net_init();
    struct net_selector* selector = safe_calloc(sizeof(struct net_selector), 1);
    FD_ZERO(&selector->sockets);
    FD_ZERO(&selector->ready);
    selector->max_handle = 1;
    selector->count = 0;
    return selector;
}

void net_close(netsocket_t sock)
{
#ifdef _WIN32
    closesocket((nethandle_t)sock);
#else
    close((nethandle_t)sock);
#endif
}

void net_close_selector(netselector_t selector)
{
    free(selector);
}

static void create_sockaddr_in(struct sockaddr_in* addr, uint32_t ip, uint16_t port)
{
    memset(addr, 0, sizeof(struct sockaddr_in));
    addr->sin_addr.s_addr = htonl(ip);
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);
#if defined(__APPLE__)
    addr->sin_len = sizeof(struct sockaddr_in);
#endif
}

bool net_udp_bind(netsocket_t sock, uint16_t port, bool local)
{
    struct sockaddr_in addr;
    create_sockaddr_in(&addr, local ? NET_IP_LOCAL : NET_IP_ANY, port);
    return bind((nethandle_t)sock, (struct sockaddr*)&addr, sizeof(addr)) == 0;
}

uint16_t net_udp_port(netsocket_t sock)
{
    struct sockaddr_in addr;
    netaddrlen_t addr_len = sizeof(struct sockaddr_in);
    if (getsockname((nethandle_t)sock, (struct sockaddr*)&addr, &addr_len)) addr.sin_port = 0;
    return ntohs(addr.sin_port);
}

size_t net_udp_send(netsocket_t sock, const void* buffer, size_t size, uint32_t ip, uint16_t port)
{
    struct sockaddr_in addr;
    create_sockaddr_in(&addr, ip, port);
    int ret = sendto((nethandle_t)sock, buffer, size, 0, (struct sockaddr*)&addr, sizeof(addr));
    return ret > 0 ? ret : 0;
}

size_t net_udp_recv(netsocket_t sock, void* buffer, size_t size, uint32_t* ip, uint16_t* port)
{
    struct sockaddr_in addr;
    netaddrlen_t addr_len = sizeof(struct sockaddr_in);
    int ret = recvfrom((nethandle_t)sock, buffer, size, 0, (struct sockaddr*)&addr, &addr_len);
    *ip = ntohl(addr.sin_addr.s_addr);
    *port = ntohs(addr.sin_port);
    return ret > 0 ? ret : 0;
}

void net_selector_add(netselector_t selector, netsocket_t sock)
{
    struct net_selector* sl = (struct net_selector*)selector;
    nethandle_t handle = (nethandle_t)sock;
    if (handle == NET_HANDLE_INVALID) return;
#ifdef _WIN32
    if (sl->count >= FD_SETSIZE)
#else
    if (handle >= FD_SETSIZE)
#endif
    {
        rvvm_warn("select(): ignoring sockets above FD_SETSIZE");
        return;
    }
    if (FD_ISSET(handle, &sl->sockets)) return;
#ifndef _WIN32
    if (sl->max_handle < handle) sl->max_handle = handle;
#endif
    FD_SET(handle, &sl->sockets);
    sl->count++;
}

void net_selector_remove(netselector_t selector, netsocket_t sock)
{
    struct net_selector* sl = (struct net_selector*)selector;
    nethandle_t handle = (nethandle_t)sock;
    if (handle == NET_HANDLE_INVALID) return;
#ifndef _WIN32
    if (handle >= FD_SETSIZE) return;
#endif
    if (!FD_ISSET(handle, &sl->sockets)) return;
    FD_CLR(handle, &sl->sockets);
    FD_CLR(handle, &sl->ready); // do not trigger selector_ready
    sl->count--;
}

bool net_selector_ready(netselector_t selector, netsocket_t sock)
{
    struct net_selector* sl = (struct net_selector*)selector;
    nethandle_t handle = (nethandle_t)sock;
    return FD_ISSET(handle, &sl->ready);
}

bool net_selector_wait(netselector_t selector)
{
    struct net_selector* sl = (struct net_selector*)selector;
    sl->ready = sl->sockets;
    return select(sl->max_handle + 1, &sl->ready, NULL, NULL, NULL) > 0;
}
