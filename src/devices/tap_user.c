/*
tap_user.c - Userspace networking TAP
Copyright (C) 2021  LekKit <github.com/LekKit>
                    0xCatPKG <github.com/PacketCat>

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

#include "rvvm.h"
#include "tap.h"
#include "mem_ops.h"
#include <string.h>

#define GATEWAY_MAC ((const uint8_t*)"\x13\x37\xDE\xAD\xBE\xEF")
#define GATEWAY_IP  ((const uint8_t*)"\xC0\xA8\x00\x01")

#define CLIENT_IP   ((const uint8_t*)"\xC0\xA8\x00\x64")

#define ETH2_IPv6       0x86DD
#define ETH2_IPv4       0x0800
#define ETH2_ARP        0x0806

#define ETH2_WRAP_SIZE  0x12
#define IPv4_WRAP_SIZE  0x14
#define IPv6_WRAP_SIZE  0x28

#define UDP_WRAP_SIZE   0x8
#define TCP_WRAP_SIZE   0x14

#define ARPv4_SIZE      0x1C
#define ARPv6_SIZE      0x34
#define ICMP_SIZE       0x8

#define IP_PROTO_ICMP   0x1
#define IP_PROTO_TCP    0x6
#define IP_PROTO_UDP    0x11
#define IP_PROTO_ENCv6  0x29 // IPv6 in IPv4 encapsulation
#define IP_PROTO_ICMPv6 0x3A // IPv6 ICMP

#define HTYPE_ETHER     0x1
#define PTYPE_IPv4      ETH2_IPv4
#define PTYPE_IPv6      ETH2_IPv6
#define HLEN_ETHER      0x6
#define PLEN_IPv4       0x4
#define PLEN_IPv6       0x10

#define OP_REQUEST      0x1
#define OP_RESPONSE     0x2

#define DHCP_SUBMASK    0x1
#define DHCP_ROUTER     0x3
#define DHCP_DNSERVERS  0x6
#define DHCP_LEASETIME  0x33
#define DHCP_MSG_TYPE   0x35
#define DHCP_DHCPSERVER 0x36
#define DHCP_DISCOVER   0x1
#define DHCP_OFFER      0x2
#define DHCP_REQUEST    0x3
#define DHCP_ACK        0x5

static void eth_send(struct tap_dev *td, vmptr_t buffer, size_t size)
{
    if (size < 65536) {
        /*for (size_t i=0; i<size; ++i) printf("%02x", buffer[i]);
        printf("\n");*/
        ringbuf_put_u16(&td->rx, size);
        ringbuf_put(&td->rx, buffer, size);
        td->flag |= TAPPOLL_IN;
    }
}

static uint8_t* create_eth_frame(struct tap_dev *td, uint8_t *frame, size_t size, uint16_t ether_type)
{
    memcpy(frame, td->mac, HLEN_ETHER);
    memcpy(frame + HLEN_ETHER, GATEWAY_MAC, HLEN_ETHER);
    write_uint16_be_m(frame + 12, ether_type);
    write_uint32_be_m(frame + 14 + size, 0);
    return frame + 14;
}

static void create_arp_frame(struct tap_dev *td, uint8_t *frame, const uint8_t *req_ip)
{
    write_uint16_be_m(frame, HTYPE_ETHER);
    write_uint16_be_m(frame + 2, PTYPE_IPv4);
    frame[4] = HLEN_ETHER;
    frame[5] = PLEN_IPv4;
    write_uint16_be_m(frame + 6, OP_RESPONSE);
    memcpy(frame + 8, GATEWAY_MAC, HLEN_ETHER);
    memcpy(frame + 14, req_ip, PLEN_IPv4);
    memcpy(frame + 18, td->mac, HLEN_ETHER);
    memcpy(frame + 24, req_ip, PLEN_IPv4); // client ip?
}

