/*
 * Zhaoxu OS - software Vulkan 1.0 ICD.
 *
 * This is a real, executable Vulkan implementation for offscreen rendering:
 * it owns instances, devices, memory, buffers, images, render passes,
 * framebuffers, SPIR-V shader modules, graphics pipelines, command buffers
 * and a synchronous queue.  Draws go through the SPIR-V interpreter and a
 * CPU triangle rasterizer into a host-visible RGBA8 image.
 *
 * It is not a complete Vulkan driver: there is no WSI/swapchain, no
 * descriptor sets or uniform buffers, no compute, no depth/stencil, no
 * multisampling, no semaphores/timeline fences and no GPU acceleration.
 * Unsupported calls return VK_ERROR_FEATURE_NOT_PRESENT or are rejected at
 * pipeline creation instead of pretending to work.
 */
#include <vulkan/vulkan.h>

#include <stdlib.h>
#include <string.h>

#include "spirv.h"

#define VK_SOFT_MAGIC 0x5A4B564Bu

struct VkInstance_T {
    unsigned int magic;
    unsigned int api_version;
};

struct VkPhysicalDevice_T {
    unsigned int magic;
};

struct VkDevice_T {
    unsigned int magic;
    struct VkQueue_T *queue;
};

struct VkQueue_T {
    unsigned int magic;
    struct VkDevice_T *device;
};

struct VkDeviceMemory_T {
    unsigned int magic;
    void *host;
    VkDeviceSize size;
};

struct VkBuffer_T {
    unsigned int magic;
    VkDeviceSize size;
    VkBufferUsageFlags usage;
    VkDeviceMemory memory;
    VkDeviceSize offset;
};

struct VkImage_T {
    unsigned int magic;
    unsigned int width;
    unsigned int height;
    VkFormat format;
    VkImageUsageFlags usage;
    VkDeviceMemory memory;
    unsigned char *pixels;
    VkImageLayout layout;
};

struct VkImageView_T {
    unsigned int magic;
    VkImage image;
    VkImageViewType type;
};

struct VkShaderModule_T {
    unsigned int magic;
    unsigned int *words;
    unsigned int word_count;
    struct spv_module vertex;
    struct spv_module fragment;
    int vertex_valid;
    int fragment_valid;
};

struct VkRenderPass_T {
    unsigned int magic;
    VkFormat format;
};

struct VkFramebuffer_T {
    unsigned int magic;
    VkImageView view;
    unsigned int width;
    unsigned int height;
};

struct VkPipelineLayout_T {
    unsigned int magic;
};

struct VkPipeline_T {
    unsigned int magic;
    VkShaderModule vertex_shader;
    VkShaderModule fragment_shader;
    VkPrimitiveTopology topology;
    VkVertexInputBindingDescription binding;
    VkVertexInputAttributeDescription attributes[8];
    unsigned int attribute_count;
    VkViewport viewport;
    VkRect2D scissor;
    int has_viewport;
    int has_scissor;
    int blend_enable;
    VkBlendFactor blend_src;
    VkBlendFactor blend_dst;
};

struct VkCommandPool_T {
    unsigned int magic;
};

enum vk_command_type {
    VK_CMD_BEGIN_RENDER_PASS = 1,
    VK_CMD_BIND_PIPELINE,
    VK_CMD_BIND_VERTEX_BUFFER,
    VK_CMD_DRAW,
    VK_CMD_END_RENDER_PASS
};

struct VkCommandBuffer_T {
    unsigned int magic;
    int recording;
    int command_count;
    struct {
        int type;
        VkFramebuffer framebuffer;
        VkRenderPass render_pass;
        float clear[4];
        VkPipeline pipeline;
        VkBuffer buffer;
        VkDeviceSize offset;
        unsigned int first_vertex;
        unsigned int vertex_count;
    } commands[64];
};

struct VkFence_T {
    unsigned int magic;
    int signaled;
    VkFenceCreateFlags flags;
};

static struct VkPhysicalDevice_T g_physical_device = {VK_SOFT_MAGIC};
static struct VkQueue_T g_queue;

/* Freestanding string helpers; the user tree links without libc. */
static void copy_string(char *destination, const char *source,
                        unsigned long capacity)
{
    unsigned long i = 0;
    for (; source[i] && i + 1 < capacity; i++)
        destination[i] = source[i];
    destination[i] = 0;
}

