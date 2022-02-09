/*
tap.c - TUN/TAP network device for Linux
Copyright (C) 2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>

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

#ifdef USE_NET

#include "riscv.h"
#include "tap.h"
#include "hashmap.h"
#include "networking.h"
#include "utils.h"

#include <string.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <linux/if_tun.h>
#include <poll.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

struct tap_dev_linux {
    int _fd;
    int _sockfd; /* For stuff like setting the interface up */
    char _ifname[IFNAMSIZ];
    /* used to wake up the poll thread for tx operation */
    int _wakefds[2];
};

bool tap_linux_is_up(struct tap_dev *dev)
{
    struct tap_dev_linux *td = (struct tap_dev_linux*) dev->data;
    struct ifreq ifr = { };
    strncpy(ifr.ifr_name, td->_ifname, sizeof(ifr.ifr_name));
    int err = ioctl(td->_sockfd, SIOCGIFFLAGS, &ifr);
    if (err < 0) {
        return false;
    }

    return !!(ifr.ifr_flags & IFF_UP);
}

bool tap_linux_set_up(struct tap_dev *dev, bool up)
{
    struct tap_dev_linux *td = (struct tap_dev_linux*) dev->data;
    struct ifreq ifr;
    strncpy(ifr.ifr_name, td->_ifname, sizeof(ifr.ifr_name));
    int err = ioctl(td->_sockfd, SIOCGIFFLAGS, &ifr);
    if (err < 0) {
        return false;
    }

    if (up) {
        ifr.ifr_flags |= IFF_UP;
    } else {
        ifr.ifr_flags &= ~IFF_UP;
    }

    return ioctl(td->_sockfd, SIOCSIFFLAGS, &ifr) >= 0;
}

bool tap_linux_get_mac(struct tap_dev *dev, uint8_t mac[6])
{
    struct tap_dev_linux *td = (struct tap_dev_linux*) dev->data;
    struct ifreq ifr;
    strncpy(ifr.ifr_name, td->_ifname, sizeof(ifr.ifr_name));
    int err = ioctl(td->_fd, SIOCGIFHWADDR, &ifr);
    if (err < 0) {
        return false;
    }

    if (ifr.ifr_hwaddr.sa_family != ARPHRD_ETHER) {
        return false;
    }

    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
    return true;
}

bool tap_linux_set_mac(struct tap_dev *dev, const uint8_t mac[6])
{
    struct tap_dev_linux *td = (struct tap_dev_linux*) dev->data;
    struct ifreq ifr;
    strncpy(ifr.ifr_name, td->_ifname, sizeof(ifr.ifr_name));
    ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
    memcpy(ifr.ifr_hwaddr.sa_data, mac, 6);
    return ioctl(td->_fd, SIOCSIFHWADDR, &ifr) >= 0;
}

ptrdiff_t tap_linux_send(struct tap_dev *dev, const void *buf, size_t len)
{
    struct tap_dev_linux *td = (struct tap_dev_linux*) dev->data;
    return write(td->_fd, buf, len);
}

ptrdiff_t tap_linux_recv(struct tap_dev *dev, void *buf, size_t len)
{
    struct tap_dev_linux *td = (struct tap_dev_linux*) dev->data;
    return read(td->_fd, buf, len);
}

void tap_linux_wake(struct tap_dev *dev)
{
    struct tap_dev_linux *td = (struct tap_dev_linux*) dev->data;
    char x = '\0';
    write(td->_wakefds[1], &x, 1);
}

bool tap_linux_open(const char* dev, struct tap_dev *td)
{
    struct tap_dev_linux *ret = (struct tap_dev_linux*) malloc(sizeof(struct tap_dev_linux));
    if (ret == NULL) {
        return false;
    }

    td->data = ret;

    if (dev == NULL) {
        strncpy(ret->_ifname, "tap0", IFNAMSIZ);
    } else {
        strncpy(ret->_ifname, dev, IFNAMSIZ);
    }

    int err;
    ret->_fd = open("/dev/net/tun", O_RDWR);
    if (ret->_fd < 0) {
        rvvm_error("/dev/net/tun open error\n");
        err = ret->_fd;
        goto err;
    }

    struct ifreq ifr = { };
    strncpy(ifr.ifr_name, dev ? dev : "tap0", IFNAMSIZ);
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    err = ioctl(ret->_fd, TUNSETIFF, &ifr);
    if (err < 0) {
        rvvm_error("ioctl(TUNSETIFF) error %d\n", err);
        goto err_close;
    }

    /* fd now describes the virtual interface */

    /* Note: the device name may be different after the call above */
    strncpy(ret->_ifname, ifr.ifr_name, sizeof(ret->_ifname));

    /* Get the socket */
    ret->_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ret->_sockfd < 0) {
        goto err_close;
    }

    if (pipe(ret->_wakefds) < 0)
    {
        rvvm_error("pipe failed\n");
        goto err_close;
    }

    /* Up the interface. Ignore errors since this is a privilleged operation */
    tap_linux_set_up(td, true);
    return true;
err_close:
    close(ret->_fd);
err:
    free(ret);
    return false;
}

void tap_linux_close(struct tap_dev *dev)
{
    struct tap_dev_linux *td = (struct tap_dev_linux*) dev->data;
    tap_linux_set_up(dev, false);
    close(td->_wakefds[0]);
    close(td->_wakefds[1]);
    close(td->_sockfd);
    free(td);
}

static enum tap_poll_result tap_linux_poll(struct tap_dev *dev, enum tap_poll_result req, int timeout)
{
    struct tap_dev_linux *td = (struct tap_dev_linux*) dev->data;
    struct pollfd tapfd[2] = {
        {
            .fd = td->_fd,
        },
        {
            .fd = td->_wakefds[0],
            .events = POLLIN,
        }
    };

    if (req & TAPPOLL_IN) {
        tapfd[0].events |= POLLIN;
    }

    if (req & TAPPOLL_OUT) {
        tapfd[0].events |= POLLOUT;
    }

    int pollret = poll(tapfd, 2, timeout);
    if (pollret < 0) {
        printf("poll error %d\n", pollret);
        return TAPPOLL_ERR;
    }

    int ret = 0;
    if (tapfd[0].revents & POLLIN) {
        ret |= TAPPOLL_IN;
    }

    if (tapfd[0].revents & POLLOUT) {
        ret |= TAPPOLL_OUT;
    }

    if (tapfd[1].revents & POLLIN) {
        /* Clear the kernel FIFO */
        char x;
        read(tapfd[1].fd, &x, 1);

        /* Actually we want to send something. Do not care if we'll block */
        ret |= TAPPOLL_OUT;
    }

    if (ret == 0) {
        /* timeout, possibly */
        return pollret == 0 ? 0 : TAPPOLL_ERR;
    }

    return ret;
}

struct tap_ops tap_linux_ops = {
    .tap_open = tap_linux_open,
    .tap_wake = tap_linux_wake,
    .tap_poll = tap_linux_poll,
    .tap_send = tap_linux_send,
    .tap_recv = tap_linux_recv,
    .tap_is_up = tap_linux_is_up,
    .tap_set_up = tap_linux_set_up,
    .tap_get_mac = tap_linux_get_mac,
    .tap_set_mac = tap_linux_set_mac,
    .tap_close = tap_linux_close,
};
#endif
