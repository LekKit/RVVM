/*
tap.h - TUN/TAP network device for Linux
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

#ifndef TAP_H
#define TAP_H

#ifdef USE_NET
#include "rvvm_types.h"
#include "spinlock.h"

#ifdef USE_TAP_LINUX
#include <net/if.h>

struct tap_dev {
    int _fd;
    int _sockfd; /* For stuff like setting the interface up */
    char _ifname[IFNAMSIZ];
};
#else
#include "ringbuf.h"

struct tap_dev {
    struct ringbuf rx;
    int flag;
    uint8_t mac[6];
    bool is_up;
};

#endif

typedef int (*pollevent_check_func)(void *arg);
typedef void (*pollevent_func)(int poll_status, void *arg);

struct tap_pollevent_cb
{
    spinlock_t lock;
    pollevent_check_func pollevent_check;
    pollevent_func pollevent;
    void *pollevent_arg;
    struct tap_dev dev;

    /* Private; used to wake up the poll thread for tx operation */
    int _wakefds[2];
};

enum tap_poll_result
{
    TAPPOLL_IN = (1 << 0), /* New data available */
    TAPPOLL_OUT = (1 << 1), /* Data can be sent again, or needs to be sent */
    TAPPOLL_ERR = -1 /* Error occured */
};

int tap_open(const char* dev, struct tap_dev *ret);
void tap_wake(struct tap_pollevent_cb *pollev);
void* tap_workthread(void *arg);
bool tap_pollevent_init(struct tap_pollevent_cb *pollcb, void *eth, pollevent_check_func pchk, pollevent_func pfunc);

ptrdiff_t tap_send(struct tap_dev *td, void *buf, size_t len);
ptrdiff_t tap_recv(struct tap_dev *td, void *buf, size_t len);

bool tap_is_up(struct tap_dev *td);
bool tap_set_up(struct tap_dev *td, bool up);
bool tap_get_mac(struct tap_dev *td, uint8_t mac[static 6]);
bool tap_set_mac(struct tap_dev *td, uint8_t mac[static 6]);
#endif

#endif
