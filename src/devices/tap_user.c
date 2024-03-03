/*
tap_user.c - Userspace TAP Networking
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

#include "tap_api.h"
#include "networking.h"
#include "threading.h"
#include "spinlock.h"
#include "rvtimer.h"
#include "hashmap.h"
#include "vector.h"
#include "mem_ops.h"
#include "utils.h"

#define GATEWAY_MAC ((const uint8_t*)"\x00\x08\x97\xDE\xC0\xDE")
#define GATEWAY_IP  ((const uint8_t*)"\xC0\xA8\x00\x01")

#define CLIENT_IP   ((const uint8_t*)"\xC0\xA8\x00\x64")

// EtherType for Ethernet Packets
#define ETH2_IPv6       0x86DD
#define ETH2_IPv4       0x0800
#define ETH2_ARP        0x0806

// Address types & sizes
#define HTYPE_ETHER     0x1
#define PTYPE_IPv4      ETH2_IPv4
#define PTYPE_IPv6      ETH2_IPv6
#define HLEN_ETHER      0x6
#define PLEN_IPv4       0x4
#define PLEN_IPv6       0x10

// Header size for each protocol
#define ETH2_HDR_SIZE   0xE
#define IPv4_HDR_SIZE   0x14
#define IPv6_HDR_SIZE   0x28
#define ARPv4_HDR_SIZE  0x1C
#define ARPv6_HDR_SIZE  0x34
#define ICMP_HDR_SIZE   0x4
#define UDP_HDR_SIZE    0x8
#define TCP_HDR_SIZE    0x14

// Protocols encapsulated in IP
#define IP_PROTO_ICMP   0x1
#define IP_PROTO_TCP    0x6
#define IP_PROTO_UDP    0x11
#define IP_PROTO_ENCv6  0x29 // IPv6 in IPv4 encapsulation
#define IP_PROTO_ICMPv6 0x3A // IPv6 ICMP

// OP field for ARP, DHCP
#define OP_REQUEST      0x1
#define OP_RESPONSE     0x2

// ICMP Control Messages
#define ICMP_ECHO_REQ   0x0800
#define ICMP_ECHO_REP   0x0
#define ICMPv6_ECHO_REQ 0x8000
#define ICMPv6_ECHO_REP 0x8100

// DHCP Options
#define DHCP_SUBMASK    0x1
#define DHCP_ROUTER     0x3
#define DHCP_DNSERVERS  0x6
#define DHCP_LEASETIME  0x33
#define DHCP_MSG_TYPE   0x35
#define DHCP_DHCPSERVER 0x36
#define DHCP_ENDMARK    0xFF

// DHCP Message Types
#define DHCP_DISCOVER   0x1
#define DHCP_OFFER      0x2
#define DHCP_REQUEST    0x3
#define DHCP_ACK        0x5

// TCP Flags
#define TCP_FLAG_FIN    0x1
#define TCP_FLAG_SYN    0x2
#define TCP_FLAG_RST    0x4
#define TCP_FLAG_PSH    0x8
#define TCP_FLAG_ACK    0x10

typedef struct tcp_segment tcp_segment_t;
struct tcp_segment {
    tcp_segment_t* next;
    size_t size;
};

typedef struct {
    tcp_segment_t* head;
    tcp_segment_t* tail;
    uint32_t seq;
    uint32_t ack;
    uint32_t seq_ack;
    uint16_t window;
    uint8_t  state;
    bool     win_full;
} tcp_ctx_t;

#define TCP_WRAP_SIZE (ETH2_HDR_SIZE + IPv4_HDR_SIZE + TCP_HDR_SIZE)

#define TCP_STATE_CLOSED      0x00 // Awaiting cleanup
#define TCP_STATE_LISTEN      0x01 // This is a listener socket
#define TCP_STATE_ESTABLISHED 0x02 // This connection was established
#define TCP_STATE_SEND_OPEN   0x04 // Guest sending side open
#define TCP_STATE_RECV_OPEN   0x08 // Guest receiving side open

// Connection actually established and not yet closing
#define TCP_STATE_NORMAL (TCP_STATE_ESTABLISHED | TCP_STATE_SEND_OPEN | TCP_STATE_RECV_OPEN)

#define BOUND_INF 0xFFFF // No UDP timeout

typedef struct {
    net_sock_t* sock;
    tcp_ctx_t*  tcp;  // If NULL, this is a UDP socket
    net_addr_t  addr; // Guest-side address
    uint32_t    timeout;
} tap_sock_t;

typedef vector_t(tap_sock_t*) ts_vec_t;

struct tap_dev {
    spinlock_t    lock;
    tap_net_dev_t net;
    net_poll_t*   poll;
    hashmap_t     udp_ports;
    hashmap_t     tcp_map;
    ts_vec_t      tcp_listeners;
    thread_ctx_t* thread;
    net_sock_t*   shut[2];
    uint8_t       mac[6];

    bool          filt_lan;
};

static inline bool eth_send(tap_dev_t* tap, const void* buffer, size_t size)
{
    return tap->net.feed_rx(tap->net.net_dev, buffer, size);
}

#if 0
static inline uint16_t ip_checksum_combine(uint16_t csum1, uint16_t csum2)
{
    uint32_t sum = ((~csum1) & 0xFFFF) + ((~csum2) & 0xFFFF);
    sum = (sum >> 16) + (sum & 0xFFFF);
    return ~sum;
}
#endif

static uint16_t ip_checksum(const void* data, size_t size, uint16_t initial)
{
    const uint8_t* buffer = (const uint8_t*)data;
    uint32_t sum = (~initial) & 0xFFFF;
    uint8_t tail = size & 1;
    size -= tail;
    for (size_t i = 0; i < size; i += 2) {
        sum += read_uint16_be_m(buffer + i);
    }
    if (tail) {
        sum += ((uint16_t)read_uint8(buffer + size)) << 8;
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += sum >> 16;
    return ~sum;
}

static uint8_t* create_eth_frame(tap_dev_t* tap, uint8_t* frame, uint16_t ether_type)
{
    memcpy(frame, tap->mac, HLEN_ETHER);
    memcpy(frame + HLEN_ETHER, GATEWAY_MAC, HLEN_ETHER);
    write_uint16_be_m(frame + 12, ether_type);
    return frame + ETH2_HDR_SIZE;
}

static void create_arp_frame(tap_dev_t* tap, uint8_t* frame, const void* req_ip)
{
    write_uint16_be_m(frame,     HTYPE_ETHER);
    write_uint16_be_m(frame + 2, PTYPE_IPv4);
    frame[4] = HLEN_ETHER;
    frame[5] = PLEN_IPv4;
    write_uint16_be_m(frame + 6, OP_RESPONSE);
    memcpy(frame + 8,  GATEWAY_MAC, HLEN_ETHER);
    memcpy(frame + 14, req_ip,      PLEN_IPv4);
    memcpy(frame + 18, tap->mac,    HLEN_ETHER);
    memcpy(frame + 24, req_ip,      PLEN_IPv4); // Сlient IP
}

static uint8_t* create_ipv4_frame(uint8_t* frame, size_t size, uint8_t proto, const void* dest_ip, const void* src_ip)
{
    frame[0] = 0x45; // Version 4, IHL 5
    frame[1] = 0;    // DSCP, ECN
    write_uint16_be_m(frame + 2, size + IPv4_HDR_SIZE);
    write_uint16_be_m(frame + 4, 0);      // Identification
    write_uint16_be_m(frame + 6, 0x4000); // Flags, Fragment Offset
    frame[8] = 64;                        // TTL
    frame[9] = proto;
    write_uint16_be_m(frame + 10, 0); // Initial checksum is zero
    memcpy(frame + 12, src_ip,  PLEN_IPv4);
    memcpy(frame + 16, dest_ip, PLEN_IPv4);

    // Header checksum calculation
    write_uint16_be_m(frame + 10, ip_checksum(frame, IPv4_HDR_SIZE, 0));

    return frame + IPv4_HDR_SIZE;
}

#if 0
static uint8_t* create_ipv6_frame(uint8_t* frame, size_t size, uint8_t proto, const void* dest_ip, const void* src_ip)
{
    frame[0] = 0x60;                    // Version 6
    frame[1] = 0;                       // Traffic class
    write_uint16_be_m(frame + 2, 0);    // Flow label
    write_uint16_be_m(frame + 4, size); // Payload Length
    frame[6] = proto;                   // Next Header
    frame[7] = 64;                      // Hop Limit (TTL)
    memcpy(frame + 8,  src_ip,  PLEN_IPv6);
    memcpy(frame + 24, dest_ip, PLEN_IPv6);
    return frame + IPv6_HDR_SIZE;
}
#endif

static uint8_t* create_udp_datagram(uint8_t* udp, size_t size, uint16_t dst_port, uint16_t src_port)
{
    write_uint16_be_m(udp,     src_port);
    write_uint16_be_m(udp + 2, dst_port);
    write_uint16_be_m(udp + 4, size + UDP_HDR_SIZE);
    write_uint16_be_m(udp + 6, 0); // Initial checksum is zero
    return udp + UDP_HDR_SIZE;
}

static void udp_ipv4_checksum(uint8_t* ipv4, size_t size)
{
    uint8_t* udp = ipv4 + IPv4_HDR_SIZE;
    uint16_t csum = ip_checksum(ipv4 + 12, PLEN_IPv4 << 1, 0);
    uint8_t phdr[4];
    phdr[0] = 0;
    phdr[1] = IP_PROTO_UDP;
    write_uint16_be_m(phdr + 2, size + UDP_HDR_SIZE);
    csum = ip_checksum(phdr, 4, csum);
    csum = ip_checksum(udp, size + UDP_HDR_SIZE, csum);
    write_uint16_be_m(udp + 6, csum);
}

static uint8_t* create_tcp_segment(uint8_t* tcp, uint8_t flags, uint32_t seq, uint32_t ack_sn, uint16_t dst_port, uint16_t src_port)
{
    write_uint16_be_m(tcp,     src_port);
    write_uint16_be_m(tcp + 2, dst_port);
    write_uint32_be_m(tcp + 4, seq);
    write_uint32_be_m(tcp + 8, ack_sn);
    tcp[12] = 0x50;                 // Data offset: 5 words
    tcp[13] = flags;
    write_uint16_be_m(tcp + 14, 0xFFFF); // Window size
    write_uint16_be_m(tcp + 16, 0); // Initial checksum (zero)
    write_uint16_be_m(tcp + 18, 0); // Urgent pointer
    return tcp + TCP_HDR_SIZE;
}

static void tcp_ipv4_checksum(uint8_t* ipv4, size_t size)
{
    uint8_t* tcp = ipv4 + IPv4_HDR_SIZE;
    uint16_t csum = ip_checksum(ipv4 + 12, PLEN_IPv4 << 1, 0);
    uint8_t phdr[4];
    phdr[0] = 0;
    phdr[1] = IP_PROTO_TCP;
    write_uint16_be_m(phdr + 2, size + TCP_HDR_SIZE);
    csum = ip_checksum(phdr, 4, csum);
    csum = ip_checksum(tcp, size + TCP_HDR_SIZE, csum);
    write_uint16_be_m(tcp + 16, csum);
}

static void handle_icmp(tap_dev_t* tap, const uint8_t* buffer, size_t size, net_addr_t* dst, net_addr_t* src)
{
    if (size >= ICMP_HDR_SIZE && size < 1460 && read_uint16_be_m(buffer) == ICMP_ECHO_REQ) {
        uint8_t frame[TAP_FRAME_SIZE];
        uint8_t* ipv4 = create_eth_frame(tap, frame, ETH2_IPv4);
        uint8_t* icmp = create_ipv4_frame(ipv4, size, IP_PROTO_ICMP, src->ip, dst->ip);
        memcpy(icmp, buffer, size);
        write_uint16_be_m(icmp, ICMP_ECHO_REP);
        write_uint16_be_m(icmp + 2, 0); // Initial checksum is zero
        write_uint16_be_m(icmp + 2, ip_checksum(icmp, size, 0));
        eth_send(tap, frame, size + IPv4_HDR_SIZE + ETH2_HDR_SIZE);
    }
}

static void handle_dhcp(tap_dev_t* tap, const uint8_t* buffer, size_t size, net_addr_t* dst, net_addr_t* src)
{
    if (unlikely(size < 240)) {
        // Packet too small
        return;
    }

    uint8_t msg_type = DHCP_ENDMARK;
    for (size_t i = 240; i + 2 < size;) {
        if (buffer[i] == DHCP_MSG_TYPE) {
            msg_type = buffer[i+2];
            break;
        }
        i += 2 + buffer[i + 1];
    }
    if (msg_type == DHCP_ENDMARK) {
        // Lacking DHCP message type
        return;
    }

    uint8_t frame[TAP_FRAME_SIZE];
    uint8_t* ipv4 = create_eth_frame(tap, frame, ETH2_IPv4);
    uint8_t* udp = create_ipv4_frame(ipv4, 277 + UDP_HDR_SIZE, IP_PROTO_UDP, (const uint8_t*)"\xFF\xFF\xFF\xFF", GATEWAY_IP);
    uint8_t* dhcp = create_udp_datagram(udp, 277, src->port, dst->port);

    dhcp[0] = OP_RESPONSE;
    dhcp[1] = HTYPE_ETHER;
    dhcp[2] = HLEN_ETHER;
    dhcp[3] = 0;                              // Hop count
    memcpy(dhcp + 4, buffer + 4, 4);          // Client ID
    write_uint16_be_m(dhcp + 8,  0);          // Start time
    write_uint16_be_m(dhcp + 10, 0);          // Flags
    memcpy(dhcp + 12, src->ip,    PLEN_IPv4); // Client IP
    memcpy(dhcp + 16, CLIENT_IP,  PLEN_IPv4); // Offered IP
    memcpy(dhcp + 20, GATEWAY_IP, PLEN_IPv4); // Server address
    memset(dhcp + 24, 0,          PLEN_IPv4); // Relay agent address
    memcpy(dhcp + 28, buffer + 28, 16);       // Client hardware address

    memset(dhcp + 44, 0, 192);                // BOOTP (legacy)
    memcpy(dhcp + 44, "RVVM DHCP", 10);       // Server name

    memcpy(dhcp + 236, buffer + 236, 4);      // Magic cookie

    // DHCP Message type
    dhcp[240] = DHCP_MSG_TYPE;
    dhcp[241] = 1;
    if (msg_type == DHCP_DISCOVER) {
        dhcp[242] = DHCP_OFFER;
    } else {
        dhcp[242] = DHCP_ACK;
    }
    // Advertise /24 subnet
    dhcp[243] = DHCP_SUBMASK;
    dhcp[244] = 4;
    write_uint32_be_m(dhcp + 245, 0xFFFFFF00);
    // Advertise gateway IP
    dhcp[249] = DHCP_ROUTER;
    dhcp[250] = 4;
    memcpy(dhcp + 251, GATEWAY_IP, PLEN_IPv4);
    // Lease time: 1 day (renewable)
    dhcp[255] = DHCP_LEASETIME;
    dhcp[256] = 4;
    write_uint32_be_m(dhcp + 257, 86400);
    // Gateway acts as a DHCP server
    dhcp[261] = DHCP_DHCPSERVER;
    dhcp[262] = 4;
    memcpy(dhcp + 263, GATEWAY_IP, PLEN_IPv4);
    // Advertise usable DNS servers (1.1.1.1, 8.8.8.8)
    dhcp[267] = DHCP_DNSERVERS;
    dhcp[268] = 8;
    write_uint32_be_m(dhcp + 269, 0x01010101);
    write_uint32_be_m(dhcp + 273, 0x08080808);

    eth_send(tap, frame, 277 + UDP_HDR_SIZE + IPv4_HDR_SIZE + ETH2_HDR_SIZE);
}

// Filter unwanted outbound traffic to special IPs
static bool tap_addr_allowed(const tap_dev_t* tap, const net_addr_t* addr)
{
    if (addr->type == NET_TYPE_IPV4) {
        // Filter attempts to reach host loopback from guest (127.x.x.x, 0.x.x.x)
        if (addr->ip[0] == 127) return false;
        if (addr->ip[0] == 0) return false;
        // Filter multicast/broadcast addresses
        if (addr->ip[0] >= 224 && addr->ip[0] <= 239) return false;
        if (addr->ip[0] == 255 && addr->ip[1] == 255 && addr->ip[2] == 255 && addr->ip[3] == 255) return false;
        if (tap->filt_lan) {
            // Filter access to LAN if enabled
            if (addr->ip[0] == 10) return false;
            if (addr->ip[0] == 172 && addr->ip[1] >= 16 && addr->ip[1] < 32) return false;
            if (addr->ip[0] == 192 && addr->ip[1] == 168) return false;
            if (addr->ip[0] == 169 && addr->ip[1] == 254) return false; // Link-local range
        }
    }
    return true;
}

// Route localhost traffic as gateway
static void tap_addr_convert(net_addr_t* addr)
{
    if (addr->ip[0] == 127) memcpy(addr->ip, GATEWAY_IP, 4);
}

static void handle_udp(tap_dev_t* tap, const uint8_t* buffer, size_t size, net_addr_t* dst, net_addr_t* src)
{
    if (unlikely(size < UDP_HDR_SIZE)) {
        // Packet too small
        return;
    }
    src->port = read_uint16_be_m(buffer);
    dst->port = read_uint16_be_m(buffer + 2);
    uint16_t udp_size = read_uint16_be_m(buffer + 4);
    const uint8_t* udb_buff = buffer + UDP_HDR_SIZE;
    if (unlikely(udp_size > size)) {
        // Encoded size exceeds frame size
        return;
    }
    udp_size -= UDP_HDR_SIZE;

    spin_lock(&tap->lock);
    tap_sock_t* ts = (tap_sock_t*)hashmap_get(&tap->udp_ports, src->port);
    if (ts == NULL) {
        if (dst->port == 67 && (read_uint32_be_m(src->ip) == 0)) {
            spin_unlock(&tap->lock);
            handle_dhcp(tap, udb_buff, udp_size, dst, src);
            return;
        }

        net_sock_t* sock = net_udp_bind(NET_IPV4_ANY);
        net_sock_set_blocking(sock, false);
        if (sock) {
            ts = safe_new_obj(tap_sock_t);
            ts->sock = sock;
            ts->addr = *src;
            hashmap_put(&tap->udp_ports, src->port, (size_t)ts);
            net_event_t event = { .data = ts, .flags = NET_POLL_RECV, };
            net_poll_add(tap->poll, ts->sock, &event);
        } else {
            // Couldn't bind UDP port
            spin_unlock(&tap->lock);
            return;
        }
    }
    if (ts->timeout != BOUND_INF) ts->timeout = 0;
    spin_unlock(&tap->lock);
    if (tap_addr_allowed(tap, dst)) net_udp_send(ts->sock, udb_buff, udp_size, dst);
}

static inline uint8_t* tcp_seg_buffer(tcp_segment_t* seg)
{
    return ((uint8_t*)seg) + sizeof(tcp_segment_t);
}

static void tap_tcp_segment_gen(tap_dev_t* tap, tap_sock_t* ts, uint8_t flags, uint32_t seq_sub)
{
    uint8_t frame[ETH2_HDR_SIZE + IPv4_HDR_SIZE + TCP_HDR_SIZE + 4];
    net_addr_t* dst = &ts->addr;
    const net_addr_t* src = net_sock_addr(ts->sock);
    uint8_t* ipv4 = create_eth_frame(tap, frame, ETH2_IPv4);
    size_t opt_size = (flags & TCP_FLAG_SYN) ? 4 : 0;
    uint8_t* tcp = create_ipv4_frame(ipv4, TCP_HDR_SIZE + opt_size, IP_PROTO_TCP, dst->ip, src->ip);
    uint8_t* opt = create_tcp_segment(tcp, flags, ts->tcp->seq - seq_sub, ts->tcp->ack, dst->port, src->port);
    if (flags & TCP_FLAG_SYN) {
        // Change MSS to 1460
        tcp[12] = 0x60;
        opt[0] = 2;
        opt[1] = 4;
        write_uint16_be_m(opt + 2, 1460);
    }
    tcp_ipv4_checksum(ipv4, opt_size);
    eth_send(tap, frame, ETH2_HDR_SIZE + IPv4_HDR_SIZE + TCP_HDR_SIZE + opt_size);
}

static void tap_tcp_segment(tap_dev_t* tap, tap_sock_t* ts, uint8_t flags)
{
    tap_tcp_segment_gen(tap, ts, flags, (flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) ? 1 : 0);
}

static inline bool tcp_window_avail(tcp_ctx_t* tcp)
{
    return tcp->seq - tcp->seq_ack < tcp->window;
}

static inline size_t tcp_ack_amount(tcp_ctx_t* tcp, uint32_t ack)
{
    size_t ret = ack - tcp->seq_ack;
    return (ret < 0x80000000) ? ret : 0; // Care for wraparound
}

static inline size_t tcp_hash_tuple(const net_addr_t* remote, const net_addr_t* local)
{
    // Hash distribution happens in hashmap itself
    size_t hash = (((uint32_t)remote->port) << 16) + local->port;
    if (remote->type == NET_TYPE_IPV6) {
        hash += read_uint64_le_m(remote->ip) + read_uint64_le_m(local->ip);
        hash += read_uint64_le_m(remote->ip + 8) + read_uint64_le_m(local->ip + 8);
    } else {
        hash += read_uint32_le_m(remote->ip) + read_uint32_le_m(local->ip);
    }
    return hash;
}

static tap_sock_t* tap_tcp_lookup(tap_dev_t* tap, const net_addr_t* remote, const net_addr_t* local)
{
    size_t hash = tcp_hash_tuple(remote, local);
    ts_vec_t* vec = (ts_vec_t*)hashmap_get(&tap->tcp_map, hash);
    if (vec) {
        vector_foreach(*vec, i) {
            tap_sock_t* ts = vector_at(*vec, i);
            if (!memcmp(&ts->addr,               local,  sizeof(net_addr_t))
             && !memcmp(net_sock_addr(ts->sock), remote, sizeof(net_addr_t))) return ts;
        }
    }
    return NULL;
}

static void tap_tcp_register(tap_dev_t* tap, tap_sock_t* ts)
{
    const net_addr_t* remote = net_sock_addr(ts->sock);
    const net_addr_t* local = &ts->addr;
    size_t hash = tcp_hash_tuple(remote, local);
    ts_vec_t* vec = (ts_vec_t*)hashmap_get(&tap->tcp_map, hash);
    if (vec == NULL) {
        vec = safe_new_obj(ts_vec_t);
        hashmap_put(&tap->tcp_map, hash, (size_t)vec);
    }
    vector_push_back(*vec, ts);
}

static void tap_tcp_remove(tap_dev_t* tap, tap_sock_t* ts)
{
    const net_addr_t* remote = net_sock_addr(ts->sock);
    const net_addr_t* local = &ts->addr;
    size_t hash = tcp_hash_tuple(remote, local);
    ts_vec_t* vec = (ts_vec_t*)hashmap_get(&tap->tcp_map, hash);
    if (vec) {
        vector_foreach_back(*vec, i) {
            if (vector_at(*vec, i) == ts) {
                vector_erase(*vec, i);
                if (!vector_size(*vec)) {
                    vector_free(*vec);
                    free(vec);
                    hashmap_remove(&tap->tcp_map, hash);
                }
                return;
            }
        }
    }
}

static void tap_tcp_close(tap_dev_t* tap, tap_sock_t* ts)
{
    // Unmap if tap != NULL
    if (tap) tap_tcp_remove(tap, ts);

    net_sock_close(ts->sock);
    while (ts->tcp && ts->tcp->head) {
        tcp_segment_t* seg = ts->tcp->head;
        ts->tcp->head = seg->next;
        free(seg);
    }
    free(ts->tcp);
    free(ts);
}

static bool tap_tcp_arm_poll(tap_dev_t* tap, tap_sock_t* ts)
{
    // Rearm the socket to the eventloop
    net_event_t event = { .data = ts, .flags = NET_POLL_RECV, };
    if (!net_poll_add(tap->poll, ts->sock, &event)) {
        DO_ONCE(rvvm_warn("net_poll_add() failed!"));
        return false;
    }
    return true;
}

static void handle_tcp(tap_dev_t* tap, const uint8_t* buffer, size_t size, net_addr_t* dst, net_addr_t* src)
{
    src->port         = read_uint16_be_m(buffer);
    dst->port         = read_uint16_be_m(buffer + 2);
    uint32_t seq      = read_uint32_be_m(buffer + 4);
    uint32_t ack      = read_uint32_be_m(buffer + 8);
    size_t   data_off = (buffer[12] >> 4) << 2;
    uint8_t  flags    = buffer[13];
    uint16_t window   = read_uint16_be_m(buffer + 14);

    spin_lock(&tap->lock);
    tap_sock_t* ts = tap_tcp_lookup(tap, dst, src);
    if (ts) {
        tcp_ctx_t* tcp = ts->tcp;
        bool reset = !!(flags & TCP_FLAG_RST);
        bool resp_ack = seq != tcp->ack; // Respond with ACK on keepalive
        bool cleanup = false;
        tcp->window = window; // Scale the window
        ts->timeout = 1; // Allow TCP retransmit, but reset keepalive
        if (flags & TCP_FLAG_ACK) {
            while (tcp->head && tcp_ack_amount(tcp, ack) >= tcp->head->size) {
                // Free ACKed segments
                tcp_segment_t* seg = tcp->head;
                tcp->seq_ack += seg->size;
                tcp->head = seg->next;
                free(seg);
                ts->timeout = 0;
            }
            if (tcp->win_full && (tcp->state & TCP_STATE_RECV_OPEN) && tcp_window_avail(tcp)) {
                // Window became available
                if (!tap_tcp_arm_poll(tap, ts)) reset = true;
                tcp->win_full = false;
            }
            if (tcp->seq == tcp->seq_ack + 1 && ack == tcp->seq) {
                if ((tcp->state & TCP_STATE_ESTABLISHED) && !(tcp->state & TCP_STATE_RECV_OPEN)) {
                    // Guest ACKed inbound FIN
                    tcp->seq_ack++;

                    if (tcp->state == TCP_STATE_ESTABLISHED) {
                        // Closed completely
                        cleanup = true;
                    }
                }
                if (tcp->state == (TCP_STATE_SEND_OPEN | TCP_STATE_RECV_OPEN)) {
                    // Guest ACKed inbound SYN ACK
                    if (tap_tcp_arm_poll(tap, ts)) {
                        tcp->state |= TCP_STATE_ESTABLISHED;
                        tcp->seq_ack++;
                    } else reset = true;
                }
                if (tcp->state == TCP_STATE_RECV_OPEN && (flags & TCP_FLAG_SYN)) {
                    // Guest SYN ACKed an inbound connection
                    if (tap_tcp_arm_poll(tap, ts)) {
                        tcp->state |= TCP_STATE_SEND_OPEN | TCP_STATE_ESTABLISHED;
                        tcp->ack = seq + 1;
                        tcp->seq_ack++;
                        resp_ack = true;
                    } else reset = true;
                }
            }
        }
        if ((tcp->state & TCP_STATE_ESTABLISHED) && (tcp->state & TCP_STATE_SEND_OPEN)) {
            // The guest sending side is open
            if (data_off >= TCP_HDR_SIZE && data_off < size) {
                // Send data segment
                size_t send_len = size - data_off;
                size_t seq_off = tcp->ack - seq;
                if (send_len > seq_off) {
                    int32_t result = net_tcp_send(ts->sock, buffer + data_off + seq_off, send_len - seq_off);
                    if (result >= 0) {
                        tcp->ack += result;
                    } else if (result != NET_ERR_BLOCK) {
                        // Connection is reset
                        reset = true;
                    }
                }
                // Acknowledge the bytes actually sent
                // TODO: Reduce amount of response ACKs
                resp_ack = true;
            }
        }
        if ((flags & TCP_FLAG_FIN) && seq + (size - data_off) == tcp->ack) {
            // Close guest sending side
            if (tcp->state & TCP_STATE_SEND_OPEN) {
                net_tcp_shutdown(ts->sock);
                tcp->state &= ~TCP_STATE_SEND_OPEN;
                tcp->ack++;
            }
            if (tcp->state == TCP_STATE_ESTABLISHED) {
                // Closed completely
                cleanup = true;
            }
            resp_ack = true;
        }
        if (reset) {
            // Reset the connection
            if (!(flags & TCP_FLAG_RST)) tap_tcp_segment(tap, ts, TCP_FLAG_RST);
            if (!!(tcp->state & TCP_STATE_ESTABLISHED) != !!(tcp->state & TCP_STATE_RECV_OPEN)) {
                // Closed completely
                cleanup = true;
            }
            tcp->state = TCP_STATE_CLOSED;
        } else if (resp_ack) {
            // Handle keepalive, ACKs
            tap_tcp_segment(tap, ts, TCP_FLAG_ACK);
        }
        if (cleanup) {
            // It's safe to clean up here,
            // since net_poll can't reference tap socket anymore
            tap_tcp_close(tap, ts);
        }
    } else if (flags == TCP_FLAG_SYN) {
        // Initiate new async connection
        net_sock_t* sock = net_tcp_connect(dst, NULL, false);
        if (sock) {
            ts = safe_new_obj(tap_sock_t);
            ts->sock = sock;
            ts->addr = *src;
            ts->tcp = safe_new_obj(tcp_ctx_t);
            ts->tcp->state = TCP_STATE_SEND_OPEN;
            ts->tcp->ack = seq + 1;
            ts->tcp->window = window;
            rvvm_randombytes(&ts->tcp->seq, sizeof(ts->tcp->seq));
            ts->tcp->seq_ack = ts->tcp->seq;

            tap_tcp_register(tap, ts);
            net_event_t event = { .data = ts, .flags = NET_POLL_SEND, };
            net_poll_add(tap->poll, ts->sock, &event);
        } else {
            DO_ONCE(rvvm_warn("net_tcp_connect() failed!"));
        }
    }
    spin_unlock(&tap->lock);
}

static void handle_ipv4(tap_dev_t* tap, const uint8_t* buffer, size_t size)
{
    net_addr_t dst = { .type = NET_TYPE_IPV4, };
    net_addr_t src = { .type = NET_TYPE_IPV4, };
    if (unlikely(size < IPv4_HDR_SIZE)) {
        // Packet too small
        return;
    }
    size_t total_length = read_uint16_be_m(buffer + 2);
    size_t header_length = (buffer[0] & 0xF) << 2;
    uint16_t frag_flags = read_uint16_be_m(buffer + 6);
    if (unlikely(frag_flags & 0x3FFF)) {
        // This is a fragmented frame
        return;
    }
    if (unlikely(size < total_length)) {
        // Encoded size exceeds frame size
        return;
    }

    memcpy(src.ip, buffer + 12, PLEN_IPv4);
    memcpy(dst.ip, buffer + 16, PLEN_IPv4);
    uint8_t proto = buffer[9];
    switch (proto) {
        case IP_PROTO_TCP:
            handle_tcp(tap, buffer + header_length, total_length - header_length, &dst, &src);
            break;
        case IP_PROTO_UDP:
            handle_udp(tap, buffer + header_length, total_length - header_length, &dst, &src);
            break;
        case IP_PROTO_ICMP:
            handle_icmp(tap, buffer + header_length, total_length - header_length, &dst, &src);
            break;
    }
}

static void handle_ipv6(tap_dev_t* tap, const uint8_t* buffer, size_t size)
{
    net_addr_t dst = { .type = NET_TYPE_IPV6, };
    net_addr_t src = { .type = NET_TYPE_IPV6, };
    if (unlikely(size < IPv6_HDR_SIZE)) {
        // Packet too small
        return;
    }
    size_t payload_length = read_uint16_be_m(buffer + 4);
    if (unlikely(size < (payload_length + IPv6_HDR_SIZE))) {
        // Encoded size exceeds frame size
        return;
    }

    memcpy(src.ip,  buffer + 8, PLEN_IPv6);
    memcpy(dst.ip, buffer + 24, PLEN_IPv6);
    uint8_t proto = buffer[6];
    UNUSED(tap);
    UNUSED(proto);
    /*switch (proto) {
        case IP_PROTO_TCP:
            handle_tcp(tap, buffer, total_length);
            break;
        case IP_PROTO_UDP:
            handle_udp(tap, buffer, total_length);
            break;
        case IP_PROTO_ICMPv6:
            handle_icmpv6(tap, buffer + IPv6_HDR_SIZE, payload_length, &dst, &src);
            break;
    }*/
}

