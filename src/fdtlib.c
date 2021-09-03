/*
fdtlib.c - Flattened Device Tree Library
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

#include "fdtlib.h"
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#ifdef FDTLIB_STANDALONE
// cerg2010 wanted this to be usable out-of-tree
#include <stdlib.h>
#define safe_malloc malloc
#define safe_calloc calloc
#else
#include "utils.h"
#endif

#define FDT_MAGIC 0xd00dfeed
#define FDT_VERSION 17
#define FDT_COMP_VERSION 16

struct fdt_header
{
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

struct fdt_reserve_entry {
    uint64_t address;
    uint64_t size;
};

enum fdt_node_tokens
{
    FDT_BEGIN_NODE = 1,
    FDT_END_NODE,
    FDT_PROP,
    FDT_NOP,
    FDT_END = 9
};

/* all values are stored in big-endian format */
static inline uint32_t fdt_host2u32(uint32_t value)
{
    const uint8_t* arr = (const uint8_t*)&value;
    return ((uint32_t)arr[0] << 24)
        | ((uint32_t)arr[1] << 16)
        | ((uint32_t)arr[2] << 8)
        | ((uint32_t)arr[3]);
}

static inline uint64_t fdt_host2u64(uint64_t value)
{
    const uint8_t* arr = (const uint8_t*)&value;
    return ((uint64_t)arr[0] << 56)
        | ((uint64_t)arr[1] << 48)
        | ((uint64_t)arr[2] << 40)
        | ((uint64_t)arr[3] << 32)
        | ((uint64_t)arr[4] << 24)
        | ((uint64_t)arr[5] << 16)
        | ((uint64_t)arr[6] << 8)
        | ((uint64_t)arr[7]);
}

static char* str_duplicate(const char* str)
{
    char* buffer = safe_malloc(strlen(str) + 1);
    strcpy(buffer, str);
    return buffer;
}

void fdt_node_add_prop(struct fdt_node *node, const char *name, const void *data, uint32_t len)
{
    assert(name != NULL);

    struct fdt_prop_list *entry = safe_calloc(sizeof(struct fdt_prop_list), 1);
    entry->prop.name = str_duplicate(name);
    char *new_data = NULL;
    if (data && len) {
        new_data = safe_calloc(len, 1);
        memcpy(new_data, data, len);
    }
    entry->prop.data = new_data;
    entry->prop.len = len;
    entry->next = NULL;
    struct fdt_prop_list** last = &node->props;
    while ((*last)) last = &(*last)->next;
    *last = entry;
}

void fdt_node_add_prop_u32(struct fdt_node *node, const char *name, uint32_t val)
{
    val = fdt_host2u32(val);
    fdt_node_add_prop(node, name, &val, sizeof(val));
}

void fdt_node_add_prop_cells(struct fdt_node *node, const char *name, uint32_t* cells, uint32_t count)
{
    uint32_t* buf = safe_calloc(sizeof(uint32_t), count);
    for (uint32_t i=0; i<count; ++i) {
        buf[i] = fdt_host2u32(cells[i]);
    }
    fdt_node_add_prop(node, name, buf, count*sizeof(uint32_t));
    free(buf);
}

void fdt_node_add_prop_str(struct fdt_node *node, const char *name, const char* val)
{
    fdt_node_add_prop(node, name, val, strlen(val) + 1);
}

void fdt_node_add_prop_reg(struct fdt_node *node, const char *name, uint64_t begin, uint64_t size)
{
    uint64_t arr[2];
    arr[0] = fdt_host2u64(begin);
    arr[1] = fdt_host2u64(size);
    fdt_node_add_prop(node, name, arr, sizeof(arr));
}

static inline bool fdt_is_illegal_phandle(uint32_t phandle)
{
    return phandle == 0 || phandle == 0xffffffff;
}

static uint32_t fdt_get_new_phandle(struct fdt_node *node)
{
    // Trace to root node
    while (node->parent) node = node->parent;
    node->phandle++;
    return node->phandle;
}

uint32_t fdt_node_get_phandle(struct fdt_node *node)
{
    if (node->parent == NULL) {
        // This is a root node, no handle needed
        return 0;
    }

    if (fdt_is_illegal_phandle(node->phandle)) {
        node->phandle = fdt_get_new_phandle(node);
        fdt_node_add_prop_u32(node, "phandle", node->phandle);
    }

    return node->phandle;
}