static uint8_t* create_ipv4_frame(uint8_t *frame, size_t size, uint8_t proto, const uint8_t *src_ip, const uint8_t *dest_ip)
{
    frame[0] = 0x45; // Version 4, IHL 5
    frame[1] = 0; // DSCP, ECN
    write_uint16_be_m(frame + 2, size + IPv4_WRAP_SIZE);
    write_uint16_be_m(frame + 4, 0); // Identification
    write_uint16_be_m(frame + 6, 0); // Flags, Fragment Offset
    frame[8] = 0xFF; // TTL
    frame[9] = proto;
    write_uint16_be_m(frame + 10, 0); // Initial checksum is zero
    memcpy(frame + 12, src_ip, PLEN_IPv4);
    memcpy(frame + 16, dest_ip, PLEN_IPv4);
    
    // Checksum calculation
    uint32_t tmp = 0;
    for (size_t i=0; i<IPv4_WRAP_SIZE; i+=2) {
        tmp += read_uint16_be_m(frame + i);
    }
    tmp = (tmp >> 16) + (tmp & 0xFFFF);
    tmp += tmp >> 16;
    write_uint16_be_m(frame + 10, ~tmp);
    
    return frame + IPv4_WRAP_SIZE;
}

static uint8_t* create_udp_datagram(uint8_t *frame, uint16_t size, uint16_t src_port, uint16_t dst_port)
{
    write_uint16_be_m(frame, src_port);
    write_uint16_be_m(frame + 2, dst_port);
    write_uint16_be_m(frame + 4, size);
    write_uint16_be_m(frame + 6, 0); // checksum
    return frame + UDP_WRAP_SIZE;
}

static void handle_icmp(struct tap_dev *td, vmptr_t buffer, size_t size)
{
    if (buffer[20] != 8) return;
    rvvm_info("Handling ICMP request");

    uint8_t frame[1536];
    uint8_t* ipv4 = create_eth_frame(td, frame, size, ETH2_IPv4);
    uint8_t* icmp = create_ipv4_frame(ipv4, size - IPv4_WRAP_SIZE, IP_PROTO_ICMP, buffer + 16, buffer + 12);
    memcpy(icmp, buffer + IPv4_WRAP_SIZE, size - IPv4_WRAP_SIZE);
    icmp[0] = 0;
    icmp[1] = 0;
    write_uint16_be_m(icmp + 2, 0); // Initial checksum is zero
    
    uint32_t tmp = 0;
    for (size_t i=0; i<size - IPv4_WRAP_SIZE; i+=2) {
        tmp += read_uint16_be_m(icmp + i);
    }
    tmp = (tmp >> 16) + (tmp & 0xFFFF);
    tmp += tmp >> 16;
    write_uint16_be_m(icmp + 2, ~tmp);
    
    eth_send(td, frame, size + ETH2_WRAP_SIZE);
}

static void handle_dhcp(struct tap_dev *td, vmptr_t buffer, size_t size, uint16_t src_port)
{
    if (unlikely(size < 240)) {
        rvvm_warn("Malformed DHCP packet!");
        return;
    }
    
    uint8_t msg_type = 0xFF;
    for (size_t i = 240; i + 2 < size;) {
        if (buffer[i] == DHCP_MSG_TYPE) {
            msg_type = buffer[i+2];
            break;
        }
        i += 2 + buffer[i + 1];
    }
    if (msg_type == 0xFF) {
        rvvm_warn("Lacking DHCP message type!");
        return;
    }
    
    rvvm_info("Handling DHCP");
    uint8_t frame[1536];
    uint8_t* ipv4 = create_eth_frame(td, frame, 273 + UDP_WRAP_SIZE + IPv4_WRAP_SIZE, ETH2_IPv4);
    uint8_t* udp = create_ipv4_frame(ipv4, 273 + UDP_WRAP_SIZE, IP_PROTO_UDP, GATEWAY_IP, (const uint8_t*)"\xFF\xFF\xFF\xFF");
    uint8_t* dhcp = create_udp_datagram(udp, 273, 67, src_port);
    
    dhcp[0] = OP_RESPONSE;
    dhcp[1] = HTYPE_ETHER;
    dhcp[2] = HLEN_ETHER;
    dhcp[3] = 0;
    memcpy(dhcp + 4, buffer + 4, 4);
    write_uint16_be_m(dhcp + 8, 0);
    write_uint16_be_m(dhcp + 10, 0);
    memset(dhcp + 12, 0, PLEN_IPv4);
    memcpy(dhcp + 16, CLIENT_IP, PLEN_IPv4);
    memcpy(dhcp + 20, GATEWAY_IP, PLEN_IPv4);
    memcpy(dhcp + 24, GATEWAY_IP, PLEN_IPv4);
    
    memcpy(dhcp + 28, buffer + 28, 16);
    
    
    memset(dhcp + 44, 0, 64);
    //memcpy(dhcp + 44, "RVVM DHCP", 10);
    
    memset(dhcp + 108, 0, 128);
    
    memcpy(dhcp + 236, buffer + 236, 4); // magic cookie
    
    dhcp[240] = DHCP_MSG_TYPE;
    dhcp[241] = 1;
    if (msg_type == DHCP_DISCOVER) {
        dhcp[242] = DHCP_OFFER;
    } else {
        dhcp[242] = DHCP_ACK;
    }
    
    dhcp[243] = DHCP_SUBMASK;
    dhcp[244] = 4;
    write_uint32_be_m(dhcp + 245, 0xFFFFFF00);
    
    dhcp[249] = DHCP_ROUTER;
    dhcp[250] = 4;
    memcpy(dhcp + 251, GATEWAY_IP, PLEN_IPv4);
    
    dhcp[255] = DHCP_LEASETIME;
    dhcp[256] = 4;
    write_uint32_be_m(dhcp + 257, 86400);
    
    dhcp[261] = DHCP_DHCPSERVER;
    dhcp[262] = 4;
    memcpy(dhcp + 263, GATEWAY_IP, PLEN_IPv4);
    
    dhcp[267] = DHCP_DNSERVERS;
    dhcp[268] = 4;
    write_uint32_be_m(dhcp + 269, 0x01010101);
    
    eth_send(td, frame, 273 + UDP_WRAP_SIZE + IPv4_WRAP_SIZE + ETH2_WRAP_SIZE);
}