static void handle_arp(tap_dev_t* tap, const uint8_t* buffer, size_t size)
{
    if (size < ARPv4_HDR_SIZE) {
        // Packet too small
        return;
    }
    uint8_t frame[ARPv4_HDR_SIZE + ETH2_HDR_SIZE];
    uint16_t ptype = read_uint16_be_m(buffer + 2);
    uint16_t oper  = read_uint16_be_m(buffer + 6);
    if (oper == OP_REQUEST && ptype == ETH2_IPv4 && memcmp(buffer + 14, buffer + 24, 4)) {
        uint8_t* arp = create_eth_frame(tap, frame, ETH2_ARP);
        create_arp_frame(tap, arp, buffer + 24);
        eth_send(tap, frame, ARPv4_HDR_SIZE + ETH2_HDR_SIZE);
    }
}

bool tap_send(tap_dev_t* tap, const void* data, size_t size)
{
    if (unlikely(size < ETH2_HDR_SIZE)) {
        // Packet too small
        return true;
    }
    const uint8_t* buffer = (const uint8_t*)data;
    uint16_t ether_type = read_uint16_be_m(buffer + 12);
    switch (ether_type) {
        case ETH2_IPv4:
            handle_ipv4(tap, buffer + 14, size - ETH2_HDR_SIZE);
            break;
        case ETH2_IPv6:
            handle_ipv6(tap, buffer + 14, size - ETH2_HDR_SIZE);
            break;
        case ETH2_ARP:
            handle_arp(tap, buffer + 14, size - ETH2_HDR_SIZE);
            break;
    }
    return true;
}