static void fdt_name_with_addr(char* buffer, size_t size, const char *name, uint64_t addr)
{
    const char* const lut = "0123456789abcdef";
    size_t cur = 0;
    size_t addr_len = 16;
    while(name[cur] && size > (cur + 17)) {
        buffer[cur] = name[cur];
        cur++;
    }
    buffer[cur++] = '@';
    while (addr_len > 1 && (addr >> 60) == 0) {
        addr <<= 4;
        addr_len--;
    }
    for (size_t i=0; i<addr_len; ++i) {
        buffer[cur++] = lut[(addr >> (60 - (i * 4))) & 0xf];
    }
    buffer[cur] = 0;
}

struct fdt_node* fdt_node_find(struct fdt_node *node, const char *name)
{
    struct fdt_node_list* list = node->nodes;
    while (list) {
        if (strcmp(list->node->name, name) == 0) return list->node;
        list = list->next;
    }
    return NULL;
}

struct fdt_node* fdt_node_find_reg(struct fdt_node *node, const char *name, uint64_t addr)
{
    char buffer[256];
    fdt_name_with_addr(buffer, 256, name, addr);
    return fdt_node_find(node, buffer);
}

struct fdt_node* fdt_node_find_reg_any(struct fdt_node *node, const char *name)
{
    char buffer[256] = {0};
    strncpy(buffer, name, 254);
    size_t len = strlen(buffer);
    buffer[len++] = '@';
    struct fdt_node_list* list = node->nodes;
    while (list) {
        if (strncmp(list->node->name, buffer, len) == 0) return list->node;
        list = list->next;
    }
    return NULL;
}

struct fdt_node* fdt_node_create(const char *name)
{
    struct fdt_node* node = safe_calloc(sizeof(struct fdt_node), 1);
    node->name = name ? str_duplicate(name) : NULL;
    node->parent = NULL;
    node->phandle = 0;
    node->nodes = NULL;
    node->props = NULL;
    return node;
}

struct fdt_node* fdt_node_create_reg(const char *name, uint64_t addr)
{
    char buffer[256];
    fdt_name_with_addr(buffer, 256, name, addr);
    return fdt_node_create(buffer);
}

void fdt_node_add_child(struct fdt_node *node, struct fdt_node *child)
{
    assert(node != NULL && child != NULL);

    struct fdt_node_list *entry = safe_calloc(sizeof(struct fdt_node_list), 1);
    child->parent = node;
    entry->node = child;
    entry->next = NULL;
    
    struct fdt_node_list** last = &node->nodes;
    while ((*last)) last = &(*last)->next;
    *last = entry;
}

void fdt_node_free(struct fdt_node *node)
{
    free(node->name);

    for (struct fdt_prop_list *entry = node->props, *entry_next = NULL;
            entry != NULL;
            entry = entry_next)
    {
        entry_next = entry->next;

        free(entry->prop.name);
        free(entry->prop.data);
        free(entry);
    }

    for (struct fdt_node_list *entry = node->nodes, *entry_next = NULL;
            entry != NULL;
            entry = entry_next)
    {
        entry_next = entry->next;

        assert(entry->node);
        fdt_node_free(entry->node);
        free(entry);
    }

    free(node);
}

struct fdt_size_desc
{
    size_t struct_size;
    size_t strings_size;
};

//#define ALIGN_UP(n, align) ((n) % (align) ? (n) + (align) - (n) % (align) : (n))
#define ALIGN_UP(n, align) (((n) + (align) - 1) & ~((align) - 1))

static void fdt_get_tree_size(struct fdt_node *node, struct fdt_size_desc *desc)
{
    assert(node != NULL && desc != NULL);

    desc->struct_size += sizeof(uint32_t); // FDT_BEGIN_NODE
    size_t name_len = node->name ? strlen(node->name) + 1 : 1;
    desc->struct_size += ALIGN_UP(name_len, sizeof(uint32_t));

    for (struct fdt_prop_list *entry = node->props;
            entry != NULL;
            entry = entry->next)
    {
        desc->struct_size += sizeof(uint32_t); // FDT_PROP
        desc->struct_size += sizeof(uint32_t) * 2; // struct fdt_prop_desc
        desc->struct_size += ALIGN_UP(entry->prop.len, sizeof(uint32_t));
        name_len = strlen(entry->prop.name) + 1;
        desc->strings_size += ALIGN_UP(name_len, sizeof(uint32_t));
    }

    for (struct fdt_node_list *entry = node->nodes;
            entry != NULL;
            entry = entry->next)
    {
        fdt_get_tree_size(entry->node, desc);
    }

    desc->struct_size += sizeof(uint32_t); // FDT_END_NODE
}

