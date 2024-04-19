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
#include "utils.h"
#include "vector.h"
#include "mem_ops.h"

#define FDT_MAGIC        0xD00DFEED
#define FDT_VERSION      17
#define FDT_COMP_VERSION 16

#define FDT_BEGIN_NODE 1
#define FDT_END_NODE   2
#define FDT_PROP       3
#define FDT_NOP        4
#define FDT_END        9

#define FDT_HDR_SIZE 40
#define FDT_RSV_SIZE 16

struct fdt_prop {
    char* name;
    void* data;
    uint32_t len;
};

struct fdt_node {
    char* name;
    struct fdt_node* parent;
    // Used as last_phandle in root node (parent = NULL)
    uint32_t phandle;

    vector_t(struct fdt_prop) props;
    vector_t(struct fdt_node*) nodes;
};

static char* str_duplicate(const char* str)
{
    size_t size = rvvm_strlen(str) + 1;
    char* buffer = safe_malloc(size);
    memcpy(buffer, str, size);
    return buffer;
}

static size_t fdt_name_with_addr(char* buffer, size_t size, const char* name, uint64_t addr)
{
    size_t len = rvvm_strlcpy(buffer, name, size);
    len += rvvm_strlcpy(buffer + len, "@", size - len);
    len += uint_to_str_base(buffer + len, size, addr, 16);
    return len;
}

struct fdt_node* fdt_node_create(const char* name)
{
    struct fdt_node* node = safe_new_obj(struct fdt_node);
    node->name = name ? str_duplicate(name) : NULL;
    return node;
}

struct fdt_node* fdt_node_create_reg(const char* name, uint64_t addr)
{
    char buffer[256];
    fdt_name_with_addr(buffer, sizeof(buffer), name, addr);
    return fdt_node_create(buffer);
}

void fdt_node_add_child(struct fdt_node* node, struct fdt_node* child)
{
    if (node == NULL || child == NULL) return;
    child->parent = node;
    vector_push_back(node->nodes, child);
}

struct fdt_node* fdt_node_find(struct fdt_node* node, const char* name)
{
    if (node) vector_foreach_back(node->nodes, i) {
        struct fdt_node* child = vector_at(node->nodes, i);
        if (rvvm_strcmp(child->name, name)) return child;
    }
    return NULL;
}

struct fdt_node* fdt_node_find_reg(struct fdt_node* node, const char* name, uint64_t addr)
{
    char buffer[256];
    fdt_name_with_addr(buffer, sizeof(buffer), name, addr);
    return fdt_node_find(node, buffer);
}

struct fdt_node* fdt_node_find_reg_any(struct fdt_node* node, const char* name)
{
    char buffer[256] = {0};
    size_t len = rvvm_strlcpy(buffer, name, sizeof(buffer));
    rvvm_strlcpy(buffer + len, "@", sizeof(buffer) - len);
    if (node) vector_foreach_back(node->nodes, i) {
        struct fdt_node* child = vector_at(node->nodes, i);
        if (rvvm_strfind(child->name, buffer) == child->name) return child;
    }
    return NULL;
}

static inline bool fdt_is_illegal_phandle(uint32_t phandle)
{
    return phandle == 0 || phandle == 0xffffffff;
}

static uint32_t fdt_get_new_phandle(struct fdt_node* node)
{
    // Trace to root node
    const char* orig_node_name = node->name;
    while (node->parent) node = node->parent;
    if (node->name) rvvm_warn("fdt_node_get_phandle(%s): Invalid hierarchy", orig_node_name);
    node->phandle++;
    return node->phandle;
}

uint32_t fdt_node_get_phandle(struct fdt_node* node)
{
    if (node == NULL || node->name == NULL) {
        // This is a root node, no handle needed
        return 0;
    }
    if (fdt_is_illegal_phandle(node->phandle)) {
        // Allocate new phandle
        node->phandle = fdt_get_new_phandle(node);
        fdt_node_add_prop_u32(node, "phandle", node->phandle);
    }
    return node->phandle;
}