static int same_string(const char *left, const char *right)
{
    while (*left && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

/* Convert an RGBA float to one byte per channel with clamping. */
static unsigned char clamp_byte(float value)
{
    if (value <= 0)
        return 0;
    if (value >= 1)
        return 255;
    return (unsigned char)(value * 255.0f + 0.5f);
}

/* Fill the image with the render pass clear colour. */
static void clear_image(struct VkImage_T *image, const float color[4])
{
    unsigned char red = clamp_byte(color[0]);
    unsigned char green = clamp_byte(color[1]);
    unsigned char blue = clamp_byte(color[2]);
    unsigned char alpha = clamp_byte(color[3]);
    for (unsigned int i = 0; i < image->width * image->height; i++) {
        image->pixels[i * 4 + 0] = red;
        image->pixels[i * 4 + 1] = green;
        image->pixels[i * 4 + 2] = blue;
        image->pixels[i * 4 + 3] = alpha;
    }
}

/* Read one vertex attribute from the bound vertex buffer. */
static int read_attribute(const float *vertex, unsigned int location,
                          const struct VkPipeline_T *pipeline, float out[4])
{
    out[0] = out[1] = out[2] = 0;
    out[3] = 1;
    for (unsigned int i = 0; i < pipeline->attribute_count; i++) {
        const VkVertexInputAttributeDescription *attribute =
            &pipeline->attributes[i];
        if ((unsigned int)attribute->location != location)
            continue;
        const float *source =
            (const float *)((const unsigned char *)vertex + attribute->offset);
        switch (attribute->format) {
        case VK_FORMAT_R32_SFLOAT:
            out[0] = source[0];
            return 1;
        case VK_FORMAT_R32G32_SFLOAT:
            out[0] = source[0];
            out[1] = source[1];
            return 2;
        case VK_FORMAT_R32G32B32_SFLOAT:
            out[0] = source[0];
            out[1] = source[1];
            out[2] = source[2];
            return 3;
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            out[0] = source[0];
            out[1] = source[1];
            out[2] = source[2];
            out[3] = source[3];
            return 4;
        default:
            return 0;
        }
    }
    return 0;
}

/* Rasterize one triangle through the vertex and fragment stages. */
static void raster_triangle(struct VkImage_T *image,
                            const struct VkPipeline_T *pipeline,
                            struct VkShaderModule_T *fragment_module,
                            const struct spv_vertex_result triangle[3])
{
    unsigned int width = image->width;
    unsigned int height = image->height;
    float viewport_x = pipeline->has_viewport ? pipeline->viewport.x : 0;
    float viewport_y = pipeline->has_viewport ? pipeline->viewport.y : 0;
    float viewport_w = pipeline->has_viewport
                           ? pipeline->viewport.width
                           : (float)width;
    float viewport_h = pipeline->has_viewport
                           ? pipeline->viewport.height
                           : (float)height;
    float x0 = viewport_x +
               (triangle[0].position[0] / triangle[0].position[3] + 1) *
                   0.5f * viewport_w;
    float y0 = viewport_y +
               (triangle[0].position[1] / triangle[0].position[3] + 1) *
                   0.5f * viewport_h;
    float x1 = viewport_x +
               (triangle[1].position[0] / triangle[1].position[3] + 1) *
                   0.5f * viewport_w;
    float y1 = viewport_y +
               (triangle[1].position[1] / triangle[1].position[3] + 1) *
                   0.5f * viewport_h;
    float x2 = viewport_x +
               (triangle[2].position[0] / triangle[2].position[3] + 1) *
                   0.5f * viewport_w;
    float y2 = viewport_y +
               (triangle[2].position[1] / triangle[2].position[3] + 1) *
                   0.5f * viewport_h;

    float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (area == 0)
        return;

    int min_x = (int)(x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2));
    int max_x = (int)(x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2)) + 1;
    int min_y = (int)(y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2));
    int max_y = (int)(y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2)) + 1;
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= (int)width) max_x = (int)width - 1;
    if (max_y >= (int)height) max_y = (int)height - 1;
    if (pipeline->has_scissor) {
        if (min_x < pipeline->scissor.offset.x)
            min_x = pipeline->scissor.offset.x;
        if (min_y < pipeline->scissor.offset.y)
            min_y = pipeline->scissor.offset.y;
        int scissor_right = pipeline->scissor.offset.x +
                            (int)pipeline->scissor.extent.width - 1;
        int scissor_bottom = pipeline->scissor.offset.y +
                             (int)pipeline->scissor.extent.height - 1;
        if (max_x > scissor_right)
            max_x = scissor_right;
        if (max_y > scissor_bottom)
            max_y = scissor_bottom;
    }

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float w0 = ((x1 - px) * (y2 - py) - (y1 - py) * (x2 - px)) /
                       area;
            float w1 = ((x2 - px) * (y0 - py) - (y2 - py) * (x0 - px)) /
                       area;
            float w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0)
                continue;

            float varyings[SPV_MAX_LOCATIONS][4];
            int components[SPV_MAX_LOCATIONS];
            memset(varyings, 0, sizeof(varyings));
            memset(components, 0, sizeof(components));
            for (int i = 0; i < 3; i++) {
                for (int location = 0; location < SPV_MAX_LOCATIONS;
                     location++) {
                    int count = triangle[i].varying_components[location];
                    if (count <= 0)
                        continue;
                    float weight = i == 0 ? w0 : (i == 1 ? w1 : w2);
                    for (int k = 0; k < count; k++)
                        varyings[location][k] +=
                            triangle[i].varyings[location][k] * weight;
                    components[location] = count;
                }
            }

            float color[4] = {0, 0, 0, 1};
            if (spv_run_fragment(&fragment_module->fragment, varyings,
                                 components, color) != 0)
                continue;

            unsigned char *pixel =
                &image->pixels[(y * (int)width + x) * 4];
            if (pipeline->blend_enable) {
                float source_factor = 1;
                float destination_factor = 0;
                if (pipeline->blend_src == VK_BLEND_FACTOR_SRC_ALPHA)
                    source_factor = color[3];
                else if (pipeline->blend_src == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)
                    source_factor = 1 - color[3];
                else if (pipeline->blend_src == VK_BLEND_FACTOR_ONE)
                    source_factor = 1;
                if (pipeline->blend_dst == VK_BLEND_FACTOR_SRC_ALPHA)
                    destination_factor = color[3];
                else if (pipeline->blend_dst ==
                         VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)
                    destination_factor = 1 - color[3];
                else if (pipeline->blend_dst == VK_BLEND_FACTOR_ONE)
                    destination_factor = 1;
                for (int k = 0; k < 3; k++) {
                    float destination = pixel[k] / 255.0f;
                    color[k] = color[k] * source_factor +
                               destination * destination_factor;
                }
            }
            pixel[0] = clamp_byte(color[0]);
            pixel[1] = clamp_byte(color[1]);
            pixel[2] = clamp_byte(color[2]);
            pixel[3] = clamp_byte(color[3]);
        }
    }
}

