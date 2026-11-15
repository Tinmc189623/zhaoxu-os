/*
 * Zhaoxu OS - software Vulkan ICD regression.
 *
 * Creates a real Vulkan instance and device, renders an RGB triangle into a
 * 64x64 R8G8B8A8_UNORM image through the SPIR-V interpreter, submits the
 * command buffer and reads the mapped image back.
 */
#include <vulkan/vulkan.h>

#include "lib/vulkan/test_shaders.h"
#include "userlib.h"

#define IMAGE_SIZE 64

struct vertex {
    float position[2];
    float color[3];
};

/* Read one RGBA pixel from the mapped image. */
static void read_pixel(const unsigned char *pixels, int x, int y, float out[4])
{
    const unsigned char *pixel = &pixels[(y * IMAGE_SIZE + x) * 4];
    for (int i = 0; i < 4; i++)
        out[i] = (float)pixel[i] / 255.0f;
}

/* Check a pixel against an expected colour with a small tolerance. */
static int near_color(const unsigned char *pixels, int x, int y, float red,
                      float green, float blue, float tolerance)
{
    float color[4];
    read_pixel(pixels, x, y, color);
    float dr = color[0] - red;
    float dg = color[1] - green;
    float db = color[2] - blue;
    if (dr < 0) dr = -dr;
    if (dg < 0) dg = -dg;
    if (db < 0) db = -db;
    return dr <= tolerance && dg <= tolerance && db <= tolerance;
}

/* Build the instance, one CPU device and its graphics queue. */
static int create_device(VkInstance *instance, VkDevice *device, VkQueue *queue)
{
    VkApplicationInfo application = {0};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "vk_test";
    application.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instance_info = {0};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &application;
    if (vkCreateInstance(&instance_info, VK_NULL_HANDLE, instance) !=
        VK_SUCCESS)
        return 0;

    uint32_t physical_count = 0;
    if (vkEnumeratePhysicalDevices(*instance, &physical_count,
                                   VK_NULL_HANDLE) != VK_SUCCESS ||
        physical_count != 1)
        return 0;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    if (vkEnumeratePhysicalDevices(*instance, &physical_count, &physical) !=
        VK_SUCCESS)
        return 0;

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    u_puts("[vk ] device ");
    u_puts(properties.deviceName);
    u_puts("\n");

    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count,
                                             VK_NULL_HANDLE);
    if (family_count < 1)
        return 0;
    VkQueueFamilyProperties families[2];
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count,
                                             families);
    if (!(families[0].queueFlags & VK_QUEUE_GRAPHICS_BIT))
        return 0;

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {0};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = 0;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    VkDeviceCreateInfo device_info = {0};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    if (vkCreateDevice(physical, &device_info, VK_NULL_HANDLE, device) !=
        VK_SUCCESS)
        return 0;
    vkGetDeviceQueue(*device, 0, 0, queue);
    return *queue != VK_NULL_HANDLE;
}