bool tap_get_mac(tap_dev_t* tap, uint8_t mac[6])
{
    memcpy(mac, tap->mac, 6);
    return true;
}

bool tap_set_mac(tap_dev_t* tap, const uint8_t mac[6])
{
    memcpy(tap->mac, mac, 6);
    return true;
}

static bool bind_port(tap_dev_t* tap, const net_addr_t* internal, const net_addr_t* external, bool tcp)
{
    net_sock_t* sock = NULL;
    if (tcp) {
        sock = net_tcp_listen(external);
    } else {
        sock = net_udp_bind(external);
    }
    net_sock_set_blocking(sock, false);
    if (sock) {
        tap_sock_t* ts = safe_new_obj(tap_sock_t);
        ts->sock = sock;
        ts->addr = *internal;
        spin_lock(&tap->lock);
        if (tcp) {
            ts->tcp = safe_new_obj(tcp_ctx_t);
            ts->tcp->state = TCP_STATE_LISTEN;
            vector_push_back(tap->tcp_listeners, ts);
        } else {
            ts->timeout = BOUND_INF;
            hashmap_put(&tap->udp_ports, internal->port, (size_t)ts);
        }
        spin_unlock(&tap->lock);
        net_event_t event = { .data = ts, .flags = NET_POLL_RECV, };
        net_poll_add(tap->poll, ts->sock, &event);
    }
    return sock;
}