void fdt_node_add_prop(struct fdt_node* node, const char* name, const void* data, uint32_t len)
{
    if (node) {
        void* new_data = len ? safe_malloc(len) : NULL;
        if (new_data) memcpy(new_data, data, len);
        struct fdt_prop prop = {
            .name = str_duplicate(name),
            .data = new_data,
            .len = len,
        };
        vector_push_back(node->props, prop);
    }
}

void fdt_node_add_prop_u32(struct fdt_node* node, const char* name, uint32_t val)
{
    write_uint32_be_m(&val, val);
    fdt_node_add_prop(node, name, &val, sizeof(val));
}

void fdt_node_add_prop_u64(struct fdt_node *node, const char* name, uint64_t val)
{
    write_uint64_be_m(&val, val);
    fdt_node_add_prop(node, name, &val, sizeof(val));
}

void fdt_node_add_prop_cells(struct fdt_node* node, const char* name, uint32_t* cells, uint32_t count)
{
    uint32_t* buffer = safe_new_arr(uint32_t, count);
    for (uint32_t i=0; i<count; ++i) {
        write_uint32_be_m(&buffer[i], cells[i]);
    }
    fdt_node_add_prop(node, name, buffer, count * sizeof(uint32_t));
    free(buffer);
}

void fdt_node_add_prop_str(struct fdt_node* node, const char* name, const char* val)
{
    fdt_node_add_prop(node, name, val, rvvm_strlen(val) + 1);
}

void fdt_node_add_prop_reg(struct fdt_node *node, const char *name, uint64_t begin, uint64_t size)
{
    uint64_t arr[2] = {0};
    write_uint64_be_m(&arr[0], begin);
    write_uint64_be_m(&arr[1], size);
    fdt_node_add_prop(node, name, arr, sizeof(arr));
}

bool fdt_node_del_prop(struct fdt_node* node, const char* name)
{
    vector_foreach_back(node->props, i) {
        struct fdt_prop* prop = &vector_at(node->props, i);
        if (rvvm_strcmp(prop->name, name)) {
            free(prop->name);
            free(prop->data);
            vector_erase(node->props, i);
            return true;
        }
    }
    return false;
}

void fdt_node_free(struct fdt_node* node)
{
    if (node) {
        vector_foreach_back(node->props, i) {
            free(vector_at(node->props, i).name);
            free(vector_at(node->props, i).data);
        }
        vector_foreach_back(node->nodes, i) {
            fdt_node_free(vector_at(node->nodes, i));
        }
        vector_free(node->props);
        vector_free(node->nodes);
        free(node->name);
        free(node);
    }
}

struct fdt_size_desc {
    uint32_t struct_size;
    uint32_t string_size;
};

static void fdt_get_tree_size(struct fdt_node* node, struct fdt_size_desc* desc)
{
    size_t name_len = align_size_up(node->name ? rvvm_strlen(node->name) + 1 : 1, sizeof(uint32_t));
    desc->struct_size += sizeof(uint32_t) + name_len; // FDT_BEGIN_NODE, name

    vector_foreach(node->props, i) {
        struct fdt_prop* prop = &vector_at(node->props, i);
        desc->struct_size += sizeof(uint32_t) * 3; // FDT_PROP, struct fdt_prop_desc
        desc->struct_size += align_size_up(prop->len, sizeof(uint32_t));
        desc->string_size += align_size_up(rvvm_strlen(prop->name) + 1, sizeof(uint32_t));
    }

    vector_foreach(node->nodes, i) {
        struct fdt_node* child = vector_at(node->nodes, i);
        fdt_get_tree_size(child, desc);
    }

    desc->struct_size += sizeof(uint32_t); // FDT_END_NODE
}

struct fdt_serializer_ctx {
    char* buf;
    uint32_t struct_off;
    uint32_t strings_begin;
    uint32_t strings_off;
    uint32_t reserve_off;
};

static void fdt_serialize_u32(struct fdt_serializer_ctx* ctx, uint32_t value)
{
    write_uint32_be_m(ctx->buf + ctx->struct_off, value);
    ctx->struct_off += sizeof(uint32_t);
}