/* Render the triangle and verify the readback; returns 0 on success. */
static int render_triangle(VkDevice device, VkQueue queue)
{
    VkImageCreateInfo image_info = {0};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.extent.width = IMAGE_SIZE;
    image_info.extent.height = IMAGE_SIZE;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image = VK_NULL_HANDLE;
    if (vkCreateImage(device, &image_info, VK_NULL_HANDLE, &image) != VK_SUCCESS)
        return 0;

    VkMemoryRequirements image_requirements;
    vkGetImageMemoryRequirements(device, image, &image_requirements);
    VkMemoryAllocateInfo image_allocation = {0};
    image_allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    image_allocation.allocationSize = image_requirements.size;
    image_allocation.memoryTypeIndex = 0;
    VkDeviceMemory image_memory = VK_NULL_HANDLE;
    if (vkAllocateMemory(device, &image_allocation, VK_NULL_HANDLE,
                         &image_memory) != VK_SUCCESS ||
        vkBindImageMemory(device, image, image_memory, 0) != VK_SUCCESS)
        return 0;

    VkImageViewCreateInfo view_info = {0};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(device, &view_info, VK_NULL_HANDLE, &view) !=
        VK_SUCCESS)
        return 0;

    VkAttachmentDescription attachment = {0};
    attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference color_reference = {0};
    color_reference.attachment = 0;
    color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_reference;

    VkRenderPassCreateInfo render_pass_info = {0};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &render_pass_info, VK_NULL_HANDLE,
                           &render_pass) != VK_SUCCESS)
        return 0;

    VkFramebufferCreateInfo framebuffer_info = {0};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = render_pass;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments = &view;
    framebuffer_info.width = IMAGE_SIZE;
    framebuffer_info.height = IMAGE_SIZE;
    framebuffer_info.layers = 1;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(device, &framebuffer_info, VK_NULL_HANDLE,
                            &framebuffer) != VK_SUCCESS)
        return 0;

    VkShaderModuleCreateInfo vertex_info = {0};
    vertex_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertex_info.codeSize = vk_test_vertex_spirv_count * 4;
    vertex_info.pCode = vk_test_vertex_spirv;
    VkShaderModule vertex_shader = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &vertex_info, VK_NULL_HANDLE,
                             &vertex_shader) != VK_SUCCESS)
        return 0;

    VkShaderModuleCreateInfo fragment_info = {0};
    fragment_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragment_info.codeSize = vk_test_fragment_spirv_count * 4;
    fragment_info.pCode = vk_test_fragment_spirv;
    VkShaderModule fragment_shader = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &fragment_info, VK_NULL_HANDLE,
                             &fragment_shader) != VK_SUCCESS)
        return 0;

    VkPipelineShaderStageCreateInfo stages[2] = {{0}};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex_shader;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment_shader;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding = {0};
    binding.binding = 0;
    binding.stride = sizeof(struct vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attributes[2] = {{0}};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[0].offset = 0;
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = 8;

    VkPipelineVertexInputStateCreateInfo vertex_input = {0};
    vertex_input.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = 2;
    vertex_input.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {0};
    input_assembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport = {0, 0, IMAGE_SIZE, IMAGE_SIZE, 0, 1};
    VkRect2D scissor = {{0, 0}, {IMAGE_SIZE, IMAGE_SIZE}};
    VkPipelineViewportStateCreateInfo viewport_state = {0};
    viewport_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterization = {0};
    rasterization.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1;

    VkPipelineMultisampleStateCreateInfo multisample = {0};
    multisample.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_attachment = {0};
    blend_attachment.blendEnable = VK_FALSE;
    blend_attachment.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo color_blend = {0};
    color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &blend_attachment;

    VkPipelineLayoutCreateInfo layout_info = {0};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device, &layout_info, VK_NULL_HANDLE, &layout) !=
        VK_SUCCESS)
        return 0;

    VkGraphicsPipelineCreateInfo pipeline_info = {0};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterization;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &color_blend;
    pipeline_info.layout = layout;
    pipeline_info.renderPass = render_pass;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info,
                                  VK_NULL_HANDLE, &pipeline) != VK_SUCCESS)
        return 0;

    struct vertex vertices[3] = {
        {{0.0f, 0.8f}, {1, 0, 0}},
        {{0.693f, -0.4f}, {0, 1, 0}},
        {{-0.693f, -0.4f}, {0, 0, 1}},
    };
    VkBufferCreateInfo buffer_info = {0};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = sizeof(vertices);
    buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    if (vkCreateBuffer(device, &buffer_info, VK_NULL_HANDLE, &vertex_buffer) !=
        VK_SUCCESS)
        return 0;
    VkMemoryRequirements buffer_requirements;
    vkGetBufferMemoryRequirements(device, vertex_buffer, &buffer_requirements);
    VkMemoryAllocateInfo buffer_allocation = {0};
    buffer_allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    buffer_allocation.allocationSize = buffer_requirements.size;
    buffer_allocation.memoryTypeIndex = 0;
    VkDeviceMemory buffer_memory = VK_NULL_HANDLE;
    if (vkAllocateMemory(device, &buffer_allocation, VK_NULL_HANDLE,
                         &buffer_memory) != VK_SUCCESS ||
        vkBindBufferMemory(device, vertex_buffer, buffer_memory, 0) !=
            VK_SUCCESS)
        return 0;
    void *mapped = VK_NULL_HANDLE;
    if (vkMapMemory(device, buffer_memory, 0, VK_WHOLE_SIZE, 0, &mapped) !=
        VK_SUCCESS)
        return 0;
    unsigned char *destination = mapped;
    const unsigned char *source = (const unsigned char *)vertices;
    for (unsigned long i = 0; i < sizeof(vertices); i++)
        destination[i] = source[i];
    vkUnmapMemory(device, buffer_memory);

    VkCommandPoolCreateInfo pool_info = {0};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device, &pool_info, VK_NULL_HANDLE, &pool) !=
        VK_SUCCESS)
        return 0;
    VkCommandBufferAllocateInfo allocation_info = {0};
    allocation_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocation_info.commandPool = pool;
    allocation_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation_info.commandBufferCount = 1;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &allocation_info, &command_buffer) !=
        VK_SUCCESS)
        return 0;

    VkCommandBufferBeginInfo begin_info = {0};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(command_buffer, &begin_info);
    VkClearValue clear = {0};
    clear.color.float32[0] = 0;
    clear.color.float32[1] = 0;
    clear.color.float32[2] = 0;
    clear.color.float32[3] = 1;
    VkRenderPassBeginInfo pass_begin = {0};
    pass_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass_begin.renderPass = render_pass;
    pass_begin.framebuffer = framebuffer;
    pass_begin.renderArea.extent.width = IMAGE_SIZE;
    pass_begin.renderArea.extent.height = IMAGE_SIZE;
    pass_begin.clearValueCount = 1;
    pass_begin.pClearValues = &clear;
    vkCmdBeginRenderPass(command_buffer, &pass_begin,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer, &offset);
    vkCmdDraw(command_buffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(command_buffer);
    vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submit = {0};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_buffer;
    if (vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
        return 0;
    vkQueueWaitIdle(queue);

    void *pixels = VK_NULL_HANDLE;
    if (vkMapMemory(device, image_memory, 0, VK_WHOLE_SIZE, 0, &pixels) !=
        VK_SUCCESS)
        return 0;
    const unsigned char *readback = pixels;
    u_puts("[vk ] image readback\n");
    int ok = near_color(readback, 2, 2, 0, 0, 0, 0.02f) &&
             near_color(readback, IMAGE_SIZE / 2, IMAGE_SIZE / 2,
                        0.333f, 0.333f, 0.333f, 0.08f);
    if (!ok) {
        const unsigned char *center =
            &readback[((IMAGE_SIZE / 2) * IMAGE_SIZE + IMAGE_SIZE / 2) * 4];
        u_puts("[vk ] center rgba ");
        for (int i = 0; i < 4; i++) {
            u_putn(center[i]);
            u_puts(" ");
        }
        u_puts("\n");
    }
    vkUnmapMemory(device, image_memory);

    vkFreeCommandBuffers(device, pool, 1, &command_buffer);
    vkDestroyCommandPool(device, pool, VK_NULL_HANDLE);
    vkFreeMemory(device, buffer_memory, VK_NULL_HANDLE);
    vkDestroyBuffer(device, vertex_buffer, VK_NULL_HANDLE);
    vkDestroyPipeline(device, pipeline, VK_NULL_HANDLE);
    vkDestroyPipelineLayout(device, layout, VK_NULL_HANDLE);
    vkDestroyShaderModule(device, fragment_shader, VK_NULL_HANDLE);
    vkDestroyShaderModule(device, vertex_shader, VK_NULL_HANDLE);
    vkDestroyFramebuffer(device, framebuffer, VK_NULL_HANDLE);
    vkDestroyRenderPass(device, render_pass, VK_NULL_HANDLE);
    vkDestroyImageView(device, view, VK_NULL_HANDLE);
    vkFreeMemory(device, image_memory, VK_NULL_HANDLE);
    vkDestroyImage(device, image, VK_NULL_HANDLE);
    return ok;
}

int user_main(long *stack)
{
    (void)stack;
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    if (!create_device(&instance, &device, &queue)) {
        u_puts("[vk ] device creation FAIL\n");
        return 1;
    }
    u_puts("[vk ] instance, physical device, queue PASS\n");

    if (!render_triangle(device, queue)) {
        u_puts("[vk ] triangle FAIL\n");
        return 1;
    }
    u_puts("[vk ] triangle PASS\n");

    vkDeviceWaitIdle(device);
    vkDestroyDevice(device, VK_NULL_HANDLE);
    vkDestroyInstance(instance, VK_NULL_HANDLE);
    u_puts("[vk ] Vulkan PASS\n");
    return 0;
}
