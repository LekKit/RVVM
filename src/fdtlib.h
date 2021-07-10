#ifndef FDTLIB_H
#define FDTLIB_H

#include "riscv.h"

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

/* all values are stored in big-endian format */
#ifdef HOST_LITTLE_ENDIAN
static uint32_t fdt_host2u32(uint32_t value)
{
    const uint8_t* arr = (const uint8_t*)&value;
    return ((uint32_t)arr[0] << 24)
        | ((uint32_t)arr[1] << 16)
        | ((uint32_t)arr[2] << 8)
        | ((uint32_t)arr[3]);
}

static uint64_t fdt_host2u64(uint64_t value)
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
#else
#define fdt_host2u32(val) (val)
#define fdt_host2u64(val) (val)
#endif

void fdt_node_add_prop(struct fdt_node *node, const char *name, void *data, uint32_t len);
uint32_t fdt_node_get_phandle(struct fdt_context *ctx, struct fdt_node *node);
struct fdt_node* fdt_node_create(const char *name);
void fdt_node_add_child(struct fdt_node *node, struct fdt_node *child);
void fdt_node_free(struct fdt_node *node);
void* fdt_serialize(struct fdt_node *node, uint32_t boot_cpuid, uint32_t *size);

#endif
