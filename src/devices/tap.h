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
#include "riscv.h"

#if 0
typedef int (*pollevent_check_func)(void *arg);
typedef void (*pollevent_func)(int poll_status, void *arg);

struct tap_pollevent_cb
{
};
#endif

enum tap_poll_result
{
    TAPPOLL_NONE = 0,
    TAPPOLL_IN = (1 << 0), /* New data available */
    TAPPOLL_OUT = (1 << 1), /* Data can be sent again, or needs to be sent */
    TAPPOLL_ERR = -1 /* Error occured */
};

struct tap_ops;
struct tap_dev
{
    // spinlock_t lock;
    struct tap_ops *ops;
    void* data;
};

struct tap_ops
{
    bool (*tap_open)(const char* dev, struct tap_dev *td);
    void (*tap_wake)(struct tap_dev *dev);
    enum tap_poll_result (*tap_poll)(struct tap_dev *dev, enum tap_poll_result request, int timeout);
    void (*tap_close)(struct tap_dev *td);

    ptrdiff_t (*tap_send)(struct tap_dev *td, const void *buf, size_t len);
    ptrdiff_t (*tap_recv)(struct tap_dev *td, void *buf, size_t len);

    bool (*tap_is_up)(struct tap_dev *td);
    bool (*tap_set_up)(struct tap_dev *td, bool up);
    bool (*tap_get_mac)(struct tap_dev *td, uint8_t mac[6]);
    bool (*tap_set_mac)(struct tap_dev *td, const uint8_t mac[6]);
};

static inline struct tap_dev* tap_open(const char* dev, struct tap_ops *ops)
{
    struct tap_dev *td = malloc(sizeof(struct tap_dev));
    if (td == NULL) {
        return NULL;
    }

    td->ops = ops;
    if (!ops->tap_open(dev, td)) {
        free(td);
        return NULL;
    }

    return td;
}

static inline void tap_close(struct tap_dev *dev)
{
    dev->ops->tap_close(dev);
    free(dev);
}

static inline void tap_wake(struct tap_dev *dev) { return dev->ops->tap_wake(dev); }
static inline enum tap_poll_result tap_poll(struct tap_dev *dev, enum tap_poll_result request, int timeout)
{
    return dev->ops->tap_poll(dev, request, timeout);
}

static inline ptrdiff_t tap_send(struct tap_dev *td, const void *buf, size_t len) { return td->ops->tap_send(td, buf, len); }
static inline ptrdiff_t tap_recv(struct tap_dev *td, void *buf, size_t len) { return td->ops->tap_recv(td, buf, len); }

static inline bool tap_is_up(struct tap_dev *td) { return td->ops->tap_is_up(td); }
static inline bool tap_set_up(struct tap_dev *td, bool up) { return td->ops->tap_set_up(td, up); }
static inline bool tap_get_mac(struct tap_dev *td, uint8_t mac[6]) { return td->ops->tap_get_mac(td, mac); }
static inline bool tap_set_mac(struct tap_dev *td, const uint8_t mac[6]) { return td->ops->tap_set_mac(td, mac); }

#ifdef USE_TAP_LINUX
extern struct tap_ops tap_linux_ops;
#else
extern struct tap_ops tap_user_ops;
#endif


#if 0
void* tap_workthread(void *arg);
bool tap_pollevent_init(struct tap_pollevent_cb *pollcb, void *eth, pollevent_check_func pchk, pollevent_func pfunc);
#endif

#endif

#endif
