//! Vulkan rendering backend — full implementation.
//!
//! Implements the framebuffer-optional backend interface with runtime
//! dlopen'd Vulkan. Partial-init safe via `InitState` state machine.
//!
//! ## Init State Machine
//!
//! ```text
//! none (0)
//!   → instance_created (1)  — VkInstance created
//!   → surface_created (2)   — VkSurfaceKHR created
//!   → device_created (3)    — VkDevice + VkQueue created
//!   → swapchain_created (4) — VkSwapchainKHR + images + views + framebuffers
//!   → pipeline_created (5)  — VkRenderPass + pipeline layout + pipeline
//!   → ready (6)             — command pool + buffers + semaphores + fence
//! ```
//!
//! `deinit` walks backward from the current state, destroying only what
//! was allocated. At `.none` it is a no-op.
//!
//! ## Frame Lifecycle
//!
//! Command buffers are re-recorded every frame (recorded in `beginFrame` /
//! `endFrame`) rather than pre-recorded at init time. This allows per-frame
//! clear colors and future dynamic vertex data.

const std = @import("std");
const testing = std.testing;

const Color = @import("../core/color.zig").Color;
const EngineError = @import("../core/error.zig").Error;
const RenderConfig = @import("renderer.zig").RenderConfig;
const Vertex = @import("renderer.zig").Vertex;
const vk = @import("vk.zig");
const vk_loader = @import("vk_loader.zig");
const createXlibSurface = @import("vulkan_xlib.zig").createSurface;
const x11 = @import("../platform/x11.zig");
const platform = @import("../platform/window.zig");
const Window = platform.Window;

// ---------------------------------------------------------------------------
// Shader SPIR-V data
// ---------------------------------------------------------------------------

const vertex_spv = @import("../shaders/triangle_vert.zig").VERTEX_SPV;
const fragment_spv = @import("../shaders/triangle_frag.zig").FRAGMENT_SPV;

// ---------------------------------------------------------------------------
// Queue flag constants — used for device selection
// ---------------------------------------------------------------------------

const VK_QUEUE_GRAPHICS_BIT: u32 = 1 << 0;

// ---------------------------------------------------------------------------
// InitState
// ---------------------------------------------------------------------------

/// Tracks which Vulkan resources have been initialized so `deinit` can
/// safely clean up even after a partial init failure.
pub const InitState = enum(u32) {
    none = 0,
    instance_created = 1,
    surface_created = 2,
    device_created = 3,
    swapchain_created = 4,
    pipeline_created = 5,
    ready = 6,

    fn reached(self: InitState, threshold: InitState) bool {
        return @intFromEnum(self) >= @intFromEnum(threshold);
    }
};

// ---------------------------------------------------------------------------
// VulkanBackend
// ---------------------------------------------------------------------------

