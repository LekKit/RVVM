/*
libretro.c - RVVM libretro core
Copyright (C) 2023  宋文武 <iyzsong@envs.net>

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

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <stdlib.h>
#include <unistd.h>
#include "libretro.h"

// RVVM headers
#include "rvvmlib.h"
#include "utils.h"
#include "devices/clint.h"
#include "devices/plic.h"
#include "devices/syscon.h"
#include "devices/rtc-goldfish.h"
#include "devices/pci-bus.h"
#include "devices/nvme.h"
#include "devices/framebuffer.h"
#include "devices/i2c-oc.h"
#include "devices/hid_api.h"
#include "devices/ns16550a.h"
#include "devices/chardev.h"
#include "devices/rtl8169.h"


static void fallback_log(enum retro_log_level level, const char *fmt, ...);
static retro_log_printf_t log_cb = fallback_log;
static retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_environment_t environ_cb;
static rvvm_machine_t *machine;
static fb_ctx_t vm_fb;
static hid_keyboard_t *vm_keyboard;
static hid_mouse_t *vm_mouse;
#define NVME_MAX 4
static struct {
    size_t smp;
    size_t mem;
    bool rv64;
    char bootrom[PATH_MAX];
    char kernel[PATH_MAX];
    char nvme[NVME_MAX][PATH_MAX];
    char cmdline[1024];
    uint32_t fb_width;
    uint32_t fb_height;
} machine_opts = {
    .smp = 1,
    .mem = 256,
    .rv64 = true,
    .bootrom = {0},
    .kernel = {0},
    .nvme = {{0}},
    .cmdline = "root=/dev/nvme0n1 rootflags=discard rw console=tty0",
    .fb_width = 640,
    .fb_height = 480,
};

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
  (void)level;
  va_list va;
  va_start(va, fmt);
  vfprintf(stderr, fmt, va);
  va_end(va);
}

static void error_msg(const char *msg)
{
    struct retro_message x = {
        .msg = msg,
        .frames = 180,
    };
    environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &x);
}

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

static hid_key_t retrok_to_hid(unsigned keycode)
{
    static const hid_key_t kbmap[] = {
        [RETROK_a] = HID_KEY_A,
        [RETROK_b] = HID_KEY_B,
        [RETROK_c] = HID_KEY_C,
        [RETROK_d] = HID_KEY_D,
        [RETROK_e] = HID_KEY_E,
        [RETROK_f] = HID_KEY_F,
        [RETROK_g] = HID_KEY_G,
        [RETROK_h] = HID_KEY_H,
        [RETROK_i] = HID_KEY_I,
        [RETROK_j] = HID_KEY_J,
        [RETROK_k] = HID_KEY_K,
        [RETROK_l] = HID_KEY_L,
        [RETROK_m] = HID_KEY_M,
        [RETROK_n] = HID_KEY_N,
        [RETROK_o] = HID_KEY_O,
        [RETROK_p] = HID_KEY_P,
        [RETROK_q] = HID_KEY_Q,
        [RETROK_r] = HID_KEY_R,
        [RETROK_s] = HID_KEY_S,
        [RETROK_t] = HID_KEY_T,
        [RETROK_u] = HID_KEY_U,
        [RETROK_v] = HID_KEY_V,
        [RETROK_w] = HID_KEY_W,
        [RETROK_x] = HID_KEY_X,
        [RETROK_y] = HID_KEY_Y,
        [RETROK_z] = HID_KEY_Z,
        [RETROK_0] = HID_KEY_0,
        [RETROK_1] = HID_KEY_1,
        [RETROK_2] = HID_KEY_2,
        [RETROK_3] = HID_KEY_3,
        [RETROK_4] = HID_KEY_4,
        [RETROK_5] = HID_KEY_5,
        [RETROK_6] = HID_KEY_6,
        [RETROK_7] = HID_KEY_7,
        [RETROK_8] = HID_KEY_8,
        [RETROK_9] = HID_KEY_9,
        [RETROK_RETURN] = HID_KEY_ENTER,
        [RETROK_ESCAPE] = HID_KEY_ESC,
        [RETROK_BACKSPACE] = HID_KEY_BACKSPACE,
        [RETROK_TAB] = HID_KEY_TAB,
        [RETROK_SPACE] = HID_KEY_SPACE,
        [RETROK_MINUS] = HID_KEY_MINUS,
        [RETROK_EQUALS] = HID_KEY_EQUAL,
        [RETROK_LEFTBRACKET] = HID_KEY_LEFTBRACE,
        [RETROK_RIGHTBRACKET] = HID_KEY_RIGHTBRACE,
        [RETROK_BACKSLASH] = HID_KEY_BACKSLASH,
        [RETROK_SEMICOLON] = HID_KEY_SEMICOLON,
        [RETROK_QUOTE] = HID_KEY_APOSTROPHE,
        [RETROK_BACKQUOTE] = HID_KEY_GRAVE,
        [RETROK_COMMA] = HID_KEY_COMMA,
        [RETROK_PERIOD] = HID_KEY_DOT,
        [RETROK_SLASH] = HID_KEY_SLASH,
        [RETROK_CAPSLOCK] = HID_KEY_CAPSLOCK,
        [RETROK_LCTRL] = HID_KEY_LEFTCTRL,
        [RETROK_LSHIFT] = HID_KEY_LEFTSHIFT,
        [RETROK_LALT] = HID_KEY_LEFTALT,
        [RETROK_LMETA] = HID_KEY_LEFTMETA,
        [RETROK_RCTRL] = HID_KEY_RIGHTCTRL,
        [RETROK_RSHIFT] = HID_KEY_RIGHTSHIFT,
        [RETROK_RALT] = HID_KEY_RIGHTALT,
        [RETROK_RMETA] = HID_KEY_RIGHTMETA,
        [RETROK_F1] = HID_KEY_F1,
        [RETROK_F2] = HID_KEY_F2,
        [RETROK_F3] = HID_KEY_F3,
        [RETROK_F4] = HID_KEY_F4,
        [RETROK_F5] = HID_KEY_F5,
        [RETROK_F6] = HID_KEY_F6,
        [RETROK_F7] = HID_KEY_F7,
        [RETROK_F8] = HID_KEY_F8,
        [RETROK_F9] = HID_KEY_F9,
        [RETROK_F10] = HID_KEY_F10,
        [RETROK_F11] = HID_KEY_F11,
        [RETROK_F12] = HID_KEY_F12,
        [RETROK_SYSREQ] = HID_KEY_SYSRQ,
        [RETROK_SCROLLOCK] = HID_KEY_SCROLLLOCK,
        [RETROK_PAUSE] = HID_KEY_PAUSE,
        [RETROK_INSERT] = HID_KEY_INSERT,
        [RETROK_HOME] = HID_KEY_HOME,
        [RETROK_PAGEUP] = HID_KEY_PAGEUP,
        [RETROK_DELETE] = HID_KEY_DELETE,
        [RETROK_END] = HID_KEY_END,
        [RETROK_PAGEDOWN] = HID_KEY_PAGEDOWN,
        [RETROK_RIGHT] = HID_KEY_RIGHT,
        [RETROK_LEFT] = HID_KEY_LEFT,
        [RETROK_DOWN] = HID_KEY_DOWN,
        [RETROK_UP] = HID_KEY_UP,
        [RETROK_NUMLOCK] = HID_KEY_NUMLOCK,
        [RETROK_KP_DIVIDE] = HID_KEY_KPSLASH,
        [RETROK_KP_MULTIPLY] = HID_KEY_KPASTERISK,
        [RETROK_KP_MINUS] = HID_KEY_KPMINUS,
        [RETROK_KP_PLUS] = HID_KEY_KPPLUS,
        [RETROK_KP_ENTER] = HID_KEY_KPENTER,
        [RETROK_KP1] = HID_KEY_KP1,
        [RETROK_KP2] = HID_KEY_KP2,
        [RETROK_KP3] = HID_KEY_KP3,
        [RETROK_KP4] = HID_KEY_KP4,
        [RETROK_KP5] = HID_KEY_KP5,
        [RETROK_KP6] = HID_KEY_KP6,
        [RETROK_KP7] = HID_KEY_KP7,
        [RETROK_KP8] = HID_KEY_KP8,
        [RETROK_KP9] = HID_KEY_KP9,
        [RETROK_KP0] = HID_KEY_KP0,
        [RETROK_KP_PERIOD] = HID_KEY_KPDOT,
        [RETROK_MENU] = HID_KEY_MENU,
    };
    return (keycode < sizeof(kbmap)) ? kbmap[keycode] : HID_KEY_NONE;
}

static void keyboard_cb(bool down, unsigned keycode,
                        uint32_t character, uint16_t key_modifiers)
{
    UNUSED(character);
    UNUSED(key_modifiers);
    hid_key_t key = retrok_to_hid(keycode);
    if (down)
        hid_keyboard_press(vm_keyboard, key);
    else
        hid_keyboard_release(vm_keyboard, key);
}

void retro_set_environment(retro_environment_t cb)
{
    static struct retro_log_callback log;
    static struct retro_keyboard_callback kbd = {
        .callback = keyboard_cb,
    };
    environ_cb = cb;
    if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
        log_cb = log.log;

    environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &kbd);
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
    video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
    UNUSED(cb);
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    UNUSED(cb);
}

void retro_set_input_poll(retro_input_poll_t cb)
{
    input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
    input_state_cb = cb;
}

void retro_get_system_info(struct retro_system_info *info)
{
    info->need_fullpath = true;
    info->valid_extensions = "rvvm";
    info->library_version = "0.6-git";
    info->library_name = "RVVM";
    info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    info->geometry.base_width = machine_opts.fb_width;
    info->geometry.base_height = machine_opts.fb_height;
    info->geometry.max_width = machine_opts.fb_width;
    info->geometry.max_height = machine_opts.fb_height;
    info->geometry.aspect_ratio = (machine_opts.fb_width * 1.0) / machine_opts.fb_height;
    info->timing.fps = 60.0;
    info->timing.sample_rate = 44100.0;
}

void retro_init(void)
{
    enum retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_XRGB8888;
    environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixfmt);
}

static void vm_init(void)
{
    machine = rvvm_create_machine(RVVM_DEFAULT_MEMBASE, machine_opts.mem << 20, machine_opts.smp, machine_opts.rv64);
    vm_fb.width = machine_opts.fb_width;
    vm_fb.height = machine_opts.fb_height;
    vm_fb.format = RGB_FMT_A8R8G8B8;
    vm_fb.buffer = safe_malloc(framebuffer_size(&vm_fb));
    clint_init_auto(machine);
    plic_init_auto(machine);
    pci_bus_init_auto(machine);
    rtc_goldfish_init_auto(machine);
    i2c_oc_init_auto(machine);
    syscon_init_auto(machine);
    framebuffer_init_auto(machine, &vm_fb);
    ns16550a_init_auto(machine, NULL);
#ifdef USE_NET
    rtl8169_init_auto(machine);
#endif
    vm_keyboard = hid_keyboard_init_auto(machine);
    vm_mouse = hid_mouse_init_auto(machine);
    hid_mouse_resolution(vm_mouse, vm_fb.width, vm_fb.height);
    if (rvvm_strlen(machine_opts.bootrom)) {
        if (!rvvm_load_bootrom(machine, machine_opts.bootrom)) {
            error_msg("RVVM: failed to load bootrom");
        }
    } else {
        error_msg("RVVM: No bootrom");
    }
    if (rvvm_strlen(machine_opts.kernel) && !rvvm_load_kernel(machine, machine_opts.kernel)) {
        error_msg("RVVM: failed to load kernel");
    }
    rvvm_set_cmdline(machine, machine_opts.cmdline);
    for (int i = 0; i < NVME_MAX; ++i) {
        if (rvvm_strlen(machine_opts.nvme[i])) {
            log_cb(RETRO_LOG_INFO, "Mount nvme%d: %s\n", i, machine_opts.nvme[i]);
            if (!nvme_init_auto(machine, machine_opts.nvme[i], true)) {
                error_msg("RVVM: failed to mount nvme");
            }
        } else {
            break;
        }
    }
}

bool retro_load_game(const struct retro_game_info *game)
{
    if (!game)
        return false;

    FILE *fp = fopen(game->path, "r");
    if (fp == NULL) {
        log_cb(RETRO_LOG_ERROR, "Failed to open %s: %s\n",
               game->path, strerror(errno));
        return false;
    }

    int nvme_idx = 0;
    char *line = NULL;
    size_t linesize = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linesize, fp)) != -1) {
        if (rvvm_strcmp(line, "rv64\n")) {
            machine_opts.rv64 = true;
            continue;
        }
        if (rvvm_strcmp(line, "rv32\n")) {
            machine_opts.rv64 = false;
            continue;
        }
        if (rvvm_strcmp(line, "\n")) {
            continue;
        }
        char *k = strtok(line, "=");
        if (k == NULL) {
            log_cb(RETRO_LOG_ERROR, "Invalid option: %s\n", line);
            continue;
        }
        char *v = strtok(NULL, "\n");
        if (v == NULL) {
            log_cb(RETRO_LOG_ERROR, "Invalid option: %s\n", line);
            continue;
        }
        if (rvvm_strcmp(k, "mem")) {
            machine_opts.mem = str_to_int_dec(v);
            continue;
        }
        if (rvvm_strcmp(k, "smp")) {
            machine_opts.smp = str_to_int_dec(v);
            continue;
        }
        if (rvvm_strcmp(k, "bootrom")) {
            size_t len = sizeof(machine_opts.bootrom);
            memset(machine_opts.bootrom, 0, len);
            memcpy(machine_opts.bootrom, v, strnlen(v, len-1));
            continue;
        }
        if (rvvm_strcmp(k, "kernel")) {
            size_t len = sizeof(machine_opts.kernel);
            memset(machine_opts.kernel, 0, len);
            memcpy(machine_opts.kernel, v, strnlen(v, len-1));
            continue;
        }
        if (rvvm_strcmp(k, "nvme")) {
            size_t len = sizeof(machine_opts.nvme[0]);
            if (nvme_idx == NVME_MAX) {
                log_cb(RETRO_LOG_ERROR, "Failed to mount %s as nvme, only %d devices are allowed\n", v, NVME_MAX);
                continue;
            }
            char *nvme = machine_opts.nvme[nvme_idx++];
            memset(nvme, 0, len);
            memcpy(nvme, v, strnlen(v, len-1));
            continue;
        }
        if (rvvm_strcmp(k, "cmdline")) {
            size_t len = sizeof(machine_opts.cmdline);
            memset(machine_opts.cmdline, 0, len);
            memcpy(machine_opts.cmdline, v, strnlen(v, len-1));
            continue;
        }
        log_cb(RETRO_LOG_ERROR, "Invalid option: %s\n", line);
    }
    free(line);
    if (ferror(fp)) {
        log_cb(RETRO_LOG_ERROR, "Failed to read %s\n", game->path);
        return false;
    }
    fclose(fp);

    char cwd[1024];
    rvvm_strlcpy(cwd, game->path, sizeof(cwd));
    chdir(dirname(cwd));
    vm_init();
    return rvvm_start_machine(machine);
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
    UNUSED(port);
    UNUSED(device);
}

void retro_deinit(void)
{
}

void retro_reset(void)
{
    rvvm_reset_machine(machine, true);
}

static void mouse_update()
{
    static bool left_pressed = false;
    static bool right_pressed = false;
    static bool middle_pressed = false;
    int16_t x = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
    int16_t y = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
    int16_t left = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT);
    int16_t right = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT);
    int16_t middle = input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE);
    if (x || y)
        hid_mouse_move(vm_mouse, x, y);
    if (left && !left_pressed) {
        hid_mouse_press(vm_mouse, HID_BTN_LEFT);
        left_pressed = true;
    }
    if (right && !right_pressed) {
        hid_mouse_press(vm_mouse, HID_BTN_RIGHT);
        right_pressed = true;
    }
    if (middle && !middle_pressed) {
        hid_mouse_press(vm_mouse, HID_BTN_MIDDLE);
        middle_pressed = true;
    }
    if (!left && left_pressed) {
        hid_mouse_release(vm_mouse, HID_BTN_LEFT);
        left_pressed = false;
    }
    if (!right && right_pressed) {
        hid_mouse_release(vm_mouse, HID_BTN_RIGHT);
        right_pressed = false;
    }
    if (!middle && middle_pressed) {
        hid_mouse_release(vm_mouse, HID_BTN_MIDDLE);
        middle_pressed = false;
    }
}

void retro_run(void)
{
    input_poll_cb();
    mouse_update();
    video_cb(vm_fb.buffer, vm_fb.width, vm_fb.height, vm_fb.width * 4);
}

size_t retro_serialize_size(void)
{
    return 0;
}

bool retro_serialize(void *data, size_t size)
{
    UNUSED(data);
    UNUSED(size);
    return false;
}

bool retro_unserialize(const void *data, size_t size)
{
    UNUSED(data);
    UNUSED(size);
    return false;
}

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
    UNUSED(index);
    UNUSED(enabled);
    UNUSED(code);
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
    UNUSED(game_type);
    UNUSED(info);
    UNUSED(num_info);
    return false;
}

void retro_unload_game(void)
{
    rvvm_reset_machine(machine, false);
    rvvm_free_machine(machine);
    free(vm_fb.buffer);
}

unsigned retro_get_region(void)
{
    return RETRO_REGION_NTSC;
}

void *retro_get_memory_data(unsigned id)
{
    UNUSED(id);
    return 0;
}

size_t retro_get_memory_size(unsigned id)
{
    UNUSED(id);
    return 0;
}
