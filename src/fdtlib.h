/*
fdtlib.h - Flattened Device Tree Library
Copyright (C) 2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>
                    LekKit <github.com/LekKit>

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

#ifndef FDTLIB_H
#define FDTLIB_H

#include <stdint.h>
#include <stddef.h>

struct fdt_context
{
    uint32_t last_phandle;
};

struct fdt_prop
{
    char *name;
    char *data;
    uint32_t len;
};

struct fdt_prop_list
{
    struct fdt_prop prop;
    struct fdt_prop_list *next;
};

struct fdt_node_list;

struct fdt_node
{
    char *name;
    struct fdt_node *parent;
    uint32_t phandle;

    struct fdt_prop_list *props;
    struct fdt_node_list *nodes;
};

struct fdt_node_list
{
    struct fdt_node *node;
    struct fdt_node_list *next;
};


struct fdt_node* fdt_node_create(const char *name);
void fdt_node_add_prop(struct fdt_node *node, const char *name, const void *data, uint32_t len);
void fdt_node_add_prop_u32(struct fdt_node *node, const char *name, uint32_t val);
void fdt_node_add_prop_str(struct fdt_node *node, const char *name, const char* val);
void fdt_node_add_prop_reg(struct fdt_node *node, const char *name, uint64_t begin, uint64_t size);
uint32_t fdt_node_get_phandle(struct fdt_context *ctx, struct fdt_node *node);
void fdt_node_add_child(struct fdt_node *node, struct fdt_node *child);
void fdt_node_free(struct fdt_node *node);
size_t fdt_serialize(struct fdt_node *node, void* buffer, size_t size, uint32_t boot_cpuid);

#endif
