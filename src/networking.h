/*
networking.h - Network sockets, listeners, multiplexing
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

#ifndef NETWORKING_H
#define NETWORKING_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef size_t netsocket_t;
typedef void*  netselector_t;

#define NET_SOCK_INVALID ((size_t)-1)

#define NET_IP_ANY   0x0
#define NET_IP_LOCAL 0x7F000001
#define NET_PORT_ANY 0x0

netsocket_t net_create_udp();
netsocket_t net_create_tcp();
netselector_t net_create_selector();
void net_close(netsocket_t sock);
void net_close_selector(netselector_t selector);

bool   net_tcp_connect(netsocket_t sock, uint32_t ip, uint16_t port);
size_t net_tcp_send(netsocket_t sock, const void* buffer, size_t size);
size_t net_tcp_recv(netsocket_t sock, void* buffer, size_t size);

bool     net_udp_bind(netsocket_t sock, uint16_t port, bool local);
uint16_t net_udp_port(netsocket_t sock);
size_t   net_udp_send(netsocket_t sock, const void* buffer, size_t size, uint32_t ip, uint16_t port);
size_t   net_udp_recv(netsocket_t sock, void* buffer, size_t size, uint32_t* ip, uint16_t* port);

void net_selector_add(netselector_t selector, netsocket_t sock);
void net_selector_remove(netselector_t selector, netsocket_t sock);
bool net_selector_ready(netselector_t selector, netsocket_t sock);
bool net_selector_wait(netselector_t selector);

#endif