static void handle_udp(struct tap_dev *td, vmptr_t buffer, size_t size)
{
    if (unlikely(size < IPv4_WRAP_SIZE + UDP_WRAP_SIZE)) {
        rvvm_warn("Malformed UDP datagram!");
        return;
    }
    rvvm_info("Handling UDP datagram");
    uint16_t src_port = read_uint16_be_m(buffer + 20);
    uint16_t dst_port = read_uint16_be_m(buffer + 22);
    uint16_t udp_size = read_uint16_be_m(buffer + 24) - UDP_WRAP_SIZE;
    if (unlikely(udp_size > (size - (IPv4_WRAP_SIZE + UDP_WRAP_SIZE)))) {
        rvvm_warn("Malformed UDP datagram size!");
        return;
    }

    if (dst_port == 67 && (read_uint32_be_m(buffer + 12) == 0)) {
        handle_dhcp(td, buffer + IPv4_WRAP_SIZE + UDP_WRAP_SIZE, udp_size, src_port);
        return;
    }
}

static void handle_ipv4(struct tap_dev *td, vmptr_t buffer, size_t size)
{
    if (unlikely(size < IPv4_WRAP_SIZE)) {
        rvvm_warn("Malformed IPv4 frame!");
        return;
    }
    rvvm_info("Handling IPv4 frame");
    rvvm_info("Source IP: %d.%d.%d.%d", buffer[12], buffer[13], buffer[14], buffer[15]);
    rvvm_info("Destination IP: %d.%d.%d.%d", buffer[16], buffer[17], buffer[18], buffer[19]);
    uint8_t proto = buffer[9];
    switch (proto) {
        case IP_PROTO_TCP:
            break;
        case IP_PROTO_UDP:
            handle_udp(td, buffer, size);
            break;
        case IP_PROTO_ICMP:
            handle_icmp(td, buffer, size);
            break;
        default:
            rvvm_warn("Unknown protocol encapsulated in IPv4!");
            break;
    }
}

static void handle_ipv6(struct tap_dev *td, vmptr_t buffer, size_t size)
{
    UNUSED(td);
    if (unlikely(size < IPv6_WRAP_SIZE)) {
        rvvm_warn("Malformed IPv6 frame!");
        return;
    }
    rvvm_info("Handling IPv6 frame");
    rvvm_info("Source IP: %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x", read_uint16_be_m(buffer + 8),
                                                                    read_uint16_be_m(buffer + 10), read_uint16_be_m(buffer + 12),
                                                                    read_uint16_be_m(buffer + 14), read_uint16_be_m(buffer + 16),
                                                                    read_uint16_be_m(buffer + 18), read_uint16_be_m(buffer + 20),
                                                                    read_uint16_be_m(buffer + 22));
    rvvm_info("Destination IP: %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x", read_uint16_be_m(buffer + 24),
                                                                         read_uint16_be_m(buffer + 26), read_uint16_be_m(buffer + 28),
                                                                         read_uint16_be_m(buffer + 30), read_uint16_be_m(buffer + 32),
                                                                         read_uint16_be_m(buffer + 34), read_uint16_be_m(buffer + 36),
                                                                         read_uint16_be_m(buffer + 38));
    // тут спит котб
}