pub const VulkanBackend = struct {
    allocator: std.mem.Allocator,
    config: RenderConfig,
    window_ptr: *Window,
    init_state: InitState,

    loader: vk_loader.VkFunctions,
    instance: vk.VkInstance,
    surface: vk.VkSurfaceKHR,
    physical_device: vk.VkPhysicalDevice,
    device: vk.VkDevice,
    queue: vk.VkQueue,
    queue_family_index: u32,
    swapchain: vk.VkSwapchainKHR,
    swapchain_format: vk.VkFormat,
    swapchain_extent: vk.VkExtent2D,
    swapchain_images: []vk.VkImage,
    swapchain_image_views: []vk.VkImageView,
    swapchain_framebuffers: []vk.VkFramebuffer,
    render_pass: vk.VkRenderPass,
    pipeline_layout: vk.VkPipelineLayout,
    pipeline: vk.VkPipeline,
    command_pool: vk.VkCommandPool,
    command_buffers: []vk.VkCommandBuffer,
    acquire_semaphore: vk.VkSemaphore,
    render_semaphore: vk.VkSemaphore,
    in_flight_fence: vk.VkFence,
    current_image: u32,

    // ------------------------------------------------------------------
    // init
    // ------------------------------------------------------------------

    /// Initialise the Vulkan backend: dlopen → instance → surface →
    /// device → swapchain → pipeline → command buffers → sync.
    ///
    /// Any step may return an error, in which case partial resources
    /// are cleaned up before the error propagates.
    ///
    pub fn init(
        allocator: std.mem.Allocator,
        window: *Window,
        width: u32,
        height: u32,
    ) (EngineError || error{VulkanSurfaceNotSupported})!VulkanBackend {
        var self = VulkanBackend{
            .allocator = allocator,
            .config = .{ .width = width, .height = height },
            .window_ptr = window,
            .init_state = .none,
            .loader = undefined,
            .instance = undefined,
            .surface = undefined,
            .physical_device = undefined,
            .device = undefined,
            .queue = undefined,
            .queue_family_index = undefined,
            .swapchain = undefined,
            .swapchain_format = undefined,
            .swapchain_extent = undefined,
            .swapchain_images = undefined,
            .swapchain_image_views = undefined,
            .swapchain_framebuffers = undefined,
            .render_pass = undefined,
            .pipeline_layout = undefined,
            .pipeline = undefined,
            .command_pool = undefined,
            .command_buffers = undefined,
            .acquire_semaphore = undefined,
            .render_semaphore = undefined,
            .in_flight_fence = undefined,
            .current_image = 0,
        };
        errdefer self.deinit();

        // ---- Step 1: load Vulkan library ----
        self.loader = try vk_loader.VkFunctions.load();

        // ---- Step 2: create instance ----
        {
            const app_info = vk.VkApplicationInfo{
                .sType = .application_info,
                .pNext = null,
                .pApplicationName = "Infinity Engine",
                .applicationVersion = 1,
                .pEngineName = "Infinity Engine",
                .engineVersion = 1,
                .apiVersion = vk.VK_API_VERSION_1_0,
            };

            const extensions = [_][*:0]const u8{ "VK_KHR_surface", "VK_KHR_xlib_surface" };

            const create_info = vk.VkInstanceCreateInfo{
                .sType = .instance_create_info,
                .pNext = null,
                .flags = 0,
                .pApplicationInfo = &app_info,
                .enabledLayerCount = 0,
                .ppEnabledLayerNames = null,
                .enabledExtensionCount = @intCast(extensions.len),
                .ppEnabledExtensionNames = @constCast(@ptrCast(&extensions)),
            };

            var instance: ?vk.VkInstance = undefined;
            const result = self.loader.vkCreateInstance(&create_info, null, &instance);
            if (result != .success) return EngineError.VulkanInitFailed;
            self.instance = instance.?;

            // Resolve all instance-level function pointers
            // (extension functions like vkCreateXlibSurfaceKHR etc.)
            self.loader.resolveInstanceFunctions(self.instance);

            self.init_state = .instance_created;
        }

        // ---- Step 3: create Xlib surface ----
        {
            self.surface = try createXlibSurface(self.instance, window.display, window.handle, &self.loader);
            self.init_state = .surface_created;
        }

        // ---- Step 4: enumerate physical devices & create logical device ----
        {
            var pd_count: u32 = 0;
            _ = self.loader.vkEnumeratePhysicalDevices(self.instance, &pd_count, null);
            if (pd_count == 0) return EngineError.VulkanInitFailed;

            const physical_devices = try allocator.alloc(vk.VkPhysicalDevice, pd_count);
            defer allocator.free(physical_devices);

            _ = self.loader.vkEnumeratePhysicalDevices(self.instance, &pd_count, physical_devices.ptr);

            var chosen_device: ?vk.VkPhysicalDevice = null;
            var chosen_queue_family: u32 = undefined;

            for (physical_devices) |pd| {
                var qf_count: u32 = 0;
                self.loader.vkGetPhysicalDeviceQueueFamilyProperties(pd, &qf_count, null);
                if (qf_count == 0) continue;

                const qf_props = try allocator.alloc(vk.VkQueueFamilyProperties, qf_count);
                defer allocator.free(qf_props);

                self.loader.vkGetPhysicalDeviceQueueFamilyProperties(pd, &qf_count, @ptrCast(qf_props.ptr));

                for (qf_props, 0..) |props, i| {
                    const qf_idx: u32 = @intCast(i);
                    const has_graphics = (props.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
                    if (!has_graphics) continue;

                    var supported: vk.VkBool32 = 0;
                    const sr = self.loader.vkGetPhysicalDeviceSurfaceSupportKHR(pd, qf_idx, self.surface, &supported);
                    if (sr != .success or supported == 0) continue;

                    chosen_device = pd;
                    chosen_queue_family = qf_idx;
                    break;
                }
                if (chosen_device != null) break;
            }

            self.physical_device = chosen_device orelse return EngineError.VulkanInitFailed;
            self.queue_family_index = chosen_queue_family;

            const priority = [_]f32{1.0};
            const queue_create_info = vk.VkDeviceQueueCreateInfo{
                .sType = .device_queue_create_info,
                .pNext = null,
                .flags = 0,
                .queueFamilyIndex = chosen_queue_family,
                .queueCount = 1,
                .pQueuePriorities = &priority,
            };

            const device_extensions = [_][*:0]const u8{ "VK_KHR_swapchain" };

            const device_create_info = vk.VkDeviceCreateInfo{
                .sType = .device_create_info,
                .pNext = null,
                .flags = 0,
                .queueCreateInfoCount = 1,
                .pQueueCreateInfos = @ptrCast(&queue_create_info),
                .enabledLayerCount = 0,
                .ppEnabledLayerNames = null,
                .enabledExtensionCount = @intCast(device_extensions.len),
                .ppEnabledExtensionNames = @constCast(@ptrCast(&device_extensions)),
                .pEnabledFeatures = null,
            };

            var device: ?vk.VkDevice = undefined;
            const dr = self.loader.vkCreateDevice(self.physical_device, &device_create_info, null, &device);
            if (dr != .success) return EngineError.VulkanInitFailed;
            self.device = device.?;

            var queue: ?vk.VkQueue = undefined;
            self.loader.vkGetDeviceQueue(self.device, chosen_queue_family, 0, &queue);
            self.queue = queue.?;
            self.init_state = .device_created;
        }

        // ---- Step 5: create swapchain + image views + framebuffers ----
        {
            var caps: vk.VkSurfaceCapabilitiesKHR = undefined;
            {
                const sr = self.loader.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                    self.physical_device,
                    self.surface,
                    &caps,
                );
                if (sr != .success) return EngineError.VulkanInitFailed;
            }

            const extent = if (caps.currentExtent.width != std.math.maxInt(u32))
                caps.currentExtent
            else
                vk.VkExtent2D{
                    .width = @max(caps.minImageExtent.width, @min(caps.maxImageExtent.width, self.config.width)),
                    .height = @max(caps.minImageExtent.height, @min(caps.maxImageExtent.height, self.config.height)),
                };

            const image_count = blk: {
                const base = caps.minImageCount + 1;
                break :blk if (caps.maxImageCount > 0 and base > caps.maxImageCount) caps.maxImageCount else base;
            };

            const chosen_format = blk: {
                var fmt_count: u32 = 0;
                _ = self.loader.vkGetPhysicalDeviceSurfaceFormatsKHR(self.physical_device, self.surface, &fmt_count, null);
                const formats = try allocator.alloc(vk.VkSurfaceFormatKHR, fmt_count);
                defer allocator.free(formats);
                _ = self.loader.vkGetPhysicalDeviceSurfaceFormatsKHR(self.physical_device, self.surface, &fmt_count, formats.ptr);

                for (formats) |f| {
                    if (f.format == .b8g8r8a8_srgb) break :blk f;
                }
                break :blk formats[0];
            };

            const present_mode = blk: {
                var pm_count: u32 = 0;
                _ = self.loader.vkGetPhysicalDeviceSurfacePresentModesKHR(self.physical_device, self.surface, &pm_count, null);
                const modes = try allocator.alloc(vk.VkPresentModeKHR, pm_count);
                defer allocator.free(modes);
                _ = self.loader.vkGetPhysicalDeviceSurfacePresentModesKHR(self.physical_device, self.surface, &pm_count, modes.ptr);

                for (modes) |pm| {
                    if (pm == .mailbox_khr) break :blk pm;
                }
                break :blk vk.VkPresentModeKHR.fifo_khr;
            };

            const swapchain_create_info = vk.VkSwapchainCreateInfoKHR{
                .sType = .swapchain_create_info_khr,
                .pNext = null,
                .flags = 0,
                .surface = self.surface,
                .minImageCount = image_count,
                .imageFormat = chosen_format.format,
                .imageColorSpace = chosen_format.colorSpace,
                .imageExtent = extent,
                .imageArrayLayers = 1,
                .imageUsage = @intFromEnum(vk.VkImageUsageFlagBits.color_attachment),
                .imageSharingMode = .exclusive,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = null,
                .preTransform = caps.currentTransform,
                .compositeAlpha = .opaque_khr,
                .presentMode = present_mode,
                .clipped = 1,
                .oldSwapchain = null,
            };

            var swapchain: ?vk.VkSwapchainKHR = undefined;
            {
                const sr = self.loader.vkCreateSwapchainKHR(self.device, &swapchain_create_info, null, &swapchain);
                if (sr != .success) return EngineError.VulkanInitFailed;
            }
            self.swapchain = swapchain.?;
            self.swapchain_format = chosen_format.format;
            self.swapchain_extent = extent;

            var sc_image_count: u32 = 0;
            _ = self.loader.vkGetSwapchainImagesKHR(self.device, self.swapchain, &sc_image_count, null);

            self.swapchain_images = try allocator.alloc(vk.VkImage, sc_image_count);
            {
                const sr = self.loader.vkGetSwapchainImagesKHR(self.device, self.swapchain, &sc_image_count, @ptrCast(self.swapchain_images.ptr));
                if (sr != .success) return EngineError.VulkanInitFailed;
            }

            // Create image views
            self.swapchain_image_views = try allocator.alloc(vk.VkImageView, sc_image_count);
            for (self.swapchain_images, 0..) |image, i| {
                const subresource_range = vk.VkImageSubresourceRange{
                    .aspectMask = @intFromEnum(vk.VkImageAspectFlagBits.color),
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                };

                const view_create_info = vk.VkImageViewCreateInfo{
                    .sType = .image_view_create_info,
                    .pNext = null,
                    .flags = 0,
                    .image = image,
                    .viewType = .image_2d,
                    .format = chosen_format.format,
                    .components = .{},
                    .subresourceRange = subresource_range,
                };

                var view: ?vk.VkImageView = undefined;
                const vr = self.loader.vkCreateImageView(self.device, &view_create_info, null, &view);
                if (vr != .success) return EngineError.VulkanInitFailed;
                self.swapchain_image_views[i] = view.?;
            }

            self.swapchain_framebuffers = try allocator.alloc(vk.VkFramebuffer, sc_image_count);
            self.init_state = .swapchain_created;
        }

        // ---- Step 6: create render pass + pipeline layout + pipeline + framebuffers ----
        {
            // Render pass
            const color_attachment = vk.VkAttachmentDescription{
                .flags = 0,
                .format = self.swapchain_format,
                .samples = .count_1,
                .loadOp = .clear,
                .storeOp = .store,
                .stencilLoadOp = .dont_care,
                .stencilStoreOp = .dont_care,
                .initialLayout = .undefined,
                .finalLayout = .present_src_khr,
            };

            const color_attachment_ref = vk.VkAttachmentReference{
                .attachment = 0,
                .layout = .color_attachment_optimal,
            };

            const subpass = vk.VkSubpassDescription{
                .flags = 0,
                .pipelineBindPoint = .graphics,
                .inputAttachmentCount = 0,
                .pInputAttachments = null,
                .colorAttachmentCount = 1,
                .pColorAttachments = @ptrCast(&color_attachment_ref),
                .pResolveAttachments = null,
                .pDepthStencilAttachment = null,
                .preserveAttachmentCount = 0,
                .pPreserveAttachments = null,
            };

            const render_pass_create_info = vk.VkRenderPassCreateInfo{
                .sType = .render_pass_create_info,
                .pNext = null,
                .flags = 0,
                .attachmentCount = 1,
                .pAttachments = @ptrCast(&color_attachment),
                .subpassCount = 1,
                .pSubpasses = @ptrCast(&subpass),
                .dependencyCount = 0,
                .pDependencies = null,
            };

            var render_pass: ?vk.VkRenderPass = undefined;
            {
                const rr = self.loader.vkCreateRenderPass(self.device, &render_pass_create_info, null, &render_pass);
                if (rr != .success) return EngineError.VulkanInitFailed;
            }
            self.render_pass = render_pass.?;

            // Now create framebuffers (needs render pass)
            for (self.swapchain_image_views, 0..) |image_view, i| {
                var attachments = [_]vk.VkImageView{image_view};
                const fb_info = vk.VkFramebufferCreateInfo{
                    .sType = .framebuffer_create_info,
                    .pNext = null,
                    .flags = 0,
                    .renderPass = self.render_pass,
                    .attachmentCount = 1,
                    .pAttachments = @ptrCast(&attachments),
                    .width = self.swapchain_extent.width,
                    .height = self.swapchain_extent.height,
                    .layers = 1,
                };

                var fb: ?vk.VkFramebuffer = undefined;
                const fr = self.loader.vkCreateFramebuffer(self.device, &fb_info, null, &fb);
                if (fr != .success) return EngineError.VulkanInitFailed;
                self.swapchain_framebuffers[i] = fb.?;
            }

            // Pipeline layout
            const pipeline_layout_info = vk.VkPipelineLayoutCreateInfo{
                .sType = .pipeline_layout_create_info,
                .pNext = null,
                .flags = 0,
                .setLayoutCount = 0,
                .pSetLayouts = null,
                .pushConstantRangeCount = 0,
                .pPushConstantRanges = null,
            };

            var pipeline_layout: ?vk.VkPipelineLayout = undefined;
            {
                const plr = self.loader.vkCreatePipelineLayout(self.device, &pipeline_layout_info, null, &pipeline_layout);
                if (plr != .success) return EngineError.VulkanInitFailed;
            }
            self.pipeline_layout = pipeline_layout.?;

            // Shader modules (destroyed via defer after pipeline creation)
            var vert_module: ?vk.VkShaderModule = undefined;
            {
                const shader_info = vk.VkShaderModuleCreateInfo{
                    .sType = .shader_module_create_info,
                    .pNext = null,
                    .flags = 0,
                    .codeSize = vertex_spv.len * @sizeOf(u32),
                    .pCode = &vertex_spv,
                };
                const sr = self.loader.vkCreateShaderModule(self.device, &shader_info, null, &vert_module);
                if (sr != .success) return EngineError.VulkanInitFailed;
            }
            defer self.loader.vkDestroyShaderModule(self.device, vert_module.?, null);

            var frag_module: ?vk.VkShaderModule = undefined;
            {
                const shader_info = vk.VkShaderModuleCreateInfo{
                    .sType = .shader_module_create_info,
                    .pNext = null,
                    .flags = 0,
                    .codeSize = fragment_spv.len * @sizeOf(u32),
                    .pCode = &fragment_spv,
                };
                const sr = self.loader.vkCreateShaderModule(self.device, &shader_info, null, &frag_module);
                if (sr != .success) return EngineError.VulkanInitFailed;
            }
            defer self.loader.vkDestroyShaderModule(self.device, frag_module.?, null);

            // Shader stages
            const stages = [_]vk.VkPipelineShaderStageCreateInfo{
                vk.VkPipelineShaderStageCreateInfo{
                    .sType = .pipeline_shader_stage_create_info,
                    .pNext = null,
                    .flags = 0,
                    .stage = .vertex,
                    .module = vert_module.?,
                    .pName = "main",
                    .pSpecializationInfo = null,
                },
                vk.VkPipelineShaderStageCreateInfo{
                    .sType = .pipeline_shader_stage_create_info,
                    .pNext = null,
                    .flags = 0,
                    .stage = .fragment,
                    .module = frag_module.?,
                    .pName = "main",
                    .pSpecializationInfo = null,
                },
            };

            // Fixed-function pipeline state
            const vertex_input_state = vk.VkPipelineVertexInputStateCreateInfo{
                .sType = .pipeline_vertex_input_state_create_info,
                .pNext = null,
                .flags = 0,
                .vertexBindingDescriptionCount = 0,
                .pVertexBindingDescriptions = null,
                .vertexAttributeDescriptionCount = 0,
                .pVertexAttributeDescriptions = null,
            };

            const input_assembly_state = vk.VkPipelineInputAssemblyStateCreateInfo{
                .sType = .pipeline_input_assembly_state_create_info,
                .pNext = null,
                .flags = 0,
                .topology = .triangle_list,
                .primitiveRestartEnable = 0,
            };

            const viewport = vk.VkViewport{
                .x = 0,
                .y = 0,
                .width = @floatFromInt(self.swapchain_extent.width),
                .height = @floatFromInt(self.swapchain_extent.height),
                .minDepth = 0.0,
                .maxDepth = 1.0,
            };

            const scissor = vk.VkRect2D{
                .offset = .{ .x = 0, .y = 0 },
                .extent = self.swapchain_extent,
            };

            const viewport_state = vk.VkPipelineViewportStateCreateInfo{
                .sType = .pipeline_viewport_state_create_info,
                .pNext = null,
                .flags = 0,
                .viewportCount = 1,
                .pViewports = @ptrCast(&viewport),
                .scissorCount = 1,
                .pScissors = @ptrCast(&scissor),
            };

            const rasterization_state = vk.VkPipelineRasterizationStateCreateInfo{
                .sType = .pipeline_rasterization_state_create_info,
                .pNext = null,
                .flags = 0,
                .depthClampEnable = 0,
                .rasterizerDiscardEnable = 0,
                .polygonMode = .fill,
                .cullMode = @intFromEnum(vk.VkCullModeFlagBits.back),
                .frontFace = .clockwise,
                .depthBiasEnable = 0,
                .depthBiasConstantFactor = 0,
                .depthBiasClamp = 0,
                .depthBiasSlopeFactor = 0,
                .lineWidth = 1.0,
            };

            const multisample_state = vk.VkPipelineMultisampleStateCreateInfo{
                .sType = .pipeline_multisample_state_create_info,
                .pNext = null,
                .flags = 0,
                .rasterizationSamples = .count_1,
                .sampleShadingEnable = 0,
                .minSampleShading = 0,
                .pSampleMask = null,
                .alphaToCoverageEnable = 0,
                .alphaToOneEnable = 0,
            };

            const blend_attachment = vk.VkPipelineColorBlendAttachmentState{
                .blendEnable = 0,
                .srcColorBlendFactor = .one,
                .dstColorBlendFactor = .zero,
                .colorBlendOp = .add,
                .srcAlphaBlendFactor = .one,
                .dstAlphaBlendFactor = .zero,
                .alphaBlendOp = .add,
                .colorWriteMask = 0xF,
            };

            const color_blend_state = vk.VkPipelineColorBlendStateCreateInfo{
                .sType = .pipeline_color_blend_state_create_info,
                .pNext = null,
                .flags = 0,
                .logicOpEnable = 0,
                .logicOp = .copy,
                .attachmentCount = 1,
                .pAttachments = @ptrCast(&blend_attachment),
                .blendConstants = .{ 0, 0, 0, 0 },
            };

            const pipeline_create_info = vk.VkGraphicsPipelineCreateInfo{
                .sType = .graphics_pipeline_create_info,
                .pNext = null,
                .flags = 0,
                .stageCount = 2,
                .pStages = &stages,
                .pVertexInputState = &vertex_input_state,
                .pInputAssemblyState = &input_assembly_state,
                .pTessellationState = null,
                .pViewportState = &viewport_state,
                .pRasterizationState = &rasterization_state,
                .pMultisampleState = &multisample_state,
                .pDepthStencilState = null,
                .pColorBlendState = &color_blend_state,
                .pDynamicState = null,
                .layout = self.pipeline_layout,
                .renderPass = self.render_pass,
                .subpass = 0,
                .basePipelineHandle = null,
                .basePipelineIndex = -1,
            };

            var pipeline: ?vk.VkPipeline = undefined;
            {
                const pr = self.loader.vkCreateGraphicsPipelines(
                    self.device,
                    null,
                    1,
                    &pipeline_create_info,
                    null,
                    @ptrCast(&pipeline),
                );
                if (pr != .success) return EngineError.VulkanInitFailed;
            }
            self.pipeline = pipeline.?;
            self.init_state = .pipeline_created;
        }

        // ---- Step 7: command pool + command buffers + sync objects ----
        {
            const cb_count = @as(u32, @intCast(self.swapchain_images.len));

            // Command pool
            const pool_info = vk.VkCommandPoolCreateInfo{
                .sType = .command_pool_create_info,
                .pNext = null,
                .flags = 0,
                .queueFamilyIndex = self.queue_family_index,
            };

            var pool: ?vk.VkCommandPool = undefined;
            {
                const pr = self.loader.vkCreateCommandPool(self.device, &pool_info, null, &pool);
                if (pr != .success) return EngineError.VulkanInitFailed;
            }
            self.command_pool = pool.?;

            // Allocate command buffers
            self.command_buffers = try allocator.alloc(vk.VkCommandBuffer, cb_count);

            const allocate_info = vk.VkCommandBufferAllocateInfo{
                .sType = .command_buffer_allocate_info,
                .pNext = null,
                .commandPool = self.command_pool,
                .level = .primary,
                .commandBufferCount = cb_count,
            };

            {
                const ar = self.loader.vkAllocateCommandBuffers(self.device, &allocate_info, @ptrCast(self.command_buffers.ptr));
                if (ar != .success) return EngineError.VulkanInitFailed;
            }

            // Semaphores
            const sem_info = vk.VkSemaphoreCreateInfo{
                .sType = .semaphore_create_info,
                .pNext = null,
                .flags = 0,
            };

            {
                var sem: ?vk.VkSemaphore = undefined;
                const sr = self.loader.vkCreateSemaphore(self.device, &sem_info, null, &sem);
                if (sr != .success) return EngineError.VulkanInitFailed;
                self.acquire_semaphore = sem.?;
            }
            {
                var sem: ?vk.VkSemaphore = undefined;
                const sr = self.loader.vkCreateSemaphore(self.device, &sem_info, null, &sem);
                if (sr != .success) return EngineError.VulkanInitFailed;
                self.render_semaphore = sem.?;
            }

            // Fence (signaled so first wait succeeds)
            const fence_info = vk.VkFenceCreateInfo{
                .sType = .fence_create_info,
                .pNext = null,
                .flags = @intFromEnum(vk.VkFenceCreateFlagBits.signaled),
            };

            {
                var fence: ?vk.VkFence = undefined;
                const fr = self.loader.vkCreateFence(self.device, &fence_info, null, &fence);
                if (fr != .success) return EngineError.VulkanInitFailed;
                self.in_flight_fence = fence.?;
            }

            self.current_image = 0;
            self.init_state = .ready;
        }

        return self;
    }

    // ------------------------------------------------------------------
    // deinit
    // ------------------------------------------------------------------

    /// Destroy all Vulkan resources in reverse creation order.
    /// Safe to call at any `init_state` — a no-op at `.none`.
    pub fn deinit(self: *VulkanBackend) void {
        const s = @intFromEnum(self.init_state);
        const a = self.allocator;

        if (s >= @intFromEnum(InitState.ready)) {
            _ = self.loader.vkDeviceWaitIdle(self.device);

            self.loader.vkDestroyFence(self.device, self.in_flight_fence, null);
            self.loader.vkDestroySemaphore(self.device, self.acquire_semaphore, null);
            self.loader.vkDestroySemaphore(self.device, self.render_semaphore, null);
            self.loader.vkFreeCommandBuffers(
                self.device,
                self.command_pool,
                @intCast(self.command_buffers.len),
                @ptrCast(self.command_buffers.ptr),
            );
            self.loader.vkDestroyCommandPool(self.device, self.command_pool, null);
            a.free(self.command_buffers);
        }

        if (s >= @intFromEnum(InitState.pipeline_created)) {
            self.loader.vkDestroyPipeline(self.device, self.pipeline, null);
            self.loader.vkDestroyPipelineLayout(self.device, self.pipeline_layout, null);

            for (self.swapchain_framebuffers) |fb| {
                self.loader.vkDestroyFramebuffer(self.device, fb, null);
            }
            a.free(self.swapchain_framebuffers);

            self.loader.vkDestroyRenderPass(self.device, self.render_pass, null);
        }

        if (s >= @intFromEnum(InitState.swapchain_created)) {
            for (self.swapchain_image_views) |view| {
                self.loader.vkDestroyImageView(self.device, view, null);
            }
            a.free(self.swapchain_image_views);
            a.free(self.swapchain_images);
            self.loader.vkDestroySwapchainKHR(self.device, self.swapchain, null);
        }

        if (s >= @intFromEnum(InitState.device_created)) {
            self.loader.vkDestroyDevice(self.device, null);
        }

        if (s >= @intFromEnum(InitState.surface_created)) {
            self.loader.vkDestroySurfaceKHR(self.instance, self.surface, null);
        }

        if (s >= @intFromEnum(InitState.instance_created)) {
            self.loader.vkDestroyInstance(self.instance, null);
        }

        self.init_state = .none;
    }

    // ------------------------------------------------------------------
    // beginFrame
    // ------------------------------------------------------------------

    /// Acquire the next swapchain image and begin recording a
    /// command buffer for this frame.
    pub fn beginFrame(self: *VulkanBackend, color: Color) void {
        // Wait for previous frame
        _ = self.loader.vkWaitForFences(self.device, 1, @ptrCast(&self.in_flight_fence), 1, std.math.maxInt(u64));

        // Reset fence
        _ = self.loader.vkResetFences(self.device, 1, @ptrCast(&self.in_flight_fence));

        // Acquire next image
        var image_index: u32 = undefined;
        const ar = self.loader.vkAcquireNextImageKHR(
            self.device,
            self.swapchain,
            std.math.maxInt(u64),
            self.acquire_semaphore,
            null,
            &image_index,
        );

        switch (ar) {
            .success, .error_out_of_date_khr => {
                self.current_image = image_index;
            },
            else => return,
        }

        // Begin command buffer
        const begin_info = vk.VkCommandBufferBeginInfo{
            .sType = .command_buffer_begin_info,
            .pNext = null,
            .flags = 0,
            .pInheritanceInfo = null,
        };

        _ = self.loader.vkBeginCommandBuffer(self.command_buffers[image_index], &begin_info);

        // Begin render pass with clear color
        const clear_color = vk.VkClearValue{
            .color = .{
                .float32 = .{
                    @as(f32, @floatFromInt(color.r)) / 255.0,
                    @as(f32, @floatFromInt(color.g)) / 255.0,
                    @as(f32, @floatFromInt(color.b)) / 255.0,
                    @as(f32, @floatFromInt(color.a)) / 255.0,
                },
            },
        };

        const render_pass_begin = vk.VkRenderPassBeginInfo{
            .sType = .render_pass_begin_info,
            .pNext = null,
            .renderPass = self.render_pass,
            .framebuffer = self.swapchain_framebuffers[image_index],
            .renderArea = .{
                .offset = .{ .x = 0, .y = 0 },
                .extent = self.swapchain_extent,
            },
            .clearValueCount = 1,
            .pClearValues = @ptrCast(&clear_color),
        };

        self.loader.vkCmdBeginRenderPass(
            self.command_buffers[image_index],
            &render_pass_begin,
            .inline_,
        );
    }

    // ------------------------------------------------------------------
    // endFrame
    // ------------------------------------------------------------------

    /// End rendering for the current frame — bind pipeline, draw,
    /// end render pass, end command buffer, and submit to queue.
    pub fn endFrame(self: *VulkanBackend) void {
        const cb = self.command_buffers[self.current_image];

        self.loader.vkCmdBindPipeline(cb, .graphics, self.pipeline);

        // MVP: draw 3 vertices with 1 instance (no vertex buffer)
        self.loader.vkCmdDraw(cb, 3, 1, 0, 0);

        self.loader.vkCmdEndRenderPass(cb);

        _ = self.loader.vkEndCommandBuffer(cb);

        // Submit
        const wait_stage = [_]vk.VkFlags{@intFromEnum(vk.VkPipelineStageFlagBits.color_attachment_output)};

        const submit_info = vk.VkSubmitInfo{
            .sType = .submit_info,
            .pNext = null,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = @ptrCast(&self.acquire_semaphore),
            .pWaitDstStageMask = &wait_stage,
            .commandBufferCount = 1,
            .pCommandBuffers = @constCast(@ptrCast(&cb)),
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = @ptrCast(&self.render_semaphore),
        };

        _ = self.loader.vkQueueSubmit(self.queue, 1, @ptrCast(&submit_info), self.in_flight_fence);
    }

    // ------------------------------------------------------------------
    // drawTriangle
    // ------------------------------------------------------------------

    /// Record a triangle draw for this frame. For the MVP the draw
    /// command is issued in `endFrame`; this method is a placeholder
    /// for future per-draw vertex data.
    pub fn drawTriangle(self: *VulkanBackend, v0: Vertex, v1: Vertex, v2: Vertex, texture: []const u8) void {
        _ = self;
        _ = v0;
        _ = v1;
        _ = v2;
        _ = texture;
    }

    // ------------------------------------------------------------------
    // present
    // ------------------------------------------------------------------

    /// Present the current swapchain image to the display.
    pub fn present(self: *VulkanBackend) void {

        const present_info = vk.VkPresentInfoKHR{
            .sType = .present_info_khr,
            .pNext = null,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = @ptrCast(&self.render_semaphore),
            .swapchainCount = 1,
            .pSwapchains = @ptrCast(&self.swapchain),
            .pImageIndices = @ptrCast(&self.current_image),
            .pResults = null,
        };

        _ = self.loader.vkQueuePresentKHR(self.queue, &present_info);
    }
};