static void tap_udp_recv(tap_dev_t* tap, tap_sock_t* ts)
{
    uint8_t buffer[TAP_FRAME_SIZE];
    net_addr_t addr;
    size_t offset = ETH2_HDR_SIZE + IPv4_HDR_SIZE + UDP_HDR_SIZE;
    size_t size = sizeof(buffer) - offset;

    if (ts->timeout != BOUND_INF) ts->timeout = 0;
    int32_t result = net_udp_recv(ts->sock, buffer + offset, size, &addr);
    if (result >= 0) {
        size = result;
        tap_addr_convert(&addr);
        uint8_t* ipv4 = create_eth_frame(tap, buffer, ETH2_IPv4);
        uint8_t* udp  = create_ipv4_frame(ipv4, size + UDP_HDR_SIZE, IP_PROTO_UDP, ts->addr.ip, addr.ip);
        create_udp_datagram(udp, size, ts->addr.port, addr.port);
        udp_ipv4_checksum(ipv4, size);
        eth_send(tap, buffer, size + UDP_HDR_SIZE + IPv4_HDR_SIZE + ETH2_HDR_SIZE);
    }
}

static void tap_tcp_recv(tap_dev_t* tap, tap_sock_t* ts)
{
    if (!tcp_window_avail(ts->tcp)) {
        // The window is full, back off and wait for ACK
        net_poll_remove(tap->poll, ts->sock);
        ts->tcp->win_full = true;
        return;
    }

    tcp_segment_t* seg = safe_malloc(sizeof(tcp_segment_t) + TAP_FRAME_SIZE);
    size_t size = TAP_FRAME_SIZE - TCP_WRAP_SIZE;
    int32_t result = net_tcp_recv(ts->sock, tcp_seg_buffer(seg) + TCP_WRAP_SIZE, size);
    if (result > 0) {
        // Push a segment and buffer it for retransmit
        seg->size = result;
        seg->next = NULL;
        uint8_t* ipv4 = create_eth_frame(tap, tcp_seg_buffer(seg), ETH2_IPv4);
        uint8_t* tcp  = create_ipv4_frame(ipv4, seg->size + TCP_HDR_SIZE, IP_PROTO_TCP, ts->addr.ip, net_sock_addr(ts->sock)->ip);
        create_tcp_segment(tcp, TCP_FLAG_PSH | TCP_FLAG_ACK, ts->tcp->seq, ts->tcp->ack, ts->addr.port, net_sock_addr(ts->sock)->port);
        tcp_ipv4_checksum(ipv4, seg->size);
        eth_send(tap, tcp_seg_buffer(seg), seg->size + TCP_WRAP_SIZE);

        ts->tcp->seq += seg->size;

        // Shrink the retransmit segment
        seg = safe_realloc(seg, sizeof(tcp_segment_t) + result + TCP_WRAP_SIZE);
        if (!ts->tcp->head) {
            ts->tcp->head = seg;
            ts->tcp->tail = seg;
        } else {
            ts->tcp->tail->next = seg;
            ts->tcp->tail = seg;
        }
    } else {
        free(seg);
        if (result == NET_ERR_DISCONNECT) {
            // Receiving side closed
            ts->tcp->state &= ~TCP_STATE_RECV_OPEN;
            ts->tcp->seq++;
            tap_tcp_segment(tap, ts, TCP_FLAG_FIN | TCP_FLAG_ACK);

            net_poll_remove(tap->poll, ts->sock);
        } else if (result != NET_ERR_BLOCK) {
            // Connection reset
            tap_tcp_segment(tap, ts, TCP_FLAG_RST);

            tap_tcp_close(tap, ts);
        }
    }
}