struct fdt_serializer_ctx
{
    char *buf;
    uint32_t struct_off;
    uint32_t strings_begin;
    uint32_t strings_off;
    uint32_t reserve_off;
};

static void fdt_serialize_u32(struct fdt_serializer_ctx *ctx, uint32_t value)
{
    value = fdt_host2u32(value);
    memcpy(ctx->buf + ctx->struct_off, &value, sizeof(value));
    ctx->struct_off += sizeof(value);
}

static void fdt_serialize_string(struct fdt_serializer_ctx *ctx, const char *str)
{
    for (str = str ? str : ""; *str != '\0'; ++str)
    {
        *(ctx->buf + ctx->struct_off++) = *str;
    }

    *(ctx->buf + ctx->struct_off++) = '\0';
    ctx->struct_off = ALIGN_UP(ctx->struct_off, sizeof(uint32_t));
}

static void fdt_serialize_data(struct fdt_serializer_ctx *ctx, const char *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; ++i)
    {
        *(ctx->buf + ctx->struct_off++) = *(data + i);
    }
    ctx->struct_off = ALIGN_UP(ctx->struct_off, sizeof(uint32_t));
}

static void fdt_serialize_name(struct fdt_serializer_ctx *ctx, const char *str)
{
    for (str = str ? str : ""; *str != '\0'; ++str)
    {
        *(ctx->buf + ctx->strings_off++) = *str;
    }

    *(ctx->buf + ctx->strings_off++) = '\0';
    ctx->strings_off = ALIGN_UP(ctx->strings_off, sizeof(uint32_t));
}

static void fdt_serialize_tree(struct fdt_serializer_ctx *ctx, struct fdt_node *node)
{
    assert(ctx != NULL && node != NULL);

    fdt_serialize_u32(ctx, FDT_BEGIN_NODE);
    fdt_serialize_string(ctx, node->name);

    for (struct fdt_prop_list *entry = node->props;
            entry != NULL;
            entry = entry->next)
    {
        fdt_serialize_u32(ctx, FDT_PROP);

        // struct fdt_prop_desc
        fdt_serialize_u32(ctx, entry->prop.len);
        fdt_serialize_u32(ctx, ctx->strings_off - ctx->strings_begin);

        fdt_serialize_data(ctx, entry->prop.data, entry->prop.len);

        fdt_serialize_name(ctx, entry->prop.name);
    }

    for (struct fdt_node_list *entry = node->nodes;
            entry != NULL;
            entry = entry->next)
    {
        fdt_serialize_tree(ctx, entry->node);
    }

    fdt_serialize_u32(ctx, FDT_END_NODE);
}

size_t fdt_serialize(struct fdt_node *node, void* buffer, size_t size, uint32_t boot_cpuid)
{
    struct fdt_size_desc size_desc = { 0 };
    fdt_get_tree_size(node, &size_desc);
    size_desc.struct_size += sizeof(uint32_t); // FDT_END

    struct fdt_serializer_ctx ctx;

    uint32_t buf_size = 0;
    ctx.reserve_off = buf_size = ALIGN_UP(buf_size + sizeof(struct fdt_header), 8);
    /* only one sentinel reservation entry with zero values */
    ctx.struct_off = buf_size = ALIGN_UP(buf_size + sizeof(struct fdt_reserve_entry), 4);
    ctx.strings_off = ctx.strings_begin = buf_size += size_desc.struct_size;
    buf_size += size_desc.strings_size;
    
    if (buf_size > size) return 0;

    ctx.buf = buffer;
    struct fdt_header *hdr = (struct fdt_header *) ctx.buf;
    hdr->magic = fdt_host2u32(FDT_MAGIC);
    hdr->version = fdt_host2u32(FDT_VERSION);
    hdr->last_comp_version = fdt_host2u32(FDT_COMP_VERSION);
    hdr->boot_cpuid_phys = fdt_host2u32(boot_cpuid);
    hdr->off_dt_struct = fdt_host2u32(ctx.struct_off);
    hdr->off_dt_strings = fdt_host2u32(ctx.strings_off);
    hdr->off_mem_rsvmap = fdt_host2u32(ctx.reserve_off);
    hdr->size_dt_struct = fdt_host2u32(size_desc.struct_size);
    hdr->size_dt_strings = fdt_host2u32(size_desc.strings_size);
    hdr->totalsize = fdt_host2u32((uint32_t) buf_size);

    /* memory reservation block is already zero */

    fdt_serialize_tree(&ctx, node);
    fdt_serialize_u32(&ctx, FDT_END);

    return buf_size;
}