/* Execute one recorded draw command. */
static void execute_draw(struct VkImage_T *image,
                         const struct VkPipeline_T *pipeline,
                         VkBuffer buffer_handle, VkDeviceSize offset,
                         unsigned int first_vertex, unsigned int vertex_count)
{
    struct VkBuffer_T *buffer = (struct VkBuffer_T *)buffer_handle;
    if (!buffer || !buffer->memory || !pipeline)
        return;
    struct VkShaderModule_T *vertex_module =
        (struct VkShaderModule_T *)pipeline->vertex_shader;
    struct VkShaderModule_T *fragment_module =
        (struct VkShaderModule_T *)pipeline->fragment_shader;
    if (!vertex_module || !fragment_module)
        return;

    const unsigned char *base =
        (const unsigned char *)buffer->memory->host + buffer->offset + offset;
    unsigned int stride = pipeline->binding.stride;
    if (stride == 0)
        return;

    for (unsigned int vertex = first_vertex;
         vertex + 2 < first_vertex + vertex_count; vertex += 3) {
        struct spv_vertex_result results[3];
        int valid = 1;
        for (int i = 0; i < 3; i++) {
            const float *data = (const float *)(base + (vertex + i) * stride);
            struct spv_attribute attributes[8];
            int attribute_count = 0;
            for (unsigned int a = 0; a < pipeline->attribute_count; a++) {
                float values[4];
                int components = read_attribute(data, pipeline->attributes[a].location,
                                                pipeline, values);
                if (components <= 0)
                    continue;
                attributes[attribute_count].location =
                    (int)pipeline->attributes[a].location;
                attributes[attribute_count].components = components;
                attributes[attribute_count].data = 0;
                /* The interpreter copies immediately, so point at a stable
                 * per-attribute buffer in this stack frame. */
                float *copy = malloc(sizeof(float) * (unsigned long)components);
                if (!copy) {
                    valid = 0;
                    break;
                }
                for (int k = 0; k < components; k++)
                    copy[k] = values[k];
                attributes[attribute_count].data = copy;
                attribute_count++;
            }
            int status = valid ? spv_run_vertex(&vertex_module->vertex,
                                                attributes, attribute_count,
                                                &results[i])
                               : -1;
            for (int a = 0; a < attribute_count; a++)
                free((void *)attributes[a].data);
            if (status != 0) {
                valid = 0;
                break;
            }
        }
        if (valid)
            raster_triangle(image, pipeline, fragment_module, results);
    }
}

