#include "riscv.h"
#include "fdtlib.h"
#include <stdio.h>
#include <string.h>

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

void fdt_node_add_prop(struct fdt_node *node, const char *name, void *data, uint32_t len)
{
    assert(name != NULL && data != NULL && len > 0);

    struct fdt_prop_list *entry = malloc(sizeof(struct fdt_prop_list));
    entry->prop.name = strdup(name);
    char *new_data = malloc(len);
    memcpy(new_data, data, len);
    entry->prop.data = new_data;
    entry->prop.len = len;
    entry->next = node->props;
    node->props = entry;
}

static inline bool fdt_is_illegal_phandle(uint32_t phandle)
{
    return phandle == 0 || phandle == 0xffffffff;
}

uint32_t fdt_node_get_phandle(struct fdt_context *ctx, struct fdt_node *node)
{
    if (fdt_is_illegal_phandle(node->phandle))
    {
        node->phandle = ++ctx->last_phandle;

        uint32_t phandle_value = fdt_host2u32(node->phandle);
        fdt_node_add_prop(node, "phandle", &phandle_value, sizeof(phandle_value));
    }

    return node->phandle;
}

struct fdt_node* fdt_node_create(const char *name)
{
    struct fdt_node* node = malloc(sizeof(struct fdt_node));
    node->name = name ? strdup(name) : NULL;
    node->parent = NULL;
    node->phandle = 0;
    node->nodes = NULL;
    node->props = NULL;
    return node;
}

void fdt_node_add_child(struct fdt_node *node, struct fdt_node *child)
{
    assert(node != NULL && child != NULL);

    struct fdt_node_list *entry = malloc(sizeof(struct fdt_node_list));
    entry->node = child;
    entry->next = node->nodes;
    node->nodes = entry;
    child->parent = node;
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
        name_len = strlen(entry->prop.name);
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

void* fdt_serialize(struct fdt_node *node, uint32_t boot_cpuid, uint32_t *size)
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

    ctx.buf = calloc(1, buf_size);
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

    if (size != NULL)
    {
        *size = buf_size;
    }
    return ctx.buf;
}

