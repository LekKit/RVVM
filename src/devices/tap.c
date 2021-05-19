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

bool tap_is_up(struct tap_dev *td)
{
    struct ifreq ifr = { };
    strncpy(ifr.ifr_name, td->_ifname, sizeof(ifr.ifr_name));
    int err = ioctl(td->_sockfd, SIOCGIFFLAGS, &ifr);
    if (err < 0) {
        return false;
    }

    return !!(ifr.ifr_flags & IFF_UP);
}

bool tap_set_up(struct tap_dev *td, bool up)
{
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

bool tap_get_mac(struct tap_dev *td, uint8_t mac[static 6])
{
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

bool tap_set_mac(struct tap_dev *td, uint8_t mac[static 6])
{
    struct ifreq ifr;
    strncpy(ifr.ifr_name, td->_ifname, sizeof(ifr.ifr_name));
    ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
    memcpy(ifr.ifr_hwaddr.sa_data, mac, 6);
    return ioctl(td->_fd, SIOCSIFHWADDR, &ifr) >= 0;
}

int tap_open(const char* dev, struct tap_dev *ret)
{
    int err;
    if (dev == NULL) {
        strncpy(ret->_ifname, "tap0", IFNAMSIZ);
    } else {
        strncpy(ret->_ifname, dev, IFNAMSIZ);
    }

    ret->_fd = open("/dev/net/tun", O_RDWR);
    if (ret->_fd < 0) {
        printf("/dev/net/tun open error\n");
        err = ret->_fd;
        goto err;
    }

    struct ifreq ifr = { };
    strncpy(ifr.ifr_name, dev ? dev : "tap0", IFNAMSIZ);
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    err = ioctl(ret->_fd, TUNSETIFF, &ifr);
    if (err < 0) {
        printf("ioctl(TUNSETIFF) error %d\n", err);
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

    /* Up the interface. Ignore errors since this is a privilleged operation */
    tap_set_up(ret, true);
    return 0;
err_close:
    close(ret->_fd);
err:
    return err;
}

ptrdiff_t tap_send(struct tap_dev *td, void *buf, size_t len)
{
    return write(td->_fd, buf, len);
}

ptrdiff_t tap_recv(struct tap_dev *td, void *buf, size_t len)
{
    return read(td->_fd, buf, len);
}

static int tap_poll(int fd, int wakefd, int req, int timeout)
{
    struct pollfd tapfd[2] = {
        {
            .fd = fd,
        },
        {
            .fd = wakefd,
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

void tap_wake(struct tap_pollevent_cb *pollev)
{
    char x = '\0';
    write(pollev->_wakefds[1], &x, 1);
}

void* tap_workthread(void *arg)
{
    struct tap_pollevent_cb *pollev = (struct tap_pollevent_cb *) arg;
    while (1) {
        int req = pollev->pollevent_check(pollev->pollevent_arg);
        if (req == TAPPOLL_ERR) {
            continue;
        }

        req = tap_poll(pollev->dev._fd, pollev->_wakefds[0], req, -1);
        if (req == TAPPOLL_ERR) {
            continue;
        }

        pollev->pollevent(req, pollev->pollevent_arg);
    }

    return NULL;
}

bool tap_pollevent_init(struct tap_pollevent_cb *pollcb, void *eth, pollevent_check_func pchk, pollevent_func pfunc)
{
    pollcb->pollevent_arg = (void*) eth;
    pollcb->pollevent_check = pchk;
    pollcb->pollevent = pfunc;
    if (pipe(pollcb->_wakefds) < 0)
    {
        printf("pipe failed\n");
        return false;
    }
    return true;
}

#endif