static void handle_arp(struct tap_dev *td, vmptr_t buffer, size_t size)
{
    UNUSED(size);
    rvvm_info("Handling ARP frame");

    uint16_t protocol_type = read_uint16_be_m(buffer + 2);
    uint16_t oper = read_uint16_be_m(buffer + 6);
    if (oper != OP_REQUEST) return;
    if (protocol_type == ETH2_IPv4) {
        rvvm_info("ARP: Requesting IP addr %d.%d.%d.%d", buffer[24], buffer[25], buffer[26], buffer[27]);

        uint8_t frame[ARPv4_SIZE + ETH2_WRAP_SIZE];
        uint8_t* arp = create_eth_frame(td, frame, ARPv4_SIZE, ETH2_ARP);
        create_arp_frame(td, arp, buffer + 24);
        eth_send(td, frame, ARPv4_SIZE + ETH2_WRAP_SIZE);
    } else {
        rvvm_info("ARP: Unsupported protocol type");
        return;
    }
}

ptrdiff_t tap_send(struct tap_dev *td, void* buf, size_t len)
{
    vmptr_t buffer = buf;
    len += 4;
    if (unlikely(len < ETH2_WRAP_SIZE)) {
        rvvm_warn("Malformed ETH2 frame!");
        return len;
    }
    uint16_t ether_type = read_uint16_be_m(buffer + 12);
    switch (ether_type) {
        case ETH2_IPv4:
            handle_ipv4(td, buffer + 14, len - ETH2_WRAP_SIZE);
            break;
        case ETH2_IPv6:
            handle_ipv6(td, buffer + 14, len - ETH2_WRAP_SIZE);
            break;
        case ETH2_ARP:
            handle_arp(td, buffer + 14, len - ETH2_WRAP_SIZE);
            break;
        default:
            rvvm_warn("Unknown EtherType!");
            break;
    }
    return len;
}

ptrdiff_t tap_recv(struct tap_dev *td, void* buf, size_t len)
{
    uint16_t rx_len = 0;
    if (ringbuf_get_u16(&td->rx, &rx_len)) {
        if (len >= rx_len) {
            rvvm_info("Receiving Eth frame, length %d", rx_len);
            if (!ringbuf_get(&td->rx, buf, rx_len)) rx_len = 0;
        } else {
            ringbuf_skip(&td->rx, rx_len);
            rx_len = 0;
        }
    }
    if (ringbuf_is_empty(&td->rx)) {
        td->flag = TAPPOLL_OUT;
    }
    return rx_len;
}

bool tap_is_up(struct tap_dev *td)
{
    return td->is_up;
}

bool tap_set_up(struct tap_dev *td, bool up)
{
    td->is_up = up;
    return true;
}

bool tap_get_mac(struct tap_dev *td, uint8_t mac[static 6])
{
    memcpy(mac, td->mac, 6);
    return true;
}

bool tap_set_mac(struct tap_dev *td, uint8_t mac[static 6])
{
    rvvm_info("MAC set to %02x::%02x::%02x::%02x::%02x::%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    memcpy(td->mac, mac, 6);
    return true;
}

int tap_open(const char* dev, struct tap_dev *ret)
{
    rvvm_info("TAP open on \"%s\"", dev);
    ret->is_up = true;
    ret->flag = TAPPOLL_OUT;
    ringbuf_create(&ret->rx, 0x10000);
    return 0;
}

void* tap_workthread(void *arg)
{
    return arg;
}

void tap_wake(struct tap_pollevent_cb *pollev)
{
    int req = pollev->pollevent_check(pollev->pollevent_arg);
    if (req == TAPPOLL_ERR) {
        return;
    }
    pollev->pollevent(req & pollev->dev.flag, pollev->pollevent_arg);
}

bool tap_pollevent_init(struct tap_pollevent_cb *pollcb, void *eth, pollevent_check_func pchk, pollevent_func pfunc)
{
    pollcb->pollevent_arg = (void*) eth;
    pollcb->pollevent_check = pchk;
    pollcb->pollevent = pfunc;
    return true;
}