static void fdt_serialize_string(struct fdt_serializer_ctx* ctx, const char* str)
{
    if (!str) str = "";
    rvvm_strlcpy(ctx->buf + ctx->struct_off, str, -1);
    ctx->struct_off = align_size_up(ctx->struct_off + rvvm_strlen(str) + 1, sizeof(uint32_t));
}

static void fdt_serialize_data(struct fdt_serializer_ctx* ctx, const char* data, uint32_t len)
{
    memcpy(ctx->buf + ctx->struct_off, data, len);
    ctx->struct_off = align_size_up(ctx->struct_off + len, sizeof(uint32_t));
}

static void fdt_serialize_name(struct fdt_serializer_ctx* ctx, const char* str)
{
    if (!str) str = "";
    rvvm_strlcpy(ctx->buf + ctx->strings_off, str, -1);
    ctx->strings_off = align_size_up(ctx->strings_off + rvvm_strlen(str) + 1, sizeof(uint32_t));
}

static void fdt_serialize_tree(struct fdt_serializer_ctx *ctx, struct fdt_node *node)
{
    fdt_serialize_u32(ctx, FDT_BEGIN_NODE);
    fdt_serialize_string(ctx, node->name);

    vector_foreach(node->props, i) {
        struct fdt_prop* prop = &vector_at(node->props, i);
        fdt_serialize_u32(ctx, FDT_PROP);

        // struct fdt_prop_desc
        fdt_serialize_u32(ctx, prop->len);
        fdt_serialize_u32(ctx, ctx->strings_off - ctx->strings_begin);

        fdt_serialize_data(ctx, prop->data, prop->len);

        fdt_serialize_name(ctx, prop->name);
    }

    vector_foreach(node->nodes, i) {
        struct fdt_node* child = vector_at(node->nodes, i);
        fdt_serialize_tree(ctx, child);
    }

    fdt_serialize_u32(ctx, FDT_END_NODE);
}

size_t fdt_size(struct fdt_node* node)
{
    return fdt_serialize(node, NULL, 0, 0);
}

size_t fdt_serialize(struct fdt_node* node, void* buffer, size_t size, uint32_t boot_cpuid)
{
    if (node == NULL) return 0;
    struct fdt_size_desc size_desc = {0};
    fdt_get_tree_size(node, &size_desc);
    size_desc.struct_size += sizeof(uint32_t); // FDT_END

    struct fdt_serializer_ctx ctx = {0};
    uint32_t buf_size = FDT_HDR_SIZE + FDT_RSV_SIZE + size_desc.struct_size;
    ctx.reserve_off = FDT_HDR_SIZE;
    ctx.struct_off = FDT_HDR_SIZE + FDT_RSV_SIZE;
    ctx.strings_begin = buf_size;
    ctx.strings_off = ctx.strings_begin;
    buf_size += size_desc.string_size;

    if (buffer) {
        if (buf_size > size) return 0;

        memset(buffer, 0, buf_size);
        ctx.buf = buffer;
        write_uint32_be_m(ctx.buf, FDT_MAGIC);
        write_uint32_be_m(ctx.buf + 4,  buf_size);
        write_uint32_be_m(ctx.buf + 8,  ctx.struct_off);
        write_uint32_be_m(ctx.buf + 12, ctx.strings_off);
        write_uint32_be_m(ctx.buf + 16, ctx.reserve_off);
        write_uint32_be_m(ctx.buf + 20, FDT_VERSION);
        write_uint32_be_m(ctx.buf + 24, FDT_COMP_VERSION);
        write_uint32_be_m(ctx.buf + 28, boot_cpuid);
        write_uint32_be_m(ctx.buf + 32, size_desc.string_size);
        write_uint32_be_m(ctx.buf + 36, size_desc.struct_size);

        fdt_serialize_tree(&ctx, node);
        fdt_serialize_u32(&ctx, FDT_END);
    }

    return buf_size;
}