/* Execute every command recorded in one command buffer. */
static void execute_command_buffer(struct VkCommandBuffer_T *command_buffer)
{
    struct VkImage_T *image = 0;
    struct VkPipeline_T *pipeline = 0;
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkDeviceSize vertex_offset = 0;

    for (int i = 0; i < command_buffer->command_count; i++) {
        typeof(command_buffer->commands[0]) *command =
            &command_buffer->commands[i];
        switch (command->type) {
        case VK_CMD_BEGIN_RENDER_PASS: {
            struct VkFramebuffer_T *framebuffer =
                (struct VkFramebuffer_T *)command->framebuffer;
            if (framebuffer && framebuffer->view) {
                struct VkImageView_T *view =
                    (struct VkImageView_T *)framebuffer->view;
                image = (struct VkImage_T *)view->image;
                if (image && image->pixels)
                    clear_image(image, command->clear);
            }
            break;
        }
        case VK_CMD_BIND_PIPELINE:
            pipeline = (struct VkPipeline_T *)command->pipeline;
            break;
        case VK_CMD_BIND_VERTEX_BUFFER:
            vertex_buffer = command->buffer;
            vertex_offset = command->offset;
            break;
        case VK_CMD_DRAW:
            if (image && pipeline)
                execute_draw(image, pipeline, vertex_buffer, vertex_offset,
                             command->first_vertex, command->vertex_count);
            break;
        case VK_CMD_END_RENDER_PASS:
            image = 0;
            break;
        default:
            break;
        }
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t *version)
{
    if (version)
        *version = VK_API_VERSION_1_0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo *create_info, const VkAllocationCallbacks *alloc,
    VkInstance *instance)
{
    (void)alloc;
    if (!create_info || !instance)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (create_info->pApplicationInfo &&
        create_info->pApplicationInfo->apiVersion > VK_API_VERSION_1_0)
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    struct VkInstance_T *object = malloc(sizeof(*object));
    if (!object)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(object, 0, sizeof(*object));
    object->magic = VK_SOFT_MAGIC;
    object->api_version = VK_API_VERSION_1_0;
    *instance = object;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance,
                                             const VkAllocationCallbacks *alloc)
{
    (void)alloc;
    free(instance);
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char *layer_name, uint32_t *count,
    VkExtensionProperties *properties)
{
    (void)layer_name;
    (void)properties;
    if (count)
        *count = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t *count, VkLayerProperties *properties)
{
    (void)properties;
    if (count)
        *count = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(
    VkInstance instance, uint32_t *count, VkPhysicalDevice *devices)
{
    if (!instance || !count)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (devices)
        devices[0] = &g_physical_device;
    *count = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(
    VkPhysicalDevice physical_device, VkPhysicalDeviceProperties *properties)
{
    (void)physical_device;
    if (!properties)
        return;
    memset(properties, 0, sizeof(*properties));
    properties->apiVersion = VK_API_VERSION_1_0;
    properties->driverVersion = 1;
    properties->vendorID = 0x5A4F;
    properties->deviceID = 1;
    properties->deviceType = VK_PHYSICAL_DEVICE_TYPE_CPU;
    copy_string(properties->deviceName, "Zhaoxu Software Vulkan",
                sizeof(properties->deviceName));
    properties->limits.maxImageDimension2D = 512;
    properties->limits.maxViewportDimensions[0] = 4096;
    properties->limits.maxViewportDimensions[1] = 4096;
    properties->limits.maxVertexInputAttributes = 8;
    properties->limits.maxVertexInputBindings = 1;
    properties->limits.maxVertexInputAttributeOffset = 256;
    properties->limits.maxVertexInputBindingStride = 256;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(
    VkPhysicalDevice physical_device, VkPhysicalDeviceFeatures *features)
{
    (void)physical_device;
    if (features)
        memset(features, 0, sizeof(*features));
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physical_device, VkPhysicalDeviceMemoryProperties *memory)
{
    (void)physical_device;
    if (!memory)
        return;
    memset(memory, 0, sizeof(*memory));
    memory->memoryTypeCount = 1;
    memory->memoryTypes[0].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    memory->memoryTypes[0].heapIndex = 0;
    memory->memoryHeapCount = 1;
    memory->memoryHeaps[0].size = 256u * 1024u * 1024u;
    memory->memoryHeaps[0].flags = 0;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physical_device, uint32_t *count,
    VkQueueFamilyProperties *properties)
{
    (void)physical_device;
    if (!count)
        return;
    if (properties) {
        memset(properties, 0, sizeof(*properties));
        properties[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT |
                                   VK_QUEUE_TRANSFER_BIT;
        properties[0].queueCount = 1;
        properties[0].timestampValidBits = 0;
    }
    *count = 1;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(
    VkPhysicalDevice physical_device, VkFormat format,
    VkFormatProperties *properties)
{
    (void)physical_device;
    (void)format;
    if (properties)
        memset(properties, 0, sizeof(*properties));
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice physical_device, const VkDeviceCreateInfo *create_info,
    const VkAllocationCallbacks *alloc, VkDevice *device)
{
    (void)physical_device;
    (void)create_info;
    (void)alloc;
    if (!device)
        return VK_ERROR_INITIALIZATION_FAILED;
    struct VkDevice_T *object = malloc(sizeof(*object));
    if (!object)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(object, 0, sizeof(*object));
    object->magic = VK_SOFT_MAGIC;
    object->queue = &g_queue;
    g_queue.magic = VK_SOFT_MAGIC;
    g_queue.device = object;
    *device = object;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device,
                                           const VkAllocationCallbacks *alloc)
{
    (void)alloc;
    if (device) {
        g_queue.device = 0;
        free(device);
    }
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice device,
                                            uint32_t queue_family_index,
                                            uint32_t queue_index,
                                            VkQueue *queue)
{
    (void)queue_family_index;
    (void)queue_index;
    if (queue)
        *queue = device ? ((struct VkDevice_T *)device)->queue : 0;
}

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device)
{
    (void)device;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue queue)
{
    (void)queue;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue, uint32_t count,
                                             const VkSubmitInfo *submits,
                                             VkFence fence)
{
    (void)queue;
    if (!submits && count)
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t c = 0; c < submits[i].commandBufferCount; c++)
            execute_command_buffer(
                (struct VkCommandBuffer_T *)submits[i].pCommandBuffers[c]);
    }
    if (fence)
        ((struct VkFence_T *)fence)->signaled = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(
    VkDevice device, const VkMemoryAllocateInfo *allocate_info,
    const VkAllocationCallbacks *alloc, VkDeviceMemory *memory)
{
    (void)device;
    (void)alloc;
    if (!allocate_info || allocate_info->allocationSize == 0 || !memory)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    struct VkDeviceMemory_T *object = malloc(sizeof(*object));
    if (!object)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    object->host = malloc((size_t)allocate_info->allocationSize);
    if (!object->host) {
        free(object);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memset(object->host, 0, (size_t)allocate_info->allocationSize);
    object->magic = VK_SOFT_MAGIC;
    object->size = allocate_info->allocationSize;
    *memory = object;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice device,
                                        VkDeviceMemory memory,
                                        const VkAllocationCallbacks *alloc)
{
    (void)device;
    (void)alloc;
    if (memory) {
        free(((struct VkDeviceMemory_T *)memory)->host);
        free(memory);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice device,
                                           VkDeviceMemory memory,
                                           VkDeviceSize offset,
                                           VkDeviceSize size,
                                           VkMemoryMapFlags flags,
                                           void **data)
{
    (void)device;
    (void)flags;
    if (!memory || !data)
        return VK_ERROR_MEMORY_MAP_FAILED;
    struct VkDeviceMemory_T *object = (struct VkDeviceMemory_T *)memory;
    if (offset > object->size)
        return VK_ERROR_MEMORY_MAP_FAILED;
    if (size == VK_WHOLE_SIZE)
        size = object->size - offset;
    if (offset + size > object->size)
        return VK_ERROR_MEMORY_MAP_FAILED;
    *data = (unsigned char *)object->host + offset;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice device,
                                         VkDeviceMemory memory)
{
    (void)device;
    (void)memory;
}

VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(
    VkDevice device, uint32_t count, const VkMappedMemoryRange *ranges)
{
    (void)device;
    (void)count;
    (void)ranges;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(
    VkDevice device, uint32_t count, const VkMappedMemoryRange *ranges)
{
    (void)device;
    (void)count;
    (void)ranges;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(
    VkDevice device, const VkBufferCreateInfo *create_info,
    const VkAllocationCallbacks *alloc, VkBuffer *buffer)
{
    (void)device;
    (void)alloc;
    if (!create_info || !buffer)
        return VK_ERROR_INITIALIZATION_FAILED;
    struct VkBuffer_T *object = malloc(sizeof(*object));
    if (!object)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(object, 0, sizeof(*object));
    object->magic = VK_SOFT_MAGIC;
    object->size = create_info->size;
    object->usage = create_info->usage;
    *buffer = object;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer,
                                           const VkAllocationCallbacks *alloc)
{
    (void)device;
    (void)alloc;
    free(buffer);
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(
    VkDevice device, VkBuffer buffer, VkMemoryRequirements *requirements)
{
    (void)device;
    if (!requirements)
        return;
    struct VkBuffer_T *object = (struct VkBuffer_T *)buffer;
    requirements->size = object ? object->size : 0;
    requirements->alignment = 64;
    requirements->memoryTypeBits = 1;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice device,
                                                  VkBuffer buffer,
                                                  VkDeviceMemory memory,
                                                  VkDeviceSize offset)
{
    (void)device;
    struct VkBuffer_T *object = (struct VkBuffer_T *)buffer;
    if (!object || !memory)
        return VK_ERROR_INITIALIZATION_FAILED;
    object->memory = memory;
    object->offset = offset;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(
    VkDevice device, const VkImageCreateInfo *create_info,
    const VkAllocationCallbacks *alloc, VkImage *image)
{
    (void)device;
    (void)alloc;
    if (!create_info || !image || create_info->extent.width == 0 ||
        create_info->extent.height == 0 ||
        create_info->extent.width > 512 || create_info->extent.height > 512)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (create_info->format != VK_FORMAT_R8G8B8A8_UNORM)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    struct VkImage_T *object = malloc(sizeof(*object));
    if (!object)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(object, 0, sizeof(*object));
    object->magic = VK_SOFT_MAGIC;
    object->width = create_info->extent.width;
    object->height = create_info->extent.height;
    object->format = create_info->format;
    object->usage = create_info->usage;
    object->layout = create_info->initialLayout;
    *image = object;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice device, VkImage image,
                                          const VkAllocationCallbacks *alloc)
{
    (void)device;
    (void)alloc;
    free(image);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(
    VkDevice device, VkImage image, VkMemoryRequirements *requirements)
{
    (void)device;
    if (!requirements)
        return;
    struct VkImage_T *object = (struct VkImage_T *)image;
    requirements->size = object
                             ? (VkDeviceSize)object->width * object->height * 4
                             : 0;
    requirements->alignment = 64;
    requirements->memoryTypeBits = 1;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice device,
                                                 VkImage image,
                                                 VkDeviceMemory memory,
                                                 VkDeviceSize offset)
{
    (void)device;
    struct VkImage_T *object = (struct VkImage_T *)image;
    if (!object || !memory)
        return VK_ERROR_INITIALIZATION_FAILED;
    struct VkDeviceMemory_T *allocation = (struct VkDeviceMemory_T *)memory;
    if (offset + object->width * object->height * 4 > allocation->size)
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    object->memory = memory;
    object->pixels = (unsigned char *)allocation->host + offset;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(
    VkDevice device, const VkImageViewCreateInfo *create_info,
    const VkAllocationCallbacks *alloc, VkImageView *view)
{
    (void)device;
    (void)alloc;
    if (!create_info || !view)
        return VK_ERROR_INITIALIZATION_FAILED;
    struct VkImageView_T *object = malloc(sizeof(*object));
    if (!object)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(object, 0, sizeof(*object));
    object->magic = VK_SOFT_MAGIC;
    object->image = create_info->image;
    object->type = create_info->viewType;
    *view = object;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice device,
                                              VkImageView view,
                                              const VkAllocationCallbacks *alloc)
{
    (void)device;
    (void)alloc;
    free(view);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(
    VkDevice device, const VkShaderModuleCreateInfo *create_info,
    const VkAllocationCallbacks *alloc, VkShaderModule *shader_module)
{
    (void)device;
    (void)alloc;
    if (!create_info || !shader_module || !create_info->pCode ||
        create_info->codeSize < 20 || create_info->codeSize % 4 != 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    struct VkShaderModule_T *object = malloc(sizeof(*object));
    if (!object)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(object, 0, sizeof(*object));
    object->magic = VK_SOFT_MAGIC;
    object->word_count = create_info->codeSize / 4;
    object->words = malloc(create_info->codeSize);
    if (!object->words) {
        free(object);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memcpy(object->words, create_info->pCode, create_info->codeSize);
    *shader_module = object;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(
    VkDevice device, VkShaderModule shader_module,
    const VkAllocationCallbacks *alloc)
{
    (void)device;
    (void)alloc;
    if (shader_module) {
        free(((struct VkShaderModule_T *)shader_module)->words);
        free(shader_module);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(
    VkDevice device, const VkRenderPassCreateInfo *create_info,
    const VkAllocationCallbacks *alloc, VkRenderPass *render_pass)
{
    (void)device;
    (void)alloc;
    if (!create_info || !render_pass || create_info->attachmentCount == 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    struct VkRenderPass_T *object = malloc(sizeof(*object));
    if (!object)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(object, 0, sizeof(*object));
    object->magic = VK_SOFT_MAGIC;
    object->format = create_info->pAttachments[0].format;
    *render_pass = object;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(VkDevice device,
                                               VkRenderPass render_pass,
                                               const VkAllocationCallbacks *alloc)
{
    (void)device;
    (void)alloc;
    free(render_pass);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(
    VkDevice device, const VkFramebufferCreateInfo *create_info,
    const VkAllocationCallbacks *alloc, VkFramebuffer *framebuffer)
{
    (void)device;
    (void)alloc;
    if (!create_info || !framebuffer || create_info->attachmentCount == 0)
        return VK_ERROR_INITIALIZATION_FAILED;
    struct VkFramebuffer_T *object = malloc(sizeof(*object));
    if (!object)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(object, 0, sizeof(*object));
    object->magic = VK_SOFT_MAGIC;
    object->view = create_info->pAttachments[0];
    object->width = create_info->width;
    object->height = create_info->height;
    *framebuffer = object;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(
    VkDevice device, VkFramebuffer framebuffer,
    const VkAllocationCallbacks *alloc)
{
    (void)device;
    (void)alloc;
    free(framebuffer);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(
    VkDevice device, const VkPipelineLayoutCreateInfo *create_info,
    const VkAllocationCallbacks *alloc, VkPipelineLayout *layout)
{
    (void)device;
    (void)create_info;
    (void)alloc;
    if (!layout)
        return VK_ERROR_INITIALIZATION_FAILED;
    struct VkPipelineLayout_T *object = malloc(sizeof(*object));
    if (!object)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(object, 0, sizeof(*object));
    object->magic = VK_SOFT_MAGIC;
    *layout = object;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(
    VkDevice device, VkPipelineLayout layout,
    const VkAllocationCallbacks *alloc)
{
    (void)device;
    (void)alloc;
    free(layout);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(
    VkDevice device, VkPipelineCache pipeline_cache, uint32_t count,
    const VkGraphicsPipelineCreateInfo *create_infos,
    const VkAllocationCallbacks *alloc, VkPipeline *pipelines)
{
    (void)device;
    (void)pipeline_cache;
    (void)alloc;
    if (!create_infos || !pipelines)
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < count; i++) {
        const VkGraphicsPipelineCreateInfo *info = &create_infos[i];
        if (!info->pVertexInputState || !info->pStages ||
            info->stageCount < 2)
            return VK_ERROR_INITIALIZATION_FAILED;
        struct VkPipeline_T *object = malloc(sizeof(*object));
        if (!object)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        memset(object, 0, sizeof(*object));
        object->magic = VK_SOFT_MAGIC;
        object->topology = info->pInputAssemblyState
                               ? info->pInputAssemblyState->topology
                               : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        const VkVertexInputBindingDescription *binding =
            info->pVertexInputState->pVertexBindingDescriptions;
        if (binding)
            object->binding = *binding;
        object->attribute_count = info->pVertexInputState->vertexAttributeDescriptionCount;
        if (object->attribute_count > 8)
            object->attribute_count = 8;
        for (unsigned int a = 0; a < object->attribute_count; a++)
            object->attributes[a] =
                info->pVertexInputState->pVertexAttributeDescriptions[a];

        if (info->pViewportState && info->pViewportState->pViewports &&
            info->pViewportState->viewportCount) {
            object->viewport = info->pViewportState->pViewports[0];
            object->has_viewport = 1;
        }
        if (info->pViewportState && info->pViewportState->pScissors &&
            info->pViewportState->scissorCount) {
            object->scissor = info->pViewportState->pScissors[0];
            object->has_scissor = 1;
        }
        if (info->pColorBlendState && info->pColorBlendState->attachmentCount &&
            info->pColorBlendState->pAttachments) {
            const VkPipelineColorBlendAttachmentState *attachment =
                &info->pColorBlendState->pAttachments[0];
            object->blend_enable = attachment->blendEnable;
            object->blend_src = attachment->srcColorBlendFactor;
            object->blend_dst = attachment->dstColorBlendFactor;
        }

        for (uint32_t stage = 0; stage < info->stageCount; stage++) {
            VkShaderModule module = info->pStages[stage].module;
            struct VkShaderModule_T *shader =
                (struct VkShaderModule_T *)module;
            if (!shader)
                continue;
            if (info->pStages[stage].stage == VK_SHADER_STAGE_VERTEX_BIT &&
                !shader->vertex_valid) {
                shader->vertex_valid =
                    spv_parse(&shader->vertex, shader->words,
                              shader->word_count, 0) == 0;
                object->vertex_shader = module;
            } else if (info->pStages[stage].stage ==
                           VK_SHADER_STAGE_FRAGMENT_BIT &&
                       !shader->fragment_valid) {
                shader->fragment_valid =
                    spv_parse(&shader->fragment, shader->words,
                              shader->word_count, 1) == 0;
                object->fragment_shader = module;
            }
        }

        struct VkShaderModule_T *vertex =
            (struct VkShaderModule_T *)object->vertex_shader;
        struct VkShaderModule_T *fragment =
            (struct VkShaderModule_T *)object->fragment_shader;
        if (!vertex || !vertex->vertex_valid || !fragment ||
            !fragment->fragment_valid ||
            object->topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) {
            free(object);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        pipelines[i] = object;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice device,
                                             VkPipeline pipeline,
                                             const VkAllocationCallbacks *alloc)
{
    (void)device;
    (void)alloc;
    free(pipeline);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(
    VkDevice device, const VkCommandPoolCreateInfo *create_info,
    const VkAllocationCallbacks *alloc, VkCommandPool *command_pool)
{
    (void)device;
    (void)create_info;
    (void)alloc;
    if (!command_pool)
        return VK_ERROR_INITIALIZATION_FAILED;
    struct VkCommandPool_T *object = malloc(sizeof(*object));
    if (!object)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(object, 0, sizeof(*object));
    object->magic = VK_SOFT_MAGIC;
    *command_pool = object;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(
    VkDevice device, VkCommandPool command_pool,
    const VkAllocationCallbacks *alloc)
{
    (void)device;
    (void)alloc;
    free(command_pool);
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(
    VkDevice device, const VkCommandBufferAllocateInfo *allocate_info,
    VkCommandBuffer *command_buffers)
{
    (void)device;
    if (!allocate_info || !command_buffers)
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < allocate_info->commandBufferCount; i++) {
        struct VkCommandBuffer_T *object = malloc(sizeof(*object));
        if (!object)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        memset(object, 0, sizeof(*object));
        object->magic = VK_SOFT_MAGIC;
        command_buffers[i] = object;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(
    VkDevice device, VkCommandPool command_pool, uint32_t count,
    const VkCommandBuffer *command_buffers)
{
    (void)device;
    (void)command_pool;
    for (uint32_t i = 0; i < count; i++)
        free(command_buffers[i]);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(
    VkCommandBuffer command_buffer,
    const VkCommandBufferBeginInfo *begin_info)
{
    (void)begin_info;
    if (!command_buffer)
        return VK_ERROR_INITIALIZATION_FAILED;
    struct VkCommandBuffer_T *object =
        (struct VkCommandBuffer_T *)command_buffer;
    object->recording = 1;
    object->command_count = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(
    VkCommandBuffer command_buffer)
{
    if (!command_buffer)
        return VK_ERROR_INITIALIZATION_FAILED;
    ((struct VkCommandBuffer_T *)command_buffer)->recording = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(
    VkCommandBuffer command_buffer, VkCommandBufferResetFlags flags)
{
    (void)flags;
    if (!command_buffer)
        return VK_ERROR_INITIALIZATION_FAILED;
    struct VkCommandBuffer_T *object =
        (struct VkCommandBuffer_T *)command_buffer;
    object->command_count = 0;
    object->recording = 0;
    return VK_SUCCESS;
}

/* Append one command, dropping it when the fixed recording buffer is full. */
static void append_command(struct VkCommandBuffer_T *command_buffer,
                           int type)
{
    if (!command_buffer || command_buffer->command_count >= 64)
        return;
    typeof(command_buffer->commands[0]) *command =
        &command_buffer->commands[command_buffer->command_count++];
    memset(command, 0, sizeof(*command));
    command->type = type;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(
    VkCommandBuffer command_buffer, const VkRenderPassBeginInfo *begin_info,
    VkSubpassContents contents)
{
    (void)contents;
    if (!command_buffer || !begin_info)
        return;
    struct VkCommandBuffer_T *object =
        (struct VkCommandBuffer_T *)command_buffer;
    append_command(object, VK_CMD_BEGIN_RENDER_PASS);
    if (!object->command_count)
        return;
    typeof(object->commands[0]) *command =
        &object->commands[object->command_count - 1];
    command->framebuffer = begin_info->framebuffer;
    command->render_pass = begin_info->renderPass;
    if (begin_info->pClearValues)
        for (int i = 0; i < 4; i++)
            command->clear[i] = begin_info->pClearValues[0].color.float32[i];
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer command_buffer)
{
    append_command((struct VkCommandBuffer_T *)command_buffer,
                   VK_CMD_END_RENDER_PASS);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(
    VkCommandBuffer command_buffer, VkPipelineBindPoint bind_point,
    VkPipeline pipeline)
{
    (void)bind_point;
    if (!command_buffer)
        return;
    struct VkCommandBuffer_T *object =
        (struct VkCommandBuffer_T *)command_buffer;
    append_command(object, VK_CMD_BIND_PIPELINE);
    if (object->command_count)
        object->commands[object->command_count - 1].pipeline = pipeline;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(
    VkCommandBuffer command_buffer, uint32_t first_binding,
    uint32_t binding_count, const VkBuffer *buffers,
    const VkDeviceSize *offsets)
{
    (void)first_binding;
    if (!command_buffer || !buffers || !offsets || binding_count == 0)
        return;
    struct VkCommandBuffer_T *object =
        (struct VkCommandBuffer_T *)command_buffer;
    append_command(object, VK_CMD_BIND_VERTEX_BUFFER);
    if (object->command_count) {
        object->commands[object->command_count - 1].buffer = buffers[0];
        object->commands[object->command_count - 1].offset = offsets[0];
    }
}

VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer command_buffer,
                                     uint32_t vertex_count,
                                     uint32_t instance_count,
                                     uint32_t first_vertex,
                                     uint32_t first_instance)
{
    (void)instance_count;
    (void)first_instance;
    if (!command_buffer)
        return;
    struct VkCommandBuffer_T *object =
        (struct VkCommandBuffer_T *)command_buffer;
    append_command(object, VK_CMD_DRAW);
    if (object->command_count) {
        object->commands[object->command_count - 1].vertex_count = vertex_count;
        object->commands[object->command_count - 1].first_vertex = first_vertex;
    }
}

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(
    VkCommandBuffer command_buffer, VkPipelineStageFlags source_stage,
    VkPipelineStageFlags destination_stage, VkDependencyFlags dependencies,
    uint32_t memory_count, const VkMemoryBarrier *memory_barriers,
    uint32_t buffer_count, const VkBufferMemoryBarrier *buffer_barriers,
    uint32_t image_count, const VkImageMemoryBarrier *image_barriers)
{
    (void)command_buffer;
    (void)source_stage;
    (void)destination_stage;
    (void)dependencies;
    (void)memory_count;
    (void)memory_barriers;
    (void)buffer_count;
    (void)buffer_barriers;
    (void)image_count;
    (void)image_barriers;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(
    VkDevice device, const VkFenceCreateInfo *create_info,
    const VkAllocationCallbacks *alloc, VkFence *fence)
{
    (void)device;
    (void)alloc;
    if (!fence)
        return VK_ERROR_INITIALIZATION_FAILED;
    struct VkFence_T *object = malloc(sizeof(*object));
    if (!object)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(object, 0, sizeof(*object));
    object->magic = VK_SOFT_MAGIC;
    object->flags = create_info ? create_info->flags : 0;
    object->signaled =
        (object->flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0;
    *fence = object;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice device, VkFence fence,
                                          const VkAllocationCallbacks *alloc)
{
    (void)device;
    (void)alloc;
    free(fence);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice device, VkFence fence)
{
    (void)device;
    if (!fence)
        return VK_ERROR_INITIALIZATION_FAILED;
    return ((struct VkFence_T *)fence)->signaled ? VK_SUCCESS : VK_NOT_READY;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice device, uint32_t count,
                                             const VkFence *fences)
{
    (void)device;
    for (uint32_t i = 0; i < count; i++)
        ((struct VkFence_T *)fences[i])->signaled = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice device,
                                               uint32_t count,
                                               const VkFence *fences,
                                               VkBool32 wait_all,
                                               uint64_t timeout)
{
    (void)device;
    (void)wait_all;
    (void)timeout;
    for (uint32_t i = 0; i < count; i++)
        ((struct VkFence_T *)fences[i])->signaled = 1;
    return VK_SUCCESS;
}

/* Resolve the small set of entry points the software ICD implements. */
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance, const char *name)
{
    (void)instance;
    if (!name)
        return 0;
#define VK_RETURN(fn) \
    if (same_string(name, "vk" #fn)) return (PFN_vkVoidFunction)vk##fn
    VK_RETURN(CreateInstance);
    VK_RETURN(DestroyInstance);
    VK_RETURN(EnumerateInstanceVersion);
    VK_RETURN(EnumerateInstanceExtensionProperties);
    VK_RETURN(EnumerateInstanceLayerProperties);
    VK_RETURN(EnumeratePhysicalDevices);
    VK_RETURN(GetPhysicalDeviceProperties);
    VK_RETURN(GetPhysicalDeviceFeatures);
    VK_RETURN(GetPhysicalDeviceMemoryProperties);
    VK_RETURN(GetPhysicalDeviceQueueFamilyProperties);
    VK_RETURN(CreateDevice);
    VK_RETURN(DestroyDevice);
    VK_RETURN(GetDeviceQueue);
    VK_RETURN(DeviceWaitIdle);
    VK_RETURN(QueueWaitIdle);
    VK_RETURN(QueueSubmit);
    VK_RETURN(AllocateMemory);
    VK_RETURN(FreeMemory);
    VK_RETURN(MapMemory);
    VK_RETURN(UnmapMemory);
    VK_RETURN(CreateBuffer);
    VK_RETURN(DestroyBuffer);
    VK_RETURN(GetBufferMemoryRequirements);
    VK_RETURN(BindBufferMemory);
    VK_RETURN(CreateImage);
    VK_RETURN(DestroyImage);
    VK_RETURN(GetImageMemoryRequirements);
    VK_RETURN(BindImageMemory);
    VK_RETURN(CreateImageView);
    VK_RETURN(DestroyImageView);
    VK_RETURN(CreateShaderModule);
    VK_RETURN(DestroyShaderModule);
    VK_RETURN(CreateRenderPass);
    VK_RETURN(DestroyRenderPass);
    VK_RETURN(CreateFramebuffer);
    VK_RETURN(DestroyFramebuffer);
    VK_RETURN(CreatePipelineLayout);
    VK_RETURN(DestroyPipelineLayout);
    VK_RETURN(CreateGraphicsPipelines);
    VK_RETURN(DestroyPipeline);
    VK_RETURN(CreateCommandPool);
    VK_RETURN(DestroyCommandPool);
    VK_RETURN(AllocateCommandBuffers);
    VK_RETURN(FreeCommandBuffers);
    VK_RETURN(BeginCommandBuffer);
    VK_RETURN(EndCommandBuffer);
    VK_RETURN(CmdBeginRenderPass);
    VK_RETURN(CmdEndRenderPass);
    VK_RETURN(CmdBindPipeline);
    VK_RETURN(CmdBindVertexBuffers);
    VK_RETURN(CmdDraw);
    VK_RETURN(CmdPipelineBarrier);
    VK_RETURN(CreateFence);
    VK_RETURN(DestroyFence);
    VK_RETURN(GetFenceStatus);
    VK_RETURN(ResetFences);
    VK_RETURN(WaitForFences);
    VK_RETURN(GetInstanceProcAddr);
#undef VK_RETURN
    return 0;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice device, const char *name)
{
    (void)device;
    return vkGetInstanceProcAddr(0, name);
}