// ===========================================================================
// Tests
// ===========================================================================

test "InitState enum values are sequential" {
    try testing.expectEqual(@intFromEnum(InitState.none), 0);
    try testing.expectEqual(@intFromEnum(InitState.instance_created), 1);
    try testing.expectEqual(@intFromEnum(InitState.surface_created), 2);
    try testing.expectEqual(@intFromEnum(InitState.device_created), 3);
    try testing.expectEqual(@intFromEnum(InitState.swapchain_created), 4);
    try testing.expectEqual(@intFromEnum(InitState.pipeline_created), 5);
    try testing.expectEqual(@intFromEnum(InitState.ready), 6);
}

test "InitState.reached works correctly" {
    try testing.expect(InitState.ready.reached(InitState.none));
    try testing.expect(InitState.ready.reached(InitState.ready));
    try testing.expect(!InitState.none.reached(InitState.instance_created));
    try testing.expect(InitState.swapchain_created.reached(InitState.surface_created));
    try testing.expect(!InitState.swapchain_created.reached(InitState.pipeline_created));
}

test "VulkanBackend struct compiles and has expected alignment" {
    try testing.expect(@sizeOf(VulkanBackend) > 0);
    try testing.expect(@alignOf(VulkanBackend) > 0);
}

test "deinit with none state is a no-op" {
    var backend = VulkanBackend{
        .allocator = std.testing.allocator,
        .config = .{ .width = 800, .height = 600 },
        .window_ptr = undefined,
        .init_state = .none,
        .loader = undefined,
        .instance = undefined,
        .surface = undefined,
        .physical_device = undefined,
        .device = undefined,
        .queue = undefined,
        .queue_family_index = undefined,
        .swapchain = undefined,
        .swapchain_format = undefined,
        .swapchain_extent = undefined,
        .swapchain_images = undefined,
        .swapchain_image_views = undefined,
        .swapchain_framebuffers = undefined,
        .render_pass = undefined,
        .pipeline_layout = undefined,
        .pipeline = undefined,
        .command_pool = undefined,
        .command_buffers = undefined,
        .acquire_semaphore = undefined,
        .render_semaphore = undefined,
        .in_flight_fence = undefined,
        .current_image = 0,
    };
    backend.deinit();
    try testing.expectEqual(backend.init_state, InitState.none);
}