static void tap_tcp_accept(tap_dev_t* tap, tap_sock_t* listener)
{
    net_sock_t* sock = net_tcp_accept(listener->sock);
    if (sock) {
        tap_addr_convert(net_sock_addr(sock));
        tap_sock_t* ts = safe_new_obj(tap_sock_t);
        ts->sock = sock;
        ts->addr = listener->addr;
        ts->tcp = safe_new_obj(tcp_ctx_t);
        rvvm_randombytes(&ts->tcp->seq, sizeof(ts->tcp->seq));
        ts->tcp->seq_ack = ts->tcp->seq - 1;
        ts->tcp->state = TCP_STATE_RECV_OPEN;

        tap_tcp_register(tap, ts);
        tap_tcp_segment(tap, ts, TCP_FLAG_SYN);
    }
}

static void tap_tcp_periodic(tap_dev_t* tap, tap_sock_t* ts)
{
    tcp_ctx_t* tcp = ts->tcp;

    if (unlikely(tcp->state != TCP_STATE_NORMAL)) {
        if (tcp->state == TCP_STATE_CLOSED) {
            // Clean up the closed socket
            tap_tcp_close(tap, ts);
            return;
        }
        if (tcp->seq != tcp->seq_ack) {
            switch (tcp->state) {
                case TCP_STATE_RECV_OPEN:
                    // Retry SYN
                    tap_tcp_segment(tap, ts, TCP_FLAG_SYN);
                    break;
                case TCP_STATE_RECV_OPEN | TCP_STATE_SEND_OPEN:
                    // Retry SYN+ACK
                    tap_tcp_segment(tap, ts, TCP_FLAG_SYN | TCP_FLAG_ACK);
                    break;
                case TCP_STATE_ESTABLISHED:
                case TCP_STATE_ESTABLISHED | TCP_STATE_SEND_OPEN:
                    // Retry FIN
                    tap_tcp_segment(tap, ts, TCP_FLAG_FIN | TCP_FLAG_ACK);
                    break;
            }
        }
    }

    if (ts->timeout++) {
        // Upon ACK timeout, retransmit the whole window
        tcp_segment_t* seg = tcp->head;
        uint32_t seq = tcp->seq_ack;
        while (seg && seq - tcp->seq_ack < tcp->window) {
            eth_send(tap, tcp_seg_buffer(seg), seg->size + TCP_WRAP_SIZE);
            seq += seg->size;
            seg = seg->next;
        }
    }
    if (ts->timeout > 50) {
        if (tcp->state & TCP_STATE_ESTABLISHED) {
            // Each 10s, send keepalive packet (seq = last seq - 1)
            tap_tcp_segment_gen(tap, ts, TCP_FLAG_ACK, 1);
        }
        if (ts->timeout > 300 || !(tcp->state & TCP_STATE_ESTABLISHED)) {
            // Connection is assumed dead after a minute
            // Incoming connection has 10s to be accepted
            tap_tcp_close(tap, ts);
        }
    }
}

