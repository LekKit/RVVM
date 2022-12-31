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

#ifndef FDTLIB_STANDALONE
# include "rvvmlib.h"
#else
# define PUBLIC
#endif

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
    // Used as last_phandle in root node (parent = NULL)
    uint32_t phandle;

    struct fdt_prop_list *props;
    struct fdt_node_list *nodes;
};

struct fdt_node_list
{
    struct fdt_node *node;
    struct fdt_node_list *next;
};

// Create a fdt node, root node should have name = NULL
PUBLIC struct fdt_node* fdt_node_create(const char *name);

// Create a fdt node with address, like device@10000
PUBLIC struct fdt_node* fdt_node_create_reg(const char *name, uint64_t addr);

// Add arbitary byte buffer property
PUBLIC void fdt_node_add_prop(struct fdt_node *node, const char *name, const void *data, uint32_t len);

// Add single-cell property
PUBLIC void fdt_node_add_prop_u32(struct fdt_node *node, const char *name, uint32_t val);

// Add double-cell property
PUBLIC void fdt_node_add_prop_u64(struct fdt_node *node, const char *name, uint64_t val);

// Add multi-cell property
PUBLIC void fdt_node_add_prop_cells(struct fdt_node *node, const char *name, uint32_t* cells, uint32_t count);

// Add string property
PUBLIC void fdt_node_add_prop_str(struct fdt_node *node, const char *name, const char* val);

// Add register range property (addr cells: 2, size cells: 2)
PUBLIC void fdt_node_add_prop_reg(struct fdt_node *node, const char *name, uint64_t begin, uint64_t size);

// Get child node phandle (allocates phandles transparently)
PUBLIC uint32_t fdt_node_get_phandle(struct fdt_node *node);

// Lookup for child node by name (returns NULL on failure)
PUBLIC struct fdt_node* fdt_node_find(struct fdt_node *node, const char *name);

// Lookup for child node by name + addr, like device@10000 (returns NULL on failure)
PUBLIC struct fdt_node* fdt_node_find_reg(struct fdt_node *node, const char *name, uint64_t addr);

// Lookup for any reg child node by name, like device@* (returns NULL on failure)
PUBLIC struct fdt_node* fdt_node_find_reg_any(struct fdt_node *node, const char *name);

// Add child node
PUBLIC void fdt_node_add_child(struct fdt_node *node, struct fdt_node *child);

// Recursively free a node and it's child nodes
PUBLIC void fdt_node_free(struct fdt_node *node);

// Returns required buffer size for serializing
PUBLIC size_t fdt_size(struct fdt_node *node);

// Serialize DTB into buffer, returns 0 when there's insufficient space
// Returns required buffer size when buffer == NULL
PUBLIC size_t fdt_serialize(struct fdt_node *node, void* buffer, size_t size, uint32_t boot_cpuid);

#endif
