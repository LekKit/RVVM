/*
main.c - RVVM CLI, API usage example
Copyright (C) 2021  LekKit <github.com/LekKit>
                    cerg2010cerg2010 <github.com/cerg2010cerg2010>
                    Mr0maks <mr.maks0443@gmail.com>
                    KotB <github.com/0xCatPKG>
                    fish4terrisa-MSDSM <fish4terrisa@fishinix.eu.org>

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

#include "rvvmlib.h"
#include "rvvm_user.h"
#include "rvvm_isolation.h"
#include "utils.h"
#include "dlib.h"

#include "devices/clint.h"
#include "devices/plic.h"
#include "devices/ns16550a.h"
#include "devices/gui_window.h"
#include "devices/syscon.h"
#include "devices/rtc-goldfish.h"
#include "devices/pci-bus.h"
#include "devices/pci-vfio.h"
#include "devices/nvme.h"
#include "devices/ata.h"
#include "devices/eth-oc.h"
#include "devices/rtl8169.h"
#include "devices/i2c-oc.h"

#include <stdio.h>
#include <inttypes.h>

#ifdef _WIN32
// For unicode args/console
#include <windows.h>
#endif

static void print_help(void)
{
#if defined(_WIN32) && !defined(UNDER_CE)
    const wchar_t* help = L"\n"
#else
    printf("\n"
#endif
           "  ██▀███   ██▒   █▓ ██▒   █▓ ███▄ ▄███▓\n"
           " ▓██ ▒ ██▒▓██░   █▒▓██░   █▒▓██▒▀█▀ ██▒\n"
           " ▓██ ░▄█ ▒ ▓██  █▒░ ▓██  █▒░▓██    ▓██░\n"
           " ▒██▀▀█▄    ▒██ █░░  ▒██ █░░▒██    ▒██ \n"
           " ░██▓ ▒██▒   ▒▀█░     ▒▀█░  ▒██▒   ░██▒\n"
           " ░ ▒▓ ░▒▓░   ░ ▐░     ░ ▐░  ░ ▒░   ░  ░\n"
           "   ░▒ ░ ▒░   ░ ░░     ░ ░░  ░  ░      ░\n"
           "   ░░   ░      ░░       ░░  ░      ░   \n"
           "    ░           ░        ░         ░   \n"
           "               ░        ░              \n"
           "\n"
           "https://github.com/LekKit/RVVM (v"RVVM_VERSION")\n"
           "\n"
           "License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>\n"
           "This is free software: you are free to change and redistribute it.\n"
           "There is NO WARRANTY, to the extent permitted by law.\n"
           "\n"
           "Usage: rvvm <firmware> [-m 256M] [-k kernel] [-i drive.img] ...\n"
           "\n"
           "    <firmware>       Initial M-mode firmware (OpenSBI [+ U-Boot], etc)\n"
           "    -k, -kernel ...  Optional S-mode kernel payload (Linux, U-Boot, etc)\n"
           "    -i, -image  ...  Attach preferred storage image (Currently as NVMe)\n"
           "    -m, -mem 1G      Memory amount, default: 256M\n"
           "    -s, -smp 4       Cores count, default: 1\n"
           "    -rv32            Enable 32-bit RISC-V, 64-bit by default\n"
           "    -cmdline    ...  Override payload kernel command line\n"
           "    -append     ...  Modify payload kernel command line\n"
           "    -res 1280x720    Set display(s) resolution\n"
           "    -poweroff_key    Send HID_KEY_POWER instead of exiting on GUI close\n"
           "    -portfwd 8080=80 Port forwarding (Extended: tcp/127.0.0.1:8080=80)\n"
           "    -vfio_pci   ...  PCI passthrough via VFIO (Example: 00:02.0), needs root\n"
           "    -nvme       ...  Explicitly attach storage image as NVMe device\n"
           "    -ata        ...  Explicitly attach storage image as ATA (IDE) device\n"
           "    -nogui           Disable display GUI\n"
           "    -nonet           Disable networking\n"
           "    -serial     ...  Add more serial ports (Via pty/pipe path), or null\n"
           "    -dtb        ...  Pass custom Device Tree Blob to the machine\n"
           "    -dumpdtb    ...  Dump auto-generated DTB to file\n"
           "    -v, -verbose     Enable verbose logging\n"
           "    -h, -help        Show this help message\n"
           "\n"
           "    -noisolation     Disable seccomp/pledge isolation\n"
           "    -nojit           Disable RVJIT (For debug purposes, slow!)\n"
#if defined(_WIN32) && !defined(UNDER_CE)
           "\n";
    WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), help, wcslen(help), NULL, NULL);
#else
           "\n");
#endif
}

static size_t get_arg(const char** argv, const char** arg_name, const char** arg_val)
{
    if (argv[0][0] == '-') {
        size_t offset = (argv[0][1] == '-') ? 2 : 1;
        *arg_name = &argv[0][offset];
        for (size_t i=0; argv[0][offset + i] != 0; ++i) {
            if (argv[0][offset + i] == '=') {
                // Argument format -arg=val
                *arg_val = &argv[0][offset + i + 1];
                return 1;
            }
        }

        if (argv[1] == NULL || argv[1][0] == '-') {
            // Argument format -arg
            *arg_val = "";
            return 1;
        } else {
            // Argument format -arg val
            *arg_val = argv[1];
            return 2;
        }
    } else {
        *arg_name = "bootrom";
        *arg_val = argv[0];
        return 1;
    }
}

static bool cmp_arg(const char* arg, const char* name)
{
    while (*arg && *arg == *name) {
        arg++;
        name++;
    }
    return *name == 0 && (*arg == '=' || *arg == 0);
}

static bool rvvm_cli_configure(rvvm_machine_t* machine, int argc, const char** argv,
                               const char* bootrom, tap_dev_t* tap)
{
    const char* arg_name = "";
    const char* arg_val = "";
    size_t      arg_size = 0;
    UNUSED(tap);
    rvvm_append_cmdline(machine, "root=/dev/nvme0n1 rootflags=discard rw");
    if (rvvm_getarg("cmdline")) rvvm_set_cmdline(machine, rvvm_getarg("cmdline"));
    if (rvvm_getarg("append")) rvvm_append_cmdline(machine, rvvm_getarg("append"));

    if (!rvvm_load_bootrom(machine, bootrom)) return false;
    if (rvvm_getarg("k") && !rvvm_load_kernel(machine, rvvm_getarg("k"))) return false;
    if (rvvm_getarg("kernel") && !rvvm_load_kernel(machine, rvvm_getarg("kernel"))) return false;
    if (rvvm_getarg("dtb") && !rvvm_load_dtb(machine, rvvm_getarg("dtb"))) return false;

    for (int i=1; i<argc; i+=arg_size) {
        arg_size = get_arg(argv + i, &arg_name, &arg_val);
        if (cmp_arg(arg_name, "i") || cmp_arg(arg_name, "image") || cmp_arg(arg_name, "nvme")) {
            if (!nvme_init_auto(machine, arg_val, true)) {
                rvvm_error("Failed to attach image \"%s\"", arg_val);
                return false;
            }
        } else if (cmp_arg(arg_name, "ata")) {
            if (!ata_init_auto(machine, arg_val, true)) {
                rvvm_error("Failed to attach image \"%s\"", arg_val);
                return false;
            }
        } else if (cmp_arg(arg_name, "serial")) {
            chardev_t* chardev = chardev_pty_create(arg_val);
            if (chardev == NULL && !rvvm_strcmp(arg_val, "null")) return false;
            ns16550a_init_auto(machine, chardev);
        } else if (cmp_arg(arg_name, "res")) {
            size_t len = 0;
            uint32_t fb_x = str_to_uint_base(arg_val, &len, 10);
            uint32_t fb_y = str_to_uint_base(arg_val + len + 1, NULL, 10);
            if (arg_val[len] != 'x') fb_y = 0;
            if (fb_x < 100 || fb_y < 100) {
                rvvm_error("Invalid resoulution: %s, expects 640x480", arg_val);
                return false;
            }
            gui_window_init_auto(machine, fb_x, fb_y);
        } else if (cmp_arg(arg_name, "portfwd")) {
#ifdef USE_NET
            if (!tap_portfwd(tap, arg_val)) return false;
#endif
        } else if (cmp_arg(arg_name, "vfio_pci")) {
            if (!pci_vfio_init_auto(machine, arg_val)) return false;
        }
    }
    if (rvvm_getarg("dumpdtb")) rvvm_dump_dtb(machine, rvvm_getarg("dumpdtb"));
    return true;
}

static int rvvm_cli_main(int argc, const char** argv)
{
    const char* arg_name = "";
    const char* arg_val = "";
    size_t      arg_size = 0;

    // Default params: 1 core, 256M ram, riscv64, 640x480 screen
    const char* bootrom = NULL;
    size_t mem = 256 << 20;
    size_t smp = 1;
    bool   rv64 = true;
    tap_dev_t* tap = NULL;

    // Set up global argparser
    rvvm_set_args(argc, argv);
    if (rvvm_has_arg("h") || rvvm_has_arg("help") || rvvm_has_arg("H")) {
        print_help();
        return 0;
    }

    // Parse initial machine options
    if (rvvm_getarg_size("m"))   mem = rvvm_getarg_size("m");
    if (rvvm_getarg_size("mem")) mem = rvvm_getarg_size("mem");
    if (rvvm_getarg_int("s"))    smp = rvvm_getarg_int("s");
    if (rvvm_getarg_int("smp"))  smp = rvvm_getarg_int("smp");
    rv64 = !rvvm_has_arg("rv32");

    for (int i=1; i<argc; i+=arg_size) {
        arg_size = get_arg(argv + i, &arg_name, &arg_val);
        if (cmp_arg(arg_name, "bootrom") || cmp_arg(arg_name, "bios")) {
            bootrom = arg_val;
        }
    }
    if (bootrom == NULL) {
        printf("Usage: rvvm [bootrom] [-mem 256M] [-k kernel] [-help] ...\n");
        return 0;
    }

    // Create & configure machine
    rvvm_machine_t* machine = rvvm_create_machine(RVVM_DEFAULT_MEMBASE, mem, smp, rv64);
    if (machine == NULL) {
        rvvm_error("Failed to create VM");
        return -1;
    }
    clint_init_auto(machine);
    plic_init_auto(machine);
    pci_bus_init_auto(machine);
    i2c_oc_init_auto(machine);

    rtc_goldfish_init_auto(machine);
    syscon_init_auto(machine);
    if (!rvvm_has_arg("serial")) ns16550a_init_term_auto(machine);
    if (!rvvm_has_arg("nogui") && !rvvm_has_arg("res")) gui_window_init_auto(machine, 640, 480);
#ifdef USE_NET
    if (!rvvm_has_arg("nonet")) {
        tap = tap_open();
        rtl8169_init(rvvm_get_pci_bus(machine), tap);
    }
#endif

    if (rvvm_cli_configure(machine, argc, argv, bootrom, tap)) {
        rvvm_start_machine(machine);

        if (!rvvm_has_arg("noisolation")) {
            // Preparations are done, isolate the process as much as possible
            rvvm_restrict_process();
        }

        // Returns on machine shutdown
        rvvm_run_eventloop();
    } else {
        rvvm_error("Failed to initialize VM");
    }
    rvvm_free_machine(machine);
    return 0;
}

static int rvvm_main(int argc, char** argv)
{
    if (argc >= 3 && rvvm_strcmp(argv[1], "-user")) {
        return rvvm_user_linux(argc - 2, argv + 2, NULL);
    }
    return rvvm_cli_main(argc, (const char**)argv);
}

int main(int argc, char** argv)
{
#if defined(_WIN32) && !defined(UNDER_CE)
    HWND (__stdcall *get_console_window)(void) = dlib_get_symbol("kernel32.dll", "GetConsoleWindow");
    if (get_console_window) {
        HWND console = get_console_window();
        DWORD pid = 0;
        GetWindowThreadProcessId(console, &pid);
        if (GetCurrentProcessId() == pid) {
            // If we don't have a parent terminal, destroy our console
            FreeConsole();
        }
    }
    // Use UTF-8 arguments
    LPWSTR* argv_u16 = CommandLineToArgvW(GetCommandLineW(), &argc);
    argv = safe_new_arr(char*, argc + 1);
    for (int i=0; i<argc; ++i) {
        size_t arg_len = WideCharToMultiByte(CP_UTF8, 0, argv_u16[i], -1, NULL, 0, NULL, NULL);
        argv[i] = safe_new_arr(char, arg_len);
        WideCharToMultiByte(CP_UTF8, 0, argv_u16[i], -1, argv[i], arg_len, NULL, NULL);
    }
#endif
    int ret = rvvm_main(argc, argv);
#if defined(_WIN32) && !defined(UNDER_CE)
    for (int i=0; i<argc; ++i) free(argv[i]);
    free(argv);
#endif
    return ret;
}