static void tap_net_periodic(tap_dev_t* tap)
{
    hashmap_foreach(&tap->tcp_map, hash, ts_val) {
        ts_vec_t* vec = (ts_vec_t*)ts_val;
        UNUSED(hash);
        vector_foreach_back(*vec, i) {
            tap_tcp_periodic(tap, vector_at(*vec, i));
        }
    }

    hashmap_foreach(&tap->udp_ports, port, ts_val) {
        tap_sock_t* ts = (tap_sock_t*)ts_val;
        if (ts->timeout != BOUND_INF && ts->timeout++ >= 300) {
            // UDP timeouts after 60 seconds
            hashmap_remove(&tap->udp_ports, port);
            net_sock_close(ts->sock);
            free(ts);
            break;
        }
    }
}

static void* tap_thread(void* arg)
{
    tap_dev_t* tap = arg;
    rvtimer_t timer;

    net_event_t events[64];
    rvtimer_init(&timer, 1000);
    while (true) {
        size_t size = net_poll_wait(tap->poll, events, 64, 200);
        spin_lock(&tap->lock);
        for (size_t i=0; i<size; ++i) {
            if (events[i].data == NULL) {
                // Shutdown notification
                spin_unlock(&tap->lock);
                return NULL;
            }
            tap_sock_t* ts = events[i].data;
            if (ts->tcp) {
                // TCP socket
                if (events[i].flags & NET_POLL_SEND) {
                    if (net_tcp_status(ts->sock)) {
                        // Connection succeeded
                        net_poll_remove(tap->poll, ts->sock);
                        ts->tcp->state |= TCP_STATE_RECV_OPEN;
                        ts->tcp->seq++;
                        tap_tcp_segment(tap, ts, TCP_FLAG_SYN | TCP_FLAG_ACK);
                    } else {
                        // Connection refused or timeout
                        tap_tcp_close(tap, ts);
                    }
                } else if (ts->tcp->state == TCP_STATE_LISTEN) {
                    tap_tcp_accept(tap, ts);
                } else {
                    tap_tcp_recv(tap, ts);
                }
            } else {
                // UDP
                tap_udp_recv(tap, ts);
            }
        }

        if (rvtimer_get(&timer) >= 200) {
            tap_net_periodic(tap);
            rvtimer_init(&timer, 1000);
        }
        spin_unlock(&tap->lock);
    }
    return NULL;
}

