/*
tap_linux.c - Linux TUN/TAP Networking
Copyright (C) 2021  LekKit <github.com/LekKit>
                    cerg2010cerg2010 <github.com/cerg2010cerg2010>

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
#include "threading.h"
#include "utils.h"

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <linux/if_tun.h>

struct tap_dev {
    tap_net_dev_t net;
    thread_ctx_t* thread;
    int           fd;
    int           shut[2];
    char          name[IFNAMSIZ];
};

static void* tap_thread(void* arg)
{
    tap_dev_t* tap = (tap_dev_t*)arg;
    uint8_t buffer[TAP_FRAME_SIZE];
    int ret = 0;
    struct pollfd pfds[2] = {
        {
            .fd = tap->fd,
            .events = POLLIN,
        },
        {
            .fd = tap->shut[0],
            .events = POLLIN | POLLHUP,
        }
    };
    while (true) {
        // Poll events
        poll(pfds, 2, -1);
        // Check for shutdown notification
        if (pfds[1].revents) break;
        // We received a packet
        if (pfds[0].revents & POLLIN) {
            ret = read(tap->fd, buffer, sizeof(buffer));
            if (ret > 0) {
                tap->net.feed_rx(tap->net.net_dev, buffer, ret);
            }
        }
    }
    return arg;
}

tap_dev_t* tap_open(const tap_net_dev_t* net_dev)
{
    tap_dev_t* tap = safe_calloc(sizeof(tap_dev_t), 1);
    tap->net = *net_dev;
    // Open TUN
    tap->fd = open("/dev/net/tun", O_RDWR);
    if (tap->fd < 0) {
        rvvm_error("Failed to open /dev/net/tun: %s", strerror(errno));
        free(tap);
        return NULL;
    }
    // Assign ifname, set TAP mode
    struct ifreq ifr = {0};
    rvvm_strlcpy(ifr.ifr_name, "tap0", sizeof(ifr.ifr_name));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    if (ioctl(tap->fd, TUNSETIFF, &ifr) < 0) {
        rvvm_error("ioctl(TUNSETIFF) failed: %s", strerror(errno));
        close(tap->fd);
        free(tap);
        return NULL;
    }
    if (ioctl(tap->fd, TUNSETOFFLOAD, TUN_F_CSUM | TUN_F_TSO4 | TUN_F_TSO6 | TUN_F_TSO_ECN | TUN_F_UFO) < 0) {
        rvvm_error("ioctl(TUNSETOFFLOAD) failed: %s", strerror(errno));
        close(tap->fd);
        free(tap);
        return NULL;
    }
    // TAP may be assigned a different name
    rvvm_strlcpy(tap->name, ifr.ifr_name, sizeof(tap->name));
    
    // Create shutdown pipe
    if (pipe(tap->shut) < 0) {
        rvvm_error("pipe() failed: %s", strerror(errno));
        close(tap->fd);
        free(tap);
        return NULL;
    }

    // Set the interface up
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    ioctl(sock, SIOCGIFFLAGS, &ifr);
    ifr.ifr_flags |= IFF_UP;    
    ioctl(sock, SIOCSIFFLAGS, &ifr);
    close(sock);

    // Run TAP thread
    tap->thread = thread_create(tap_thread, tap);
    
    return tap;
}

bool tap_send(tap_dev_t* tap, const void* data, size_t size)
{
    return write(tap->fd, data, size) >= 0;
}

bool tap_get_mac(tap_dev_t* tap, uint8_t mac[6])
{
    struct ifreq ifr = {0};
    rvvm_strlcpy(ifr.ifr_name, tap->name, sizeof(ifr.ifr_name));
    if (ioctl(tap->fd, SIOCGIFHWADDR, &ifr) < 0) return false;
    if (ifr.ifr_hwaddr.sa_family != ARPHRD_ETHER) {
        return false;
    }
    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
    return true;
}

bool tap_set_mac(tap_dev_t* tap, const uint8_t mac[6])
{
    struct ifreq ifr = {0};
    rvvm_strlcpy(ifr.ifr_name, tap->name, sizeof(ifr.ifr_name));
    ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
    memcpy(ifr.ifr_hwaddr.sa_data, mac, 6);
    return ioctl(tap->fd, SIOCSIFHWADDR, &ifr) >= 0;
}

void tap_close(tap_dev_t* tap)
{
    // Shut down the TAP thread
    close(tap->shut[1]);
    thread_join(tap->thread);
    
    // Cleanup
    close(tap->fd);
    close(tap->shut[0]);
    free(tap);
}
