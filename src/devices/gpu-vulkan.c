/*
gpu-vulkan.c - Vulkan GPU backend
Copyright (C) 2026  David Korenchuk <github.com/epoll-reactor-2>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifdef USE_GPU

#include "gpu-vulkan.h"
#include <rvvm/rvvm_fb.h>
#include <util/atomics.h>
#include <util/compiler.h>
#include <util/spinlock.h>
#include <util/threading.h>
#include <util/utils.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

static const char* gpu_vulkan_result_to_string(VkResult result)
{
    switch (result) {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_NOT_READY:
            return "VK_NOT_READY";
        case VK_TIMEOUT:
            return "VK_TIMEOUT";
        case VK_EVENT_SET:
            return "VK_EVENT_SET";
        case VK_EVENT_RESET:
            return "VK_EVENT_RESET";
        case VK_INCOMPLETE:
            return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:
            return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS:
            return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
            return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL:
            return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_UNKNOWN:
            return "VK_ERROR_UNKNOWN";
        case VK_ERROR_OUT_OF_POOL_MEMORY:
            return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE:
            return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        case VK_ERROR_FRAGMENTATION:
            return "VK_ERROR_FRAGMENTATION";
        case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
            return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
        case VK_PIPELINE_COMPILE_REQUIRED:
            return "VK_PIPELINE_COMPILE_REQUIRED";
        case VK_ERROR_SURFACE_LOST_KHR:
            return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
            return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR:
            return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:
            return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
            return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
        case VK_ERROR_VALIDATION_FAILED_EXT:
            return "VK_ERROR_VALIDATION_FAILED_EXT";
        case VK_ERROR_INVALID_SHADER_NV:
            return "VK_ERROR_INVALID_SHADER_NV";
        case VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR:
            return "VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR";
        case VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR:
            return "VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR";
        case VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR:
            return "VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR";
        case VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR:
            return "VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR";
        case VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR:
            return "VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR";
        case VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR:
            return "VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR";
        case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT:
            return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
        case VK_ERROR_NOT_PERMITTED_KHR:
            return "VK_ERROR_NOT_PERMITTED_KHR";
        case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
            return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
        case VK_THREAD_IDLE_KHR:
            return "VK_THREAD_IDLE_KHR";
        case VK_THREAD_DONE_KHR:
            return "VK_THREAD_DONE_KHR";
        case VK_OPERATION_DEFERRED_KHR:
            return "VK_OPERATION_DEFERRED_KHR";
        case VK_OPERATION_NOT_DEFERRED_KHR:
            return "VK_OPERATION_NOT_DEFERRED_KHR";
        case VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR:
            return "VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR";
        case VK_ERROR_COMPRESSION_EXHAUSTED_EXT:
            return "VK_ERROR_COMPRESSION_EXHAUSTED_EXT";
        case VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT:
            return "VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT";
        default:
            return "VK_UNKNOWN_RESULT";
    }
}

// Log-and-bail helper. Every Vulkan call site uses this instead of
// aborting the process - a broken GPU driver should not take the whole
// emulator down with it.
#define VK_TRY(x)                                                                                                      \
    do {                                                                                                               \
        VkResult res__ = (x);                                                                                          \
        if (res__ != VK_SUCCESS) {                                                                                     \
            rvvm_warn("%s failed (%d: %s) at %s:%d\n", #x, res__, gpu_vulkan_result_to_string(res__), __FILE__,        \
                      __LINE__);                                                                                       \
            goto fail;                                                                                                 \
        }                                                                                                              \
    } while (0)

#define GPU_VULKAN_ALL_STAGES ((1u << GPU_VULKAN_STAGE_COUNT) - 1u)

// A draw as the renderer keeps it: everything the caller passed to
// gpu_vulkan_submit_draw() except the constants (those have their own
// path, see gpu_vulkan_ctx_t), copied, so nothing here can be recompiled
// or freed underneath the render thread.
typedef struct {
    uint32_t* spirv[GPU_VULKAN_STAGE_COUNT]; // Owned.
    uint32_t  spirv_nwords[GPU_VULKAN_STAGE_COUNT];

    uint32_t topology;
    uint32_t vertex_count;
    uint32_t instance_count;
    uint32_t first_vertex;
    uint32_t first_instance;

    // Vertex input, deep-copied like spirv[] above.
    uint8_t*                   vertex_data[GPU_VULKAN_MAX_VERTEX_BINDINGS]; // Owned.
    uint32_t                   vertex_size[GPU_VULKAN_MAX_VERTEX_BINDINGS];
    uint32_t                   vertex_stride[GPU_VULKAN_MAX_VERTEX_BINDINGS];
    uint32_t                   vertex_binding_count;
    gpu_vulkan_vertex_attrib_t vertex_attrib[GPU_VULKAN_MAX_VERTEX_ATTRIBS];
    uint32_t                   vertex_attrib_count;

    uint8_t* index_data; // Owned, NULL for a non-indexed draw.
    uint32_t index_size;
    uint32_t index_type;
    int32_t  vertex_offset;
} gpu_vulkan_scene_t;

struct gpu_vulkan_ctx_t {
    VkInstance       instance;
    VkPhysicalDevice physical_device;
    VkDevice         device;
    uint32_t         graphics_family;
    VkQueue          graphics_queue;

    VkRenderPass     render_pass;
    VkPipelineLayout pipeline_layout;

    // Pipeline built for the active guest scene, plus the key it was
    // built from - shader modules and topology hashed together, so a
    // guest re-submitting the same scene does not rebuild it.
    VkPipeline guest_pipeline;
    uint64_t   guest_pipeline_key;

    // Per-stage uniform blocks holding the constants the guest pushed.
    // Persistently mapped and written straight from the active scene.
    VkDescriptorSetLayout const_set_layout;
    VkDescriptorPool      const_pool;
    VkDescriptorSet       const_set;
    VkBuffer              const_buffer[GPU_VULKAN_STAGE_COUNT];
    VkDeviceMemory        const_memory[GPU_VULKAN_STAGE_COUNT];
    void*                 const_mapped[GPU_VULKAN_STAGE_COUNT];

    // Host-side life of a constant block:
    //   device thread: submit_draw()/update_constants() -> const_shadow
    //   render thread: take_scene() snapshots dirty stages -> const_stage
    //                  upload_constants() const_stage -> const_mapped
    // const_shadow/const_dirty are guarded by draw_lock. const_stage and
    // const_upload belong to the render thread; the snapshot exists so the
    // lock is never held across writes to mapped (possibly write-combined)
    // memory. const_upload survives tasks that bail out early, so a
    // snapshot is never lost. Bit i of the masks == stage i.
    uint8_t  const_shadow[GPU_VULKAN_STAGE_COUNT][GPU_VULKAN_CONST_BYTES];
    uint32_t const_dirty;
    uint8_t  const_stage[GPU_VULKAN_STAGE_COUNT][GPU_VULKAN_CONST_BYTES];
    uint32_t const_upload;

    // Vertex/index buffers for the active scene. Render thread only,
    // (re)sized in gpu_vulkan_upload_vertex_input() right before they're
    // bound; *_cap tracks the allocated size so an equal-or-smaller next
    // draw reuses the allocation instead of recreating it every frame.
    VkBuffer       vertex_buffer[GPU_VULKAN_MAX_VERTEX_BINDINGS];
    VkDeviceMemory vertex_memory[GPU_VULKAN_MAX_VERTEX_BINDINGS];
    VkDeviceSize   vertex_buffer_cap[GPU_VULKAN_MAX_VERTEX_BINDINGS];
    VkBuffer       index_buffer;
    VkDeviceMemory index_memory;
    VkDeviceSize   index_buffer_cap;

    // Scene handoff. The device thread publishes into `pending`, the
    // render worker takes it over into `active` at the top of a task.
    spinlock_t         draw_lock;
    gpu_vulkan_scene_t pending;
    bool               pending_valid;
    gpu_vulkan_scene_t active;
    bool               active_valid;

    VkCommandPool   command_pool;
    VkCommandBuffer command_buffer;
    VkFence         fence;

    // Per-resolution resources; recreated when width/height/format change.
    // Owned exclusively by the render task thread.
    uint32_t       cur_width;
    uint32_t       cur_height;
    uint32_t       cur_stride;    // only meaningful when zero_copy
    VkFormat       cur_vk_format; // format color_image/render_pass/pipeline are built for
    VkDeviceSize   cur_buf_size;  // readback buffer's actual allocated size
    bool           zero_copy;     // true: GPU output already matches the guest's byte layout
    VkImage        color_image;
    VkDeviceMemory color_memory;
    VkImageView    color_view;
    VkFramebuffer  framebuffer;
    VkBuffer       readback_buffer;
    VkDeviceMemory readback_memory;
    void*          readback_mapped;

    uint64_t frame_index;

    // Async page-flip state. The worker does the render AND the CPU-side
    // format conversion/blit, writing straight into the guest's own vram
    // buffer. Scanout never touches a pixel - it just asks what's
    // currently resident in that buffer.
    bool     render_in_progress;
    bool     shutting_down;
    uint32_t requested_width;
    uint32_t requested_height;

    // Blit target for the *next* task to pick up - published by scanout
    // every call, read once by the worker at the top of its task. This
    // is the guest's real vram (rvvm_fbdev_get_vram()), not an internal
    // Vulkan buffer.
    uint8_t*   requested_dst;
    size_t     requested_dst_size;
    uint32_t   requested_stride;
    rvvm_rgb_t requested_format;

    // Describes whatever the worker most recently finished writing into
    // requested_dst - i.e. what's actually safe to hand to
    // rvvm_fbdev_set_scanout() right now.
    uint32_t   front_width;
    uint32_t   front_height;
    uint32_t   front_stride;
    rvvm_rgb_t front_format;
};

// -----------------------------------------------------------
// Initialization
// -----------------------------------------------------------



static bool gpu_vulkan_create_instance_and_device(gpu_vulkan_ctx_t* ctx)
{
    VkApplicationInfo app_info = {
        .sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "xe2-scanout-renderer",
        .apiVersion       = VK_API_VERSION_1_3,
    };
    // No extensions: no surface/presentation is ever used.
    VkInstanceCreateInfo inst_ci = {
        .sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
    };
    VK_TRY(vkCreateInstance(&inst_ci, NULL, &ctx->instance));

    uint32_t dev_count = 0;
    vkEnumeratePhysicalDevices(ctx->instance, &dev_count, NULL);
    if (dev_count == 0) {
        rvvm_warn("No Vulkan devices available\n");
        goto fail;
    }
    VkPhysicalDevice* devices = safe_calloc(sizeof(VkPhysicalDevice), dev_count);
    vkEnumeratePhysicalDevices(ctx->instance, &dev_count, devices);
    for (size_t i = 0; i < dev_count; ++i) {
        VkPhysicalDeviceProperties property = {0};
        vkGetPhysicalDeviceProperties(devices[i], &property);
        rvvm_info("Vulkan detected physical GPU: %s", property.deviceName);
    }

    int graphics_family = -1;
    for (uint32_t i = 0; i < dev_count && graphics_family < 0; i++) {
        uint32_t q_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &q_count, NULL);
        VkQueueFamilyProperties* q_props = safe_calloc(sizeof(VkQueueFamilyProperties), q_count);
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &q_count, q_props);
        for (uint32_t q = 0; q < q_count; q++) {
            if (q_props[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                ctx->physical_device = devices[i];
                graphics_family      = (int)q;
                break;
            }
        }
        free(q_props);
    }
    free(devices);

    if (graphics_family < 0) {
        rvvm_warn("No graphics-capable queue family found\n");
        goto fail;
    }
    ctx->graphics_family = (uint32_t)graphics_family;

    float                   priority = 1.0f;
    VkDeviceQueueCreateInfo queue_ci = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = ctx->graphics_family,
        .queueCount       = 1,
        .pQueuePriorities = &priority,
    };
    VkDeviceCreateInfo dev_ci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_ci,
        // No VK_KHR_swapchain - nothing here ever presents.
    };
    VK_TRY(vkCreateDevice(ctx->physical_device, &dev_ci, NULL, &ctx->device));
    vkGetDeviceQueue(ctx->device, ctx->graphics_family, 0, &ctx->graphics_queue);
    return true;

fail:
    return false;
}

// Maps a guest pixel format onto the Vulkan format whose memory byte
// layout matches it exactly - Vulkan's *8*8*8*8_UNORM formats list
// components in the same order as their bytes in memory, so these are
// direct matches, not approximations:
//   RVVM_RGB_XRGB8888    -> bytes R,G,B,X -> VK_FORMAT_R8G8B8A8_UNORM
//   RVVM_RGB_XBGR8888    -> bytes B,G,R,X -> VK_FORMAT_B8G8R8A8_UNORM
//   RVVM_RGB_RGB565      -> bits R:G:B = 5:6:5, MSB-first -> VK_FORMAT_R5G6B5_UNORM_PACK16
//   RVVM_RGB_XRGB2101010 -> bits X:R:G:B = 2:10:10:10     -> VK_FORMAT_A2R10G10B10_UNORM_PACK32
// Verify these against your actual rvvm_rgb_t byte-order definition -
// same caveat as the CPU conversion table below, which this mirrors.
forceinline static VkFormat gpu_vulkan_native_format_for_guest(rvvm_rgb_t format)
{
    switch (format) {
        case RVVM_RGB_RGB565:
            return VK_FORMAT_R5G6B5_UNORM_PACK16;

        case RVVM_RGB_XRGB2101010:
            return VK_FORMAT_A2R10G10B10_UNORM_PACK32;

        case RVVM_RGB_XBGR8888:
            return VK_FORMAT_B8G8R8A8_UNORM;

        case RVVM_RGB_XRGB8888:
        default:
            return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

// R8G8B8A8/B8G8R8A8 color-attachment support is mandatory in the Vulkan
// spec; R5G6B5/A2R10G10B10 are not, so this has to be a runtime check.
static bool gpu_vulkan_format_usable(VkPhysicalDevice phys, VkFormat fmt)
{
    VkFormatProperties props = {0};
    vkGetPhysicalDeviceFormatProperties(phys, fmt, &props);
    return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0;
}

static size_t gpu_vulkan_vk_format_bpp(VkFormat fmt)
{
    return (fmt == VK_FORMAT_R5G6B5_UNORM_PACK16) ? 2 : 4;
}

static bool gpu_vulkan_create_render_pass(gpu_vulkan_ctx_t* ctx, VkFormat vk_format)
{
    VkAttachmentDescription color_attachment = {
        .format         = vk_format,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    };
    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkSubpassDescription subpass = {
        .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &color_ref,
    };
    VkSubpassDependency dependency = {
        .srcSubpass    = VK_SUBPASS_EXTERNAL,
        .dstSubpass    = 0,
        .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };
    VkRenderPassCreateInfo ci = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments    = &color_attachment,
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = &dependency,
    };
    VK_TRY(vkCreateRenderPass(ctx->device, &ci, NULL, &ctx->render_pass));
    return true;
fail:
    return false;
}

static uint32_t gpu_vulkan_find_memory_type(VkPhysicalDevice phys, uint32_t type_bits, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mem_props = {0};
    vkGetPhysicalDeviceMemoryProperties(phys, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) && (mem_props.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return UINT32_MAX;
}

// (Re)creates *buf/*mem if *cap is too small, then uploads `size` bytes
// of `data` into it. Host-visible + coherent, mapped/unmapped per call:
// vertex/index data changes shape every draw, unlike the persistently
// mapped constant buffers above, so there is no steady-state mapping to
// keep around.
static bool gpu_vulkan_upload_host_buffer(gpu_vulkan_ctx_t* ctx, VkBuffer* buf, VkDeviceMemory* mem, VkDeviceSize* cap,
                                          VkBufferUsageFlags usage, const void* data, VkDeviceSize size)
{
    if (size == 0) {
        return true;
    }
    if (size > *cap) {
        if (*buf) {
            vkDestroyBuffer(ctx->device, *buf, NULL);
        }
        if (*mem) {
            vkFreeMemory(ctx->device, *mem, NULL);
        }
        *buf = VK_NULL_HANDLE;
        *mem = VK_NULL_HANDLE;
        *cap = 0;

        VkBufferCreateInfo buf_ci = {
            .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size        = size,
            .usage       = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        VK_TRY(vkCreateBuffer(ctx->device, &buf_ci, NULL, buf));

        VkMemoryRequirements req = {0};
        vkGetBufferMemoryRequirements(ctx->device, *buf, &req);
        uint32_t mem_type
            = gpu_vulkan_find_memory_type(ctx->physical_device, req.memoryTypeBits,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (mem_type == UINT32_MAX) {
            goto fail;
        }
        VkMemoryAllocateInfo alloc = {
            .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize  = req.size,
            .memoryTypeIndex = mem_type,
        };
        VK_TRY(vkAllocateMemory(ctx->device, &alloc, NULL, mem));
        VK_TRY(vkBindBufferMemory(ctx->device, *buf, *mem, 0));
        *cap = req.size;
    }

    void* mapped = NULL;
    VK_TRY(vkMapMemory(ctx->device, *mem, 0, size, 0, &mapped));
    memcpy(mapped, data, size);
    vkUnmapMemory(ctx->device, *mem);
    return true;

fail:
    return false;
}

// Uniform block layout shared by every pipeline: one binding per
// programmable stage, at the set and size gpu-vulkan.h documents. A
// cross-compiled guest shader declares exactly this, so its constant
// reads land in the buffer this backend fills.
//
// Only vertex and fragment shaders are turned into pipeline stages for
// now, so those are the stages the bindings are visible to; the bindings
// themselves all exist, keeping binding number == stage index.
static bool gpu_vulkan_create_const_layout(gpu_vulkan_ctx_t* ctx)
{
    VkDescriptorSetLayoutBinding bindings[GPU_VULKAN_STAGE_COUNT] = {0};
    for (uint32_t i = 0; i < GPU_VULKAN_STAGE_COUNT; i++) {
        bindings[i] = (VkDescriptorSetLayoutBinding) {
            .binding         = i,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    }
    VkDescriptorSetLayoutCreateInfo set_ci = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = GPU_VULKAN_STAGE_COUNT,
        .pBindings    = bindings,
    };
    VK_TRY(vkCreateDescriptorSetLayout(ctx->device, &set_ci, NULL, &ctx->const_set_layout));

    VkDescriptorPoolSize pool_size = {
        .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = GPU_VULKAN_STAGE_COUNT,
    };
    VkDescriptorPoolCreateInfo pool_ci = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = 1,
        .poolSizeCount = 1,
        .pPoolSizes    = &pool_size,
    };
    VK_TRY(vkCreateDescriptorPool(ctx->device, &pool_ci, NULL, &ctx->const_pool));

    VkDescriptorSetAllocateInfo set_alloc = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = ctx->const_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &ctx->const_set_layout,
    };
    VK_TRY(vkAllocateDescriptorSets(ctx->device, &set_alloc, &ctx->const_set));

    // One host-visible buffer per stage, mapped for the lifetime of the
    // context. Only the render thread ever writes them, between fence
    // waits, so no extra synchronization is needed.
    VkDescriptorBufferInfo buf_info[GPU_VULKAN_STAGE_COUNT] = {0};
    VkWriteDescriptorSet   writes[GPU_VULKAN_STAGE_COUNT]   = {0};
    for (uint32_t i = 0; i < GPU_VULKAN_STAGE_COUNT; i++) {
        VkBufferCreateInfo buf_ci = {
            .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size        = GPU_VULKAN_CONST_BYTES,
            .usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        VK_TRY(vkCreateBuffer(ctx->device, &buf_ci, NULL, &ctx->const_buffer[i]));

        VkMemoryRequirements req = {0};
        vkGetBufferMemoryRequirements(ctx->device, ctx->const_buffer[i], &req);
        uint32_t mem_type
            = gpu_vulkan_find_memory_type(ctx->physical_device, req.memoryTypeBits,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (mem_type == UINT32_MAX) {
            goto fail;
        }
        VkMemoryAllocateInfo alloc = {
            .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize  = req.size,
            .memoryTypeIndex = mem_type,
        };
        VK_TRY(vkAllocateMemory(ctx->device, &alloc, NULL, &ctx->const_memory[i]));
        VK_TRY(vkBindBufferMemory(ctx->device, ctx->const_buffer[i], ctx->const_memory[i], 0));
        VK_TRY(vkMapMemory(ctx->device, ctx->const_memory[i], 0, GPU_VULKAN_CONST_BYTES, 0, &ctx->const_mapped[i]));
        memset(ctx->const_mapped[i], 0, GPU_VULKAN_CONST_BYTES);

        buf_info[i] = (VkDescriptorBufferInfo) {
            .buffer = ctx->const_buffer[i],
            .offset = 0,
            .range  = GPU_VULKAN_CONST_BYTES,
        };
        writes[i] = (VkWriteDescriptorSet) {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = ctx->const_set,
            .dstBinding      = i,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo     = &buf_info[i],
        };
    }
    // The bindings never move afterwards, so this is the only update.
    vkUpdateDescriptorSets(ctx->device, GPU_VULKAN_STAGE_COUNT, writes, 0, NULL);

    // Constants only ever travel through the uniform blocks above - no
    // push constants, so every stage sees the same, size-bounded interface.
    VkPipelineLayoutCreateInfo layout_ci = {
        .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts    = &ctx->const_set_layout,
    };
    VK_TRY(vkCreatePipelineLayout(ctx->device, &layout_ci, NULL, &ctx->pipeline_layout));
    return true;

fail:
    return false;
}

// Builds a graphics pipeline around already-created shader modules.
// Vertex input matches the scene's binding strides and attribute table
// (empty when the scene has none, e.g. a VS that generates its own
// geometry from gl_VertexIndex). Everything else is fixed: dynamic
// viewport/scissor, no depth or blending.
static VkPipeline gpu_vulkan_build_pipeline(gpu_vulkan_ctx_t* ctx, const VkPipelineShaderStageCreateInfo* stages,
                                            uint32_t stage_count, uint32_t topology, const gpu_vulkan_scene_t* scene)
{
    rvvm_info("%s", __FUNCTION__);

    VkVertexInputBindingDescription binding_desc[GPU_VULKAN_MAX_VERTEX_BINDINGS] = {0};
    for (uint32_t i = 0; i < scene->vertex_binding_count; i++) {
        binding_desc[i] = (VkVertexInputBindingDescription) {
            .binding   = i,
            .stride    = scene->vertex_stride[i],
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
    }
    VkVertexInputAttributeDescription attrib_desc[GPU_VULKAN_MAX_VERTEX_ATTRIBS] = {0};
    for (uint32_t i = 0; i < scene->vertex_attrib_count; i++) {
        attrib_desc[i] = (VkVertexInputAttributeDescription) {
            .location = scene->vertex_attrib[i].location,
            .binding  = scene->vertex_attrib[i].binding,
            .format   = (VkFormat)scene->vertex_attrib[i].format,
            .offset   = scene->vertex_attrib[i].offset,
        };
    }
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = scene->vertex_binding_count,
        .pVertexBindingDescriptions      = binding_desc,
        .vertexAttributeDescriptionCount = scene->vertex_attrib_count,
        .pVertexAttributeDescriptions    = attrib_desc,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = (VkPrimitiveTopology)topology,
    };

    // Viewport/scissor are dynamic so the same pipeline serves any
    // plane resolution the guest sets up - no pipeline recreation on
    // a mode change, only the offscreen image/framebuffer.
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount  = 1,
    };
    VkDynamicState dynamic_states[2] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates    = dynamic_states,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode    = VK_CULL_MODE_NONE,
        .frontFace   = VK_FRONT_FACE_CLOCKWISE,
        .lineWidth   = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState blend_attachment = {
        .colorWriteMask
        = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo color_blending = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments    = &blend_attachment,
    };

    for (uint64_t i = 0; i < stage_count; ++i) {
        rvvm_info("%s: Stage %u, %s", __FUNCTION__, stages[i].flags, stages[i].pName);
    }

    VkGraphicsPipelineCreateInfo pipeline_ci = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount          = stage_count,
        .pStages             = stages,
        .pVertexInputState   = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState      = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState   = &multisampling,
        .pColorBlendState    = &color_blending,
        .pDynamicState       = &dynamic_state,
        .layout              = ctx->pipeline_layout,
        .renderPass          = ctx->render_pass,
        .subpass             = 0,
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_TRY(vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1, &pipeline_ci, NULL, &pipeline));
    return pipeline;

fail:
    return VK_NULL_HANDLE;
}

// Identity of a scene as far as pipeline construction is concerned: the
// shader modules, the topology, and the vertex input layout (baked into
// VkPipelineVertexInputStateCreateInfo, so a layout change needs a new
// pipeline same as new SPIR-V does). Constants and the raw vertex/index
// bytes deliberately do not take part - they change every frame and
// never invalidate a pipeline.
static uint64_t gpu_vulkan_scene_key(const gpu_vulkan_scene_t* scene)
{
    uint64_t hash = 0xCBF29CE484222325ULL; // FNV-1a
    for (uint32_t s = 0; s < GPU_VULKAN_STAGE_COUNT; s++) {
        for (uint32_t i = 0; i < scene->spirv_nwords[s]; i++) {
            hash = (hash ^ scene->spirv[s][i]) * 0x100000001B3ULL;
        }
        hash = (hash ^ scene->spirv_nwords[s]) * 0x100000001B3ULL;
    }
    for (uint32_t i = 0; i < scene->vertex_binding_count; i++) {
        hash = (hash ^ scene->vertex_stride[i]) * 0x100000001B3ULL;
    }
    for (uint32_t i = 0; i < scene->vertex_attrib_count; i++) {
        const gpu_vulkan_vertex_attrib_t* a = &scene->vertex_attrib[i];

        hash = (hash ^ a->location) * 0x100000001B3ULL;
        hash = (hash ^ a->binding) * 0x100000001B3ULL;
        hash = (hash ^ a->format) * 0x100000001B3ULL;
        hash = (hash ^ a->offset) * 0x100000001B3ULL;
    }
    return (hash ^ scene->topology) * 0x100000001B3ULL;
}

// Builds (or reuses) the pipeline for the active guest scene. Stages
// beyond vertex and fragment are skipped: turning them into pipeline
// stages needs device features this backend does not request yet.
static bool gpu_vulkan_ensure_guest_pipeline(gpu_vulkan_ctx_t* ctx)
{
    const gpu_vulkan_scene_t* scene = &ctx->active;
    uint64_t                  key   = gpu_vulkan_scene_key(scene);

    if (ctx->guest_pipeline != VK_NULL_HANDLE && ctx->guest_pipeline_key == key) {
        return true;
    }

    static const VkShaderStageFlagBits stage_bits[GPU_VULKAN_STAGE_COUNT] = {
        [GPU_VULKAN_STAGE_VERTEX]   = VK_SHADER_STAGE_VERTEX_BIT,
        [GPU_VULKAN_STAGE_FRAGMENT] = VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    VkShaderModule                  modules[GPU_VULKAN_STAGE_COUNT] = {0};
    VkPipelineShaderStageCreateInfo stages[GPU_VULKAN_STAGE_COUNT]  = {0};
    uint32_t                        stage_count                     = 0;
    VkPipeline                      pipeline                        = VK_NULL_HANDLE;

    // A graphics pipeline without a vertex shader is not a legal one.
    // A missing fragment shader is fine - it just writes no colour.
    if (!scene->spirv[GPU_VULKAN_STAGE_VERTEX]) {
        rvvm_warn("%s: No SPIR-V submitted", __FUNCTION__);
        return false;
    }

    for (uint32_t s = 0; s < GPU_VULKAN_STAGE_COUNT; s++) {
        if (!scene->spirv[s] || !scene->spirv_nwords[s] || !stage_bits[s]) {
            continue;
        }
        VkShaderModuleCreateInfo module_ci = {
            .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = scene->spirv_nwords[s] * sizeof(uint32_t),
            .pCode    = scene->spirv[s],
        };
        rvvm_info("Submit SPIR-V: %p", scene->spirv[s]);
        VK_TRY(vkCreateShaderModule(ctx->device, &module_ci, NULL, &modules[s]));
        rvvm_info("%s: Write stage %u", __FUNCTION__, s);
        // Entry point name confirmed to be "main" for each kind of shader.
        stages[stage_count++] = (VkPipelineShaderStageCreateInfo) {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = stage_bits[s],
            .module = modules[s],
            .pName  = "main",
        };
    }

    rvvm_info("%s: Build pipeline (stages: %p, count: %u)", __FUNCTION__, stages, stage_count);
    pipeline = gpu_vulkan_build_pipeline(ctx, stages, stage_count, scene->topology, scene);
    rvvm_info("Created guest Vulkan pipeline");

fail:
    for (uint32_t s = 0; s < GPU_VULKAN_STAGE_COUNT; s++) {
        if (modules[s]) {
            vkDestroyShaderModule(ctx->device, modules[s], NULL);
        }
    }
    if (pipeline == VK_NULL_HANDLE) {
        rvvm_warn("%s: Vulkan attempt to create pipeline failed", __FUNCTION__);
        return false;
    }
    if (ctx->guest_pipeline) {
        vkDestroyPipeline(ctx->device, ctx->guest_pipeline, NULL);
    }
    ctx->guest_pipeline     = pipeline;
    ctx->guest_pipeline_key = key;
    return true;
}

// -----------------------------------------------------------
// Scene handoff
// -----------------------------------------------------------

static void gpu_vulkan_scene_free(gpu_vulkan_scene_t* scene)
{
    for (uint32_t s = 0; s < GPU_VULKAN_STAGE_COUNT; s++) {
        free(scene->spirv[s]);
    }
    for (uint32_t i = 0; i < GPU_VULKAN_MAX_VERTEX_BINDINGS; i++) {
        free(scene->vertex_data[i]);
    }
    free(scene->index_data);
    memset(scene, 0, sizeof(*scene));
}

bool gpu_vulkan_submit_draw(gpu_vulkan_ctx_t* ctx, const gpu_vulkan_draw_t* draw)
{
    if (unlikely(!ctx || !draw || atomic_load_uint8_relax(&ctx->shutting_down))) {
        rvvm_warn("%s: invariant", __FUNCTION__);
        return false;
    }
    // Every graphics pipeline needs a vertex shader; without one there
    // is nothing to build the scene around.
    if (!draw->stage[GPU_VULKAN_STAGE_VERTEX].spirv || !draw->stage[GPU_VULKAN_STAGE_VERTEX].spirv_nwords) {
        rvvm_warn("%s: No SPIR-V (spirv: %p, spirv_nwords: %u)", __FUNCTION__,
                  draw->stage[GPU_VULKAN_STAGE_VERTEX].spirv, draw->stage[GPU_VULKAN_STAGE_VERTEX].spirv_nwords);
        return false;
    }

    // Build the copy outside the lock - the device thread should not
    // hold it across allocations. The constants are staged here too and
    // published together with the scene, so the renderer never pairs new
    // shaders with the previous draw's constants.
    uint8_t consts[GPU_VULKAN_STAGE_COUNT][GPU_VULKAN_CONST_BYTES] = {0};

    gpu_vulkan_scene_t scene = {
        .topology
        = draw->topology <= GPU_VULKAN_TOPOLOGY_TRIANGLE_FAN ? draw->topology : GPU_VULKAN_TOPOLOGY_TRIANGLE_LIST,
        .vertex_count   = draw->vertex_count,
        .instance_count = draw->instance_count ? draw->instance_count : 1,
        .first_vertex   = draw->first_vertex,
        .first_instance = draw->first_instance,
    };
    for (uint32_t s = 0; s < GPU_VULKAN_STAGE_COUNT; s++) {
        const gpu_vulkan_stage_desc_t* desc = &draw->stage[s];
        if (desc->spirv && desc->spirv_nwords) {
            size_t size    = desc->spirv_nwords * sizeof(uint32_t);
            scene.spirv[s] = safe_malloc(size);
            memcpy(scene.spirv[s], desc->spirv, size);
            scene.spirv_nwords[s] = desc->spirv_nwords;
            rvvm_info("Set up submitted SPIR-V: %08x %08x %08x %08x", scene.spirv[s][0], scene.spirv[s][1],
                      scene.spirv[s][2], scene.spirv[s][3]);
        }
        if (desc->constants && desc->const_bytes) {
            uint32_t nbytes = (desc->const_bytes > GPU_VULKAN_CONST_BYTES) ? GPU_VULKAN_CONST_BYTES : desc->const_bytes;
            memcpy(consts[s], desc->constants, nbytes);
        }
    }

    const gpu_vulkan_vertex_input_t* vi = &draw->vertex;
    scene.vertex_binding_count          = EVAL_MIN(vi->binding_count, GPU_VULKAN_MAX_VERTEX_BINDINGS);
    for (uint32_t i = 0; i < scene.vertex_binding_count; i++) {
        const gpu_vulkan_vertex_binding_t* b = &vi->binding[i];
        scene.vertex_stride[i]               = b->stride;
        if (b->data && b->size) {
            scene.vertex_data[i] = safe_malloc(b->size);
            memcpy(scene.vertex_data[i], b->data, b->size);
            scene.vertex_size[i] = b->size;
        }
    }
    scene.vertex_attrib_count = EVAL_MIN(vi->attrib_count, GPU_VULKAN_MAX_VERTEX_ATTRIBS);
    memcpy(scene.vertex_attrib, vi->attrib, scene.vertex_attrib_count * sizeof(vi->attrib[0]));
    if (vi->index_data && vi->index_size) {
        scene.index_data = safe_malloc(vi->index_size);
        memcpy(scene.index_data, vi->index_data, vi->index_size);
        scene.index_size = vi->index_size;
        scene.index_type = vi->index_type;
    }
    scene.vertex_offset = vi->vertex_offset;

    spin_lock(&ctx->draw_lock);
    // An unconsumed scene is replaced wholesale: the renderer only ever
    // draws the newest one, so the older is dead weight.
    if (ctx->pending_valid) {
        gpu_vulkan_scene_free(&ctx->pending);
    }
    ctx->pending       = scene;
    ctx->pending_valid = true;
    // A draw defines its constant blocks entirely: stages it carries no
    // constants for are zeroed, not left over from the previous draw.
    memcpy(ctx->const_shadow, consts, sizeof(consts));
    ctx->const_dirty = GPU_VULKAN_ALL_STAGES;
    spin_unlock(&ctx->draw_lock);
    return true;
}

bool gpu_vulkan_update_constants(gpu_vulkan_ctx_t* ctx, gpu_vulkan_stage_t stage, uint32_t offset, const void* data,
                                 uint32_t size)
{
    if (unlikely(!ctx || atomic_load_uint8_relax(&ctx->shutting_down))) {
        return false;
    }
    if (unlikely((uint32_t)stage >= GPU_VULKAN_STAGE_COUNT || (!data && size))) {
        rvvm_warn("%s: bad stage %d or data %p", __FUNCTION__, (int)stage, data);
        return false;
    }
    // Written so that offset + size cannot wrap.
    if (unlikely(size > GPU_VULKAN_CONST_BYTES || offset > GPU_VULKAN_CONST_BYTES - size)) {
        rvvm_warn("%s: range [%u, %u + %u) exceeds %u bytes", __FUNCTION__, offset, offset, size,
                  GPU_VULKAN_CONST_BYTES);
        return false;
    }
    if (size == 0) {
        return true;
    }

    spin_lock(&ctx->draw_lock);
    memcpy(&ctx->const_shadow[stage][offset], data, size);
    ctx->const_dirty |= 1u << stage;
    spin_unlock(&ctx->draw_lock);
    return true;
}

// Moves a newly submitted scene into the renderer's own copy, together
// with a snapshot of whichever constant blocks changed since the last
// task - one critical section, so scene and constants stay a matching
// pair. Returns whether a guest scene is available to draw at all.
static bool gpu_vulkan_take_scene(gpu_vulkan_ctx_t* ctx)
{
    spin_lock(&ctx->draw_lock);
    if (ctx->pending_valid) {
        if (ctx->active_valid) {
            gpu_vulkan_scene_free(&ctx->active);
        }
        ctx->active        = ctx->pending;
        ctx->active_valid  = true;
        ctx->pending_valid = false;
        memset(&ctx->pending, 0, sizeof(ctx->pending));
    }
    for (uint32_t s = 0; s < GPU_VULKAN_STAGE_COUNT; s++) {
        if (ctx->const_dirty & (1u << s)) {
            memcpy(ctx->const_stage[s], ctx->const_shadow[s], GPU_VULKAN_CONST_BYTES);
        }
    }
    ctx->const_upload |= ctx->const_dirty;
    ctx->const_dirty   = 0;
    spin_unlock(&ctx->draw_lock);
    return ctx->active_valid;
}

// Writes the snapshotted constant blocks into the mapped uniform buffers
// the descriptor set points at. Render thread only, and only between
// fence waits, so the GPU is never reading a buffer while it is written;
// HOST_COHERENT memory plus vkQueueSubmit() make the writes visible.
static void gpu_vulkan_upload_constants(gpu_vulkan_ctx_t* ctx)
{
    for (uint32_t s = 0; s < GPU_VULKAN_STAGE_COUNT; s++) {
        if ((ctx->const_upload & (1u << s)) && ctx->const_mapped[s]) {
            memcpy(ctx->const_mapped[s], ctx->const_stage[s], GPU_VULKAN_CONST_BYTES);
        }
    }
    ctx->const_upload = 0;
}

// Materializes the active scene's vertex/index bytes into real Vulkan
// buffers. Render thread only, called once per task right before the
// draw is recorded - same slot in the pipeline as upload_constants()
// above, just for data vkCmdBindVertexBuffers()/vkCmdBindIndexBuffer()
// need instead of a descriptor set.
static bool gpu_vulkan_upload_vertex_input(gpu_vulkan_ctx_t* ctx)
{
    const gpu_vulkan_scene_t* scene = &ctx->active;
    for (uint32_t i = 0; i < scene->vertex_binding_count; i++) {
        if (!scene->vertex_data[i] || !scene->vertex_size[i]) {
            continue;
        }
        if (!gpu_vulkan_upload_host_buffer(ctx, &ctx->vertex_buffer[i], &ctx->vertex_memory[i],
                                           &ctx->vertex_buffer_cap[i], VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                           scene->vertex_data[i], scene->vertex_size[i])) {
            return false;
        }
    }
    if (scene->index_data && scene->index_size) {
        if (!gpu_vulkan_upload_host_buffer(ctx, &ctx->index_buffer, &ctx->index_memory, &ctx->index_buffer_cap,
                                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT, scene->index_data, scene->index_size)) {
            return false;
        }
    }
    return true;
}

static bool gpu_vulkan_create_command_and_sync(gpu_vulkan_ctx_t* ctx)
{
    VkCommandPoolCreateInfo pool_ci = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = ctx->graphics_family,
    };
    VK_TRY(vkCreateCommandPool(ctx->device, &pool_ci, NULL, &ctx->command_pool));

    VkCommandBufferAllocateInfo alloc_info = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = ctx->command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VK_TRY(vkAllocateCommandBuffers(ctx->device, &alloc_info, &ctx->command_buffer));

    VkFenceCreateInfo fence_ci = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VK_TRY(vkCreateFence(ctx->device, &fence_ci, NULL, &ctx->fence));
    return true;
fail:
    return false;
}

gpu_vulkan_ctx_t* gpu_vulkan_create(void)
{
    gpu_vulkan_ctx_t* ctx = safe_calloc(1, sizeof(*ctx));

    spin_init(&ctx->draw_lock);

    if (!gpu_vulkan_create_instance_and_device(ctx)) {
        goto fail;
    }
    if (!gpu_vulkan_create_command_and_sync(ctx)) {
        goto fail;
    }
    // The constant buffers and the pipeline layout around them do not
    // depend on the render target, so they are built once here and
    // outlive every mode-set.
    if (!gpu_vulkan_create_const_layout(ctx)) {
        goto fail;
    }
    // render_pass/pipeline are built lazily in gpu_vulkan_reconfigure()
    // once the guest's target pixel format is known, and rebuilt only
    // when that format actually changes (a real mode-set, not every
    // frame).

    return ctx;

fail:
    gpu_vulkan_destroy(ctx);
    return NULL;
}

static void gpu_vulkan_destroy_sized_resources(gpu_vulkan_ctx_t* ctx)
{
    if (!ctx->device) {
        return;
    }
    if (ctx->framebuffer) {
        vkDestroyFramebuffer(ctx->device, ctx->framebuffer, NULL);
    }
    if (ctx->color_view) {
        vkDestroyImageView(ctx->device, ctx->color_view, NULL);
    }
    if (ctx->color_image) {
        vkDestroyImage(ctx->device, ctx->color_image, NULL);
    }
    if (ctx->color_memory) {
        vkFreeMemory(ctx->device, ctx->color_memory, NULL);
    }
    if (ctx->readback_mapped) {
        vkUnmapMemory(ctx->device, ctx->readback_memory);
    }
    if (ctx->readback_buffer) {
        vkDestroyBuffer(ctx->device, ctx->readback_buffer, NULL);
    }
    if (ctx->readback_memory) {
        vkFreeMemory(ctx->device, ctx->readback_memory, NULL);
    }
    ctx->framebuffer     = VK_NULL_HANDLE;
    ctx->color_view      = VK_NULL_HANDLE;
    ctx->color_image     = VK_NULL_HANDLE;
    ctx->color_memory    = VK_NULL_HANDLE;
    ctx->readback_buffer = VK_NULL_HANDLE;
    ctx->readback_memory = VK_NULL_HANDLE;
    ctx->readback_mapped = NULL;
}

void gpu_vulkan_destroy(gpu_vulkan_ctx_t* ctx)
{
    if (unlikely(!ctx)) {
        return;
    }

    // Stop accepting new work and wait for any in-flight page-flip task.
    atomic_store_uint8_relax(&ctx->shutting_down, 1);
    rvvm_info("Vulkan requested to stop");
    while (atomic_load_uint8_relax(&ctx->render_in_progress)) {
        // Worker will clear the flag after its fence wait (or immediately
        // if it sees shutting_down before submit). Short spin is fine;
        // a single triangle completes in well under a millisecond.
    }

    if (ctx->device) {
        vkDeviceWaitIdle(ctx->device);
    }

    gpu_vulkan_destroy_sized_resources(ctx);

    gpu_vulkan_scene_free(&ctx->pending);
    gpu_vulkan_scene_free(&ctx->active);
    ctx->pending_valid = false;
    ctx->active_valid  = false;

    if (ctx->fence) {
        vkDestroyFence(ctx->device, ctx->fence, NULL);
    }
    if (ctx->command_pool) {
        vkDestroyCommandPool(ctx->device, ctx->command_pool, NULL);
    }
    for (uint32_t s = 0; s < GPU_VULKAN_STAGE_COUNT; s++) {
        if (ctx->const_mapped[s]) {
            vkUnmapMemory(ctx->device, ctx->const_memory[s]);
        }
        if (ctx->const_buffer[s]) {
            vkDestroyBuffer(ctx->device, ctx->const_buffer[s], NULL);
        }
        if (ctx->const_memory[s]) {
            vkFreeMemory(ctx->device, ctx->const_memory[s], NULL);
        }
    }
    for (uint32_t i = 0; i < GPU_VULKAN_MAX_VERTEX_BINDINGS; i++) {
        if (ctx->vertex_buffer[i]) {
            vkDestroyBuffer(ctx->device, ctx->vertex_buffer[i], NULL);
        }
        if (ctx->vertex_memory[i]) {
            vkFreeMemory(ctx->device, ctx->vertex_memory[i], NULL);
        }
    }
    if (ctx->index_buffer) {
        vkDestroyBuffer(ctx->device, ctx->index_buffer, NULL);
    }
    if (ctx->index_memory) {
        vkFreeMemory(ctx->device, ctx->index_memory, NULL);
    }
    if (ctx->const_pool) {
        // Frees the set allocated from it.
        vkDestroyDescriptorPool(ctx->device, ctx->const_pool, NULL);
    }
    if (ctx->const_set_layout) {
        vkDestroyDescriptorSetLayout(ctx->device, ctx->const_set_layout, NULL);
    }
    if (ctx->guest_pipeline) {
        vkDestroyPipeline(ctx->device, ctx->guest_pipeline, NULL);
    }
    if (ctx->pipeline_layout) {
        vkDestroyPipelineLayout(ctx->device, ctx->pipeline_layout, NULL);
    }
    if (ctx->render_pass) {
        vkDestroyRenderPass(ctx->device, ctx->render_pass, NULL);
    }
    if (ctx->device) {
        vkDestroyDevice(ctx->device, NULL);
    }
    if (ctx->instance) {
        vkDestroyInstance(ctx->instance, NULL);
    }
    free(ctx);
}

// -----------------------------------------------------------
// Real-time render
// -----------------------------------------------------------



// This optionally re-maps Vulkan framebuffer to CPU-visible memory.
// buf_size is passed in rather than computed here because it depends on
// whether the caller is in the zero-copy path (buf_size == guest's real
// stride * height) or the CPU-conversion fallback (tightly packed
// width * height * bpp) - see gpu_vulkan_reconfigure().
static bool gpu_vulkan_resize_targets(gpu_vulkan_ctx_t* ctx, uint32_t w, uint32_t h, VkFormat vk_format,
                                      VkDeviceSize buf_size)
{
    if (ctx->framebuffer != VK_NULL_HANDLE && ctx->cur_width == w && ctx->cur_height == h
        && ctx->cur_vk_format == vk_format && ctx->cur_buf_size == buf_size) {
        return true; // Already sized correctly - typical steady-state case.
    }

    gpu_vulkan_destroy_sized_resources(ctx);

    // Offscreen color image.
    VkImageCreateInfo image_ci = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = vk_format,
        .extent        = {.width = w, .height = h, .depth = 1},
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VK_TRY(vkCreateImage(ctx->device, &image_ci, NULL, &ctx->color_image));

    VkMemoryRequirements image_req = {0};
    vkGetImageMemoryRequirements(ctx->device, ctx->color_image, &image_req);
    uint32_t image_mem_type = gpu_vulkan_find_memory_type(ctx->physical_device, image_req.memoryTypeBits,
                                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (image_mem_type == UINT32_MAX) {
        goto fail;
    }
    VkMemoryAllocateInfo image_alloc = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = image_req.size,
        .memoryTypeIndex = image_mem_type,
    };
    VK_TRY(vkAllocateMemory(ctx->device, &image_alloc, NULL, &ctx->color_memory));
    VK_TRY(vkBindImageMemory(ctx->device, ctx->color_image, ctx->color_memory, 0));

    VkImageViewCreateInfo view_ci = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = ctx->color_image,
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = vk_format,
        .subresourceRange = {.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel   = 0,
                             .levelCount     = 1,
                             .baseArrayLayer = 0,
                             .layerCount     = 1},
    };
    VK_TRY(vkCreateImageView(ctx->device, &view_ci, NULL, &ctx->color_view));

    VkFramebufferCreateInfo framebuffer_ci = {
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass      = ctx->render_pass,
        .attachmentCount = 1,
        .pAttachments    = &ctx->color_view,
        .width           = w,
        .height          = h,
        .layers          = 1,
    };
    VK_TRY(vkCreateFramebuffer(ctx->device, &framebuffer_ci, NULL, &ctx->framebuffer));

    // Host-visible readback buffer, sized by the caller (see above).
    VkBufferCreateInfo buf_ci = {
        .size        = buf_size,
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_TRY(vkCreateBuffer(ctx->device, &buf_ci, NULL, &ctx->readback_buffer));

    VkMemoryRequirements buf_req = {0};
    vkGetBufferMemoryRequirements(ctx->device, ctx->readback_buffer, &buf_req);
    uint32_t buf_mem_type
        = gpu_vulkan_find_memory_type(ctx->physical_device, buf_req.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (buf_mem_type == UINT32_MAX) {
        goto fail;
    }
    VkMemoryAllocateInfo buf_alloc = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = buf_req.size,
        .memoryTypeIndex = buf_mem_type,
    };
    VK_TRY(vkAllocateMemory(ctx->device, &buf_alloc, NULL, &ctx->readback_memory));
    VK_TRY(vkBindBufferMemory(ctx->device, ctx->readback_buffer, ctx->readback_memory, 0));
    // HOST_COHERENT: no explicit flush/invalidate needed around reads.
    VK_TRY(vkMapMemory(ctx->device, ctx->readback_memory, 0, buf_size, 0, &ctx->readback_mapped));

    ctx->cur_width     = w;
    ctx->cur_height    = h;
    ctx->cur_vk_format = vk_format;
    ctx->cur_buf_size  = buf_size;
    return true;

fail:
    gpu_vulkan_destroy_sized_resources(ctx);
    return false;
}

// Picks the best Vulkan format for the guest's requested pixel format,
// rebuilds the render pass/pipeline if that format actually changed
// (rare - a real mode-set), and (re)sizes the offscreen image/readback
// buffer for the current width/height/stride. Leaves ctx->zero_copy set
// so the caller knows whether the GPU output already matches the
// guest's byte layout (straight memcpy) or needs the CPU fallback path.
static bool gpu_vulkan_reconfigure(gpu_vulkan_ctx_t* ctx, uint32_t w, uint32_t h, uint32_t stride,
                                   rvvm_rgb_t guest_format)
{
    VkFormat native    = gpu_vulkan_native_format_for_guest(guest_format);
    bool     zero_copy = gpu_vulkan_format_usable(ctx->physical_device, native);
    VkFormat vk_format = zero_copy ? native : VK_FORMAT_B8G8R8A8_UNORM;

    if (vk_format != ctx->cur_vk_format) {
        // Attachment format changed - the render pass and every pipeline
        // built against it must be rebuilt to match; this only happens on
        // a real mode-set. The pipeline layout does not depend on the
        // format and stays as it is, so the constant bindings survive.
        if (ctx->guest_pipeline) {
            vkDestroyPipeline(ctx->device, ctx->guest_pipeline, NULL);
        }
        if (ctx->render_pass) {
            vkDestroyRenderPass(ctx->device, ctx->render_pass, NULL);
        }
        ctx->guest_pipeline     = VK_NULL_HANDLE;
        ctx->guest_pipeline_key = 0;
        ctx->render_pass        = VK_NULL_HANDLE;

        if (!gpu_vulkan_create_render_pass(ctx, vk_format)) {
            return false;
        }
    }

    VkDeviceSize buf_size
        = zero_copy ? (VkDeviceSize)stride * h                                   // GPU writes guest's exact layout.
                    : (VkDeviceSize)w * h * gpu_vulkan_vk_format_bpp(vk_format); // Tightly packed for CPU fallback.

    if (!gpu_vulkan_resize_targets(ctx, w, h, vk_format, buf_size)) {
        return false;
    }

    ctx->zero_copy  = zero_copy;
    ctx->cur_stride = stride;
    return true;
}

// -----------------------------------------------------------
// CPU-side format conversion, now done on the worker thread
// -----------------------------------------------------------

forceinline static size_t gpu_vulkan_bytes_per_pixel(rvvm_rgb_t format)
{
    switch (format) {
        case RVVM_RGB_RGB565:
            return 2;

        default:
            return 4; // XRGB8888 / XBGR8888 / XRGB2101010
    }
}

// Only used by the fallback path, which always renders in
// VK_FORMAT_B8G8R8A8_UNORM (see gpu_vulkan_reconfigure), so src_bgra is
// always (B,G,R,A) per pixel here.
static inline void gpu_vulkan_write_pixel(uint8_t* dst, const uint8_t* src_bgra, rvvm_rgb_t format)
{
    uint8_t b = src_bgra[0];
    uint8_t g = src_bgra[1];

    uint8_t r = src_bgra[2];
    switch (format) {
        case RVVM_RGB_RGB565: {
            uint16_t pixel = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            memcpy(dst, &pixel, sizeof(pixel));
            break;
        }
        case RVVM_RGB_XRGB2101010: {
            uint32_t r10   = (uint32_t)r * 1023 / 255;
            uint32_t g10   = (uint32_t)g * 1023 / 255;
            uint32_t b10   = (uint32_t)b * 1023 / 255;
            uint32_t pixel = (r10 << 20) | (g10 << 10) | b10;
            memcpy(dst, &pixel, sizeof(pixel));
            break;
        }
        case RVVM_RGB_XBGR8888:
            dst[0] = b;
            dst[1] = g;
            dst[2] = r;
            dst[3] = 0xFF;
            break;
        case RVVM_RGB_XRGB8888:
        default:
            dst[0] = r;
            dst[1] = g;
            dst[2] = b;
            dst[3] = 0xFF;
            break;
    }
}

// Converts the tightly-packed BGRA8 render output into dst at the
// guest's real stride/format. Runs entirely on the worker thread now -
// this used to be xe2_blit_shader_output() called from xe2_scanout().
static void gpu_vulkan_blit_to_guest(uint8_t* dst, uint32_t stride, const uint8_t* pixels, size_t row_pitch,
                                     uint32_t width, uint32_t height, rvvm_rgb_t format)
{
    size_t bpp = gpu_vulkan_bytes_per_pixel(format);
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* src_row = pixels + (size_t)y * row_pitch;
        uint8_t*       dst_row = dst + (size_t)y * stride;
        for (uint32_t x = 0; x < width; x++) {
            gpu_vulkan_write_pixel(dst_row + x * bpp, src_row + x * 4, format);
        }
    }
}

// Worker task that performs the actual Vulkan work (resize + draw +
// readback + fence wait), then does the CPU-side blit directly into the
// guest's vram buffer. Publishes front_width/height/stride/format
// describing what's now sitting in that buffer, so the next scanout
// can page-flip without touching a single pixel.
//
// I am drunk while writing this. God forgive me.
static void* gpu_vulkan_render_task(void* arg)
{
    gpu_vulkan_ctx_t* ctx      = (gpu_vulkan_ctx_t*)arg;
    uint32_t          width    = ctx->requested_width;
    uint32_t          height   = ctx->requested_height;
    uint32_t          stride   = ctx->requested_stride;
    rvvm_rgb_t        format   = ctx->requested_format;
    uint8_t*          dst      = ctx->requested_dst;
    size_t            dst_size = ctx->requested_dst_size;

    rvvm_debug("Vulkan render task");

    if (atomic_load_uint8_relax(&ctx->shutting_down) || width == 0 || height == 0) {
        goto done;
    }

    if (!gpu_vulkan_reconfigure(ctx, width, height, stride, format)) {
        goto done;
    }
    rvvm_debug("Vulkan reconfigure done");

    // Pick up whatever the guest submitted last. A scene whose pipeline
    // fails to build (a kernel we cross-compiled into something the
    // driver rejects) falls back to the built-in one rather than
    // dropping the frame, so the display keeps updating.
    bool guest_scene = gpu_vulkan_take_scene(ctx) && gpu_vulkan_ensure_guest_pipeline(ctx);
    if (!guest_scene) {
        rvvm_warn("Failed to obtain guest pipeline");
        goto done;
    }

    // Publish whatever constants changed into the uniform blocks the
    // shaders read. Nothing else touches these buffers, and the previous
    // frame's fence has already been waited on.
    gpu_vulkan_upload_constants(ctx);
    if (!gpu_vulkan_upload_vertex_input(ctx)) {
        rvvm_warn("Failed to upload vertex/index buffers");
        goto done;
    }

    vkResetCommandBuffer(ctx->command_buffer, 0);
    VkCommandBufferBeginInfo begin_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (vkBeginCommandBuffer(ctx->command_buffer, &begin_info) != VK_SUCCESS) {
        goto done;
    }

    VkViewport viewport = {
        .x        = 0.0f,
        .y        = 0.0f,
        .width    = width,
        .height   = height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {
        .offset = {        .x = 0,           .y = 0},
        .extent = {.width = width, .height = height},
    };
    vkCmdSetViewport(ctx->command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(ctx->command_buffer, 0, 1, &scissor);

    VkClearValue          clear            = {.color = {.float32 = {0.02f, 0.02f, 0.05f, 1.0f}}};
    VkRenderPassBeginInfo render_pass_info = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass      = ctx->render_pass,
        .framebuffer     = ctx->framebuffer,
        .renderArea      = {.offset = {.x = 0, .y = 0}, .extent = {.width = width, .height = height}},
        .clearValueCount = 1,
        .pClearValues    = &clear,
    };
    vkCmdBeginRenderPass(ctx->command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(ctx->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->guest_pipeline);
    vkCmdBindDescriptorSets(ctx->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pipeline_layout,
                            GPU_VULKAN_CONST_SET, 1, &ctx->const_set, 0, NULL);
    if (!!ctx->active.vertex_binding_count) {
        VkBuffer     buffers[GPU_VULKAN_MAX_VERTEX_BINDINGS];
        VkDeviceSize offsets[GPU_VULKAN_MAX_VERTEX_BINDINGS] = {0};
        for (uint32_t i = 0; i < ctx->active.vertex_binding_count; i++) {
            buffers[i] = ctx->vertex_buffer[i];
        }
        vkCmdBindVertexBuffers(ctx->command_buffer, 0, ctx->active.vertex_binding_count, buffers, offsets);
    }
    if (ctx->active.index_data && ctx->index_buffer) {
        vkCmdBindIndexBuffer(ctx->command_buffer, ctx->index_buffer, 0, (VkIndexType)ctx->active.index_type);
        vkCmdDrawIndexed(ctx->command_buffer, ctx->active.vertex_count, ctx->active.instance_count,
                         ctx->active.first_vertex, ctx->active.vertex_offset, ctx->active.first_instance);
    } else {
        vkCmdDraw(ctx->command_buffer, ctx->active.vertex_count, ctx->active.instance_count, ctx->active.first_vertex,
                  ctx->active.first_instance);
    }
    vkCmdEndRenderPass(ctx->command_buffer);
    // finalLayout already leaves the image in TRANSFER_SRC_OPTIMAL.

    // In the zero-copy path, bufferRowLength is the guest's real stride
    // (in texels of vk_format) - the GPU's copy engine handles any row
    // padding for us. In the fallback path it's just `width`, tightly
    // packed, and gpu_vulkan_blit_to_guest() destrides it on the CPU.
    size_t   vk_bpp         = gpu_vulkan_vk_format_bpp(ctx->cur_vk_format);
    uint32_t buffer_row_len = ctx->zero_copy ? (uint32_t)(stride / vk_bpp) : width;

    VkBufferImageCopy region = {
        .bufferOffset      = 0,
        .bufferRowLength   = buffer_row_len,
        .bufferImageHeight = height,
        .imageSubresource
        = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
        .imageExtent = {.width = width, .height = height, .depth = 1}
    };
    vkCmdCopyImageToBuffer(ctx->command_buffer, ctx->color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           ctx->readback_buffer, 1, &region);

    if (vkEndCommandBuffer(ctx->command_buffer) != VK_SUCCESS) {
        goto done;
    }

    if (vkResetFences(ctx->device, 1, &ctx->fence) != VK_SUCCESS) {
        goto done;
    }
    VkSubmitInfo submit = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &ctx->command_buffer,
    };
    if (vkQueueSubmit(ctx->graphics_queue, 1, &submit, ctx->fence) != VK_SUCCESS) {
        goto done;
    }

    // Blocking wait lives on the worker thread, never on the scanout path.
    if (vkWaitForFences(ctx->device, 1, &ctx->fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        goto done;
    }

    if (atomic_load_uint8_relax(&ctx->shutting_down)) {
        goto done;
    }

    // dst/dst_size/stride/format were snapshotted at the top of this
    // function, alongside width/height.
    if (dst && stride) {
        if (ctx->zero_copy) {
            // The GPU already wrote exactly what the guest needs, at the
            // guest's real stride - one flat memcpy, no per-pixel work,
            // no branchy switch. This is the fast path for all four
            // formats whenever the GPU supports them as attachments.
            size_t needed = (size_t)stride * height;
            if (needed <= dst_size) {
                memcpy(dst, ctx->readback_mapped, needed);
                ctx->front_width  = width;
                ctx->front_height = height;
                ctx->front_stride = stride;
                ctx->front_format = format;
            }
        } else {
            // Fallback: this GPU can't render natively in the guest's
            // format (only expected for RGB565/XRGB2101010 on some
            // drivers) - readback_mapped is tightly-packed B8G8R8A8 here,
            // matching gpu_vulkan_blit_to_guest()'s assumption.
            size_t row_pitch = (size_t)width * gpu_vulkan_vk_format_bpp(ctx->cur_vk_format);
            size_t needed    = (size_t)stride * height;
            if (needed <= dst_size) {
                gpu_vulkan_blit_to_guest(dst, stride, ctx->readback_mapped, row_pitch, width, height, format);
                ctx->front_width  = width;
                ctx->front_height = height;
                ctx->front_stride = stride;
                ctx->front_format = format;
            }
        }
    }
    ctx->frame_index++;

done:
    atomic_store_uint8_relax(&ctx->render_in_progress, 0);
    return NULL;
}

// dst/dst_size/stride/format describe the guest's own vram buffer
// `rvvm_fbdev_get_vram()` - the worker blits directly into it, so
// nothing here ever touches a pixel. out_width/out_height/out_stride/
// out_format describe whatever the worker most recently finished
// writing into dst, which the caller hands straight to
// rvvm_fbdev_set_scanout() - no per-frame CPU work on this thread.
bool gpu_vulkan_render_frame(gpu_vulkan_ctx_t* ctx, uint32_t width, uint32_t height, uint8_t* dst, size_t dst_size,
                             uint32_t stride, rvvm_rgb_t format, uint32_t* out_width, uint32_t* out_height,
                             uint32_t* out_stride, rvvm_rgb_t* out_format)
{
    if (unlikely(!ctx || width == 0 || height == 0 || atomic_load_uint8_relax(&ctx->shutting_down))) {
        return false;
    }

    // Publish what the worker should render/blit next. Cheap field
    // stores, same as requested_width/height already were.
    ctx->requested_width    = width;
    ctx->requested_height   = height;
    ctx->requested_dst      = dst;
    ctx->requested_dst_size = dst_size;
    ctx->requested_stride   = stride;
    ctx->requested_format   = format;

    // Queue a new render+blit only when the previous one has finished.
    // This keeps at most one in-flight task, and guarantees the GUI
    // thread is never the one doing the work, no matter how slow the
    // blit is.
    if (atomic_cas_uint8(&ctx->render_in_progress, 0, 1)) {
        thread_create_task(gpu_vulkan_render_task, ctx);
    }

    // Page-flip: hand back whatever the worker last actually wrote into
    // dst. May lag behind the just-requested width/height if the worker
    // hasn't caught up yet - that's expected, not a bug; it just shows
    // the previous frame a little longer instead of blocking or glitching.
    if (ctx->front_width && ctx->front_height) {
        *out_width  = ctx->front_width;
        *out_height = ctx->front_height;
        *out_stride = ctx->front_stride;
        *out_format = ctx->front_format;
        return true;
    }

    return false;
}

#endif /* USE_GPU */