tap_dev_t* tap_open()
{
    tap_dev_t* tap = safe_new_obj(tap_dev_t);
    // Generate a random local unicast MAC
    rvvm_randombytes(tap->mac, 6);
    tap->mac[0] = (tap->mac[0] & 0xFE) | 0x2;

    tap->poll = net_poll_create();

    // Create shutdown sockpair & watch for it
    net_tcp_sockpair(tap->shut);
    net_event_t event = { .data = NULL, };
    net_poll_add(tap->poll, tap->shut[0], &event);

    hashmap_init(&tap->udp_ports, 16);
    hashmap_init(&tap->tcp_map, 16);

    tap_portfwd(tap, "tcp/[::1]:2022=22");
    tap_portfwd(tap, "tcp/127.0.0.1:2022=22");

    return tap;
}

void tap_attach(tap_dev_t* tap, const tap_net_dev_t* net_dev)
{
    if (tap->net.feed_rx == NULL) {
        tap->net = *net_dev;
        tap->thread = thread_create(tap_thread, tap);
    }
}

bool tap_portfwd(tap_dev_t* tap, const char* fwd)
{
    net_addr_t host, guest;
    char host_str[256];
    const char* tcp_prefix = rvvm_strfind(fwd, "tcp/");
    const char* udp_prefix = rvvm_strfind(fwd, "udp/");
    if (tcp_prefix || udp_prefix) fwd += 4;
    const char* delim = rvvm_strfind(fwd, "=");
    if (!delim) return false;
    rvvm_strlcpy(host_str, fwd, EVAL_MIN((size_t)(delim - fwd + 1), sizeof(host_str)));
    if (!net_parse_addr(&host, host_str) || !net_parse_addr(&guest, delim + 1)) {
        return false;
    }
    // Accomodate addr types (If only port is passed at either side, etc)
    if (guest.type == NET_TYPE_IPV4) guest.type = host.type;
    if (host.type == NET_TYPE_IPV4) host.type = guest.type;
    if (guest.type == NET_TYPE_IPV4 && memcmp(guest.ip, net_ipv4_any_addr.ip, PLEN_IPv4) == 0) {
        memcpy(guest.ip, CLIENT_IP, PLEN_IPv4);
    }
    bool ret = true;
    if (tcp_prefix || !udp_prefix) ret = bind_port(tap, &guest, &host, true);
    if (ret && (udp_prefix || !tcp_prefix)) ret = bind_port(tap, &guest, &host, false);
    if (!ret) {
        rvvm_error("Failed to bind %s", host_str);
        if (host.port && host.port < 1024) rvvm_error("Binding ports below 1024 requires root/admin privilege");
    }
    return ret;
}

void tap_close(tap_dev_t* tap)
{
    // Shut down the TAP thread
    net_sock_close(tap->shut[1]);
    thread_join(tap->thread);

    // Cleanup
    hashmap_foreach(&tap->tcp_map, hash, ts_val) {
        ts_vec_t* vec = (ts_vec_t*)ts_val;
        UNUSED(hash);
        vector_foreach_back(*vec, i) {
            tap_tcp_close(NULL, vector_at(*vec, i));
        }
        vector_free(*vec);
        free(vec);
    }
    hashmap_foreach(&tap->udp_ports, port, ts_val) {
        UNUSED(port);
        tap_sock_t* ts = (tap_sock_t*)ts_val;
        net_sock_close(ts->sock);
        free(ts);
    }
    vector_foreach(tap->tcp_listeners, i) {
        tap_tcp_close(NULL, vector_at(tap->tcp_listeners, i));
    }
    vector_free(tap->tcp_listeners);
    hashmap_destroy(&tap->udp_ports);
    hashmap_destroy(&tap->tcp_map);
    net_sock_close(tap->shut[0]);
    net_poll_close(tap->poll);
    free(tap);
}
