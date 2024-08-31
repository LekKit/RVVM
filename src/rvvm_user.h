/*
rvvm_user.h - RVVM Linux binary emulator
Copyright (C) 2024  LekKit <github.com/LekKit>

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

#ifndef RVVM_USER_H
#define RVVM_USER_H

// Just call this like main(), envp may be NULL
int rvvm_user_linux(int argc, char** argv, char** envp);

#endif