test "init without libvulkan returns VulkanNotAvailable" {
    // On systems without libvulkan.so.1 the loader fails with
    // VulkanNotAvailable. On systems WITH Vulkan we skip the
    // full init test (no X11 display available in test runner).
    const result = vk_loader.VkFunctions.load();
    if (result) |funcs_val| {
        var funcs = funcs_val;
        defer funcs.deinit();
        _ = &funcs;
    } else |err| {
        try testing.expectEqual(err, EngineError.VulkanNotAvailable);
    }
}

test "VulkanBackend present takes no extra arguments" {
    // After refactoring, present() should only take self (no window_ctx)
    const sig = @typeInfo(@TypeOf(VulkanBackend.present)).@"fn";
    try testing.expectEqual(sig.params.len, 1);
}

test "VulkanBackend init accepts window, width, height" {
    // New init signature: (allocator, window: *platform.Window, width: u32, height: u32)
    const sig = @typeInfo(@TypeOf(VulkanBackend.init)).@"fn";
    try testing.expect(sig.params.len >= 4);
    // Second param should be *platform.Window
    const second_param_type = sig.params[1].type.?;
    try testing.expect(second_param_type == *@import("../platform/window.zig").Window);
}

test "VulkanBackend module compiles and links correctly" {
    try testing.expect(@sizeOf(InitState) <= @sizeOf(u32));
}
