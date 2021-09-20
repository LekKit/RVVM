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

#define GATEWAY_MAC "\x13\x37\xDE\xAD\xBE\xEF"

#define ETH2_IPv6 0x86DD
#define ETH2_IPv4 0x0800
#define ETH2_ARP  0x0806

#define IP_PROTO_ICMP   0x1
#define IP_PROTO_TCP    0x6
#define IP_PROTO_UDP    0x11
#define IP_PROTO_ENCv6  0x29 // IPv6 in IPv4 encapsulation
#define IP_PROTO_ICMPv6 0x3A // IPv6 ICMP

#define ARP_HTYPE_ETHER 0x1
#define ARP_PTYPE_IPv4  ETH2_IPv4
#define ARP_PTYPE_IPv6  ETH2_IPv6
#define ARP_HLEN_ETHER  0x6
#define ARP_PLEN_IPv4   0x4
#define ARP_PLEN_IPv6   0x10
#define ARP_OP_REQUEST  0x1
#define ARP_OP_RESPONSE 0x2

static void eth_send(struct tap_dev *td, vmptr_t buffer, size_t size)
{
    if (size < 65536) {
        /*for (size_t i=0; i<size; ++i) printf("%02x", buffer[i]);
        printf("\n");*/
        ringbuf_put_u16(&td->rx, size);
        ringbuf_put(&td->rx, buffer, size);
        td->flag = TAPPOLL_IN;
    }
}

static void create_ethernet_frame(struct tap_dev *td, uint8_t *frame, uint8_t *buffer, size_t size, uint16_t ether_type)
{
    memcpy(frame, td->mac, 6);
    memcpy(frame+6, GATEWAY_MAC, 6);
    write_uint16_be_m(frame+12, ether_type);
    memcpy(frame+14, buffer, size);
    write_uint32_be_m(frame+14+size, 0);
}

static void handle_ipv4(struct tap_dev *td, vmptr_t buffer, size_t size)
{
    rvvm_info("Handling IPv4 frame");
}

static void handle_arp(struct tap_dev *td, vmptr_t buffer, size_t size)
{
    UNUSED(size);
    rvvm_info("Handling ARP frame");


    uint16_t protocol_type = read_uint16_be_m(buffer + 2);
    uint16_t oper = read_uint16_be_m(buffer + 6);
    if (oper != ARP_OP_REQUEST) return;
    // uint8_t *response;
    // uint8_t response_lenght;
    if (protocol_type == ETH2_IPv4) {
        rvvm_info("Requesting IP addr %d.%d.%d.%d", buffer[24],
                                                    buffer[25], buffer[26],
                                                    buffer[27]);
        uint8_t response[28];
        // response_lenght = 28;
        write_uint16_be_m(response + 2, ARP_PTYPE_IPv4);
        response[5] = 4;
        memcpy(response + 14, buffer + 24, 4);
        memcpy(response + 18, td->mac, 6);
        response[24] = 192;
        response[25] = 168;
        response[26] = 2;
        response[27] = 1;
        write_uint16_be_m(response, ARP_HTYPE_ETHER);
        response[4] = 6;
        write_uint16_be_m(response+6, ARP_OP_RESPONSE);
        memcpy(response+8, GATEWAY_MAC, 6);


        uint8_t frame[28+18];
        create_ethernet_frame(td, frame, response, 28+18, ETH2_ARP);

        eth_send(td, frame, 28+18);
    } else if (protocol_type == ETH2_IPv6) {
        rvvm_info("Requesting IP addr %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x", read_uint16_be_m(buffer + 36),
                                                                                read_uint16_be_m(buffer + 38), read_uint16_be_m(buffer + 40),
                                                                                read_uint16_be_m(buffer + 42), read_uint16_be_m(buffer + 44),
                                                                                read_uint16_be_m(buffer + 46), read_uint16_be_m(buffer + 48),
                                                                                read_uint16_be_m(buffer + 50));
        uint8_t response[52];
        // response_lenght = 52;
        write_uint16_be_m(response + 2, ARP_PTYPE_IPv6);
        response[5] = 16;
        memcpy(response + 14, buffer + 36, 16);
        memcpy(response + 30, td->mac, 6);
        write_uint32_be_m(response + 36, 0x2002);
        write_uint32_be_m(response + 40, 0x0C89);
        write_uint16_be_m(response + 44, 0xA000);
        write_uint16_be_m(response + 46, 0x0);
        write_uint16_be_m(response + 48, 0x0);
        write_uint16_be_m(response + 50, 0x0001);
        write_uint16_be_m(response, ARP_HTYPE_ETHER);
        response[4] = 6;
        write_uint16_be_m(response+6, ARP_OP_RESPONSE);
        memcpy(response+8, GATEWAY_MAC, 6);


        uint8_t frame[52+18];
        create_ethernet_frame(td, frame, response, 52+18, ETH2_ARP);

        eth_send(td, frame, 52+18);
    } else {
        rvvm_info("ARP: Unsupported protocol type");
        return;
    }


}

ptrdiff_t tap_send(struct tap_dev *td, void* buf, size_t len)
{
    vmptr_t buffer = buf;
    if (unlikely(len < 18)) {
        rvvm_warn("Malformed ETH2 frame!");
    }
    uint16_t ether_type = read_uint16_be_m(buffer + 12);
    switch (ether_type) {
        case ETH2_IPv4:
            handle_ipv4(td, buffer + 14, len - 18);
            break;
        case ETH2_IPv6:
            // тут спит котб
            break;
        case ETH2_ARP:
            handle_arp(td, buffer + 14, len - 18);
            break;
        default:
            rvvm_warn("Unknown EtherType!");
            break;
    }
    return len;
}

ptrdiff_t tap_recv(struct tap_dev *td, void* buf, size_t len)
{
    uint16_t rx_len;
    if (ringbuf_get_u16(&td->rx, &rx_len)) {
        if (len >= rx_len) {
            rvvm_info("Receiving Eth frame, length %d", rx_len);
            if (ringbuf_get(&td->rx, buf, rx_len)) return rx_len;
        } else {
            ringbuf_skip(&td->rx, rx_len);
        }
    }
    if (ringbuf_is_empty(&td->rx)) {
        td->flag = TAPPOLL_OUT;
    }
    return 0;
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
    pollev->pollevent(pollev->dev.flag, pollev->pollevent_arg);
}

bool tap_pollevent_init(struct tap_pollevent_cb *pollcb, void *eth, pollevent_check_func pchk, pollevent_func pfunc)
{
    pollcb->pollevent_arg = (void*) eth;
    pollcb->pollevent_check = pchk;
    pollcb->pollevent = pfunc;
    return true;
}
