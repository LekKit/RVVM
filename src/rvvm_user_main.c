/*
rvvm_user_main.c - RVVM Linux userland emulator entry point
Thin wrapper around rvvm_user_linux() from core/rvvm_user.c

Usage: rvvm_user <guest_elf> [guest args...]
*/

#include <stdio.h>
#include "core/rvvm_user.h"

int main(int argc, char** argv, char** envp)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <guest_elf> [guest args...]\n", argv[0]);
        return 1;
    }

    // argv[1..] becomes the guest argv (argv[0] = guest ELF path)
    return rvvm_user_linux(argc - 1, argv + 1, envp);
}
