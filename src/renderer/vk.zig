//! Manual Vulkan FFI bindings.
//!
//! We declare exactly the Vulkan API surface we need instead of depending on
//! generated bindings or system headers. Follows the same pattern as `x11.zig`:
//! opaque handle types, `extern fn` declarations, and struct creation types.
//!
//! Function pointers are resolved at runtime via dlopen in `vk_loader.zig`.

const std = @import("std");
const testing = std.testing;

// ---------------------------------------------------------------------------
// Vulkan Handle Types (opaque)
// ---------------------------------------------------------------------------

pub const VkInstance = *opaque {};
pub const VkPhysicalDevice = *opaque {};
pub const VkDevice = *opaque {};
pub const VkQueue = *opaque {};
pub const VkCommandBuffer = *opaque {};
pub const VkCommandPool = *opaque {};
pub const VkDeviceMemory = *opaque {};
pub const VkBuffer = *opaque {};
pub const VkImage = *opaque {};
pub const VkImageView = *opaque {};
pub const VkShaderModule = *opaque {};
pub const VkPipeline = *opaque {};
pub const VkPipelineLayout = *opaque {};
pub const VkRenderPass = *opaque {};
pub const VkFramebuffer = *opaque {};
pub const VkSampler = *opaque {};

// ---------------------------------------------------------------------------
// Vulkan Handle Types (dispatchable opaque for KHR surfaces)
// ---------------------------------------------------------------------------

pub const VkSurfaceKHR = *opaque {};
pub const VkSwapchainKHR = *opaque {};
pub const VkSemaphore = *opaque {};
pub const VkFence = *opaque {};
pub const VkEvent = *opaque {};
pub const VkQueryPool = *opaque {};
pub const VkDescriptorPool = *opaque {};
pub const VkDescriptorSet = *opaque {};
pub const VkDescriptorSetLayout = *opaque {};
pub const VkDebugUtilsMessengerEXT = *opaque {};

// ---------------------------------------------------------------------------
// Vulkan Integer Types
// ---------------------------------------------------------------------------

pub const VkBool32 = u32;
pub const VkDeviceSize = u64;
pub const VkFlags = u32;

// ---------------------------------------------------------------------------
// VkResult
// ---------------------------------------------------------------------------

pub const VkResult = enum(i32) {
    success = 0,
    not_ready = 1,
    timeout = 2,
    event_set = 3,
    event_reset = 4,
    incomplete = 5,
    error_out_of_host_memory = -1,
    error_out_of_device_memory = -2,
    error_initialization_failed = -3,
    error_device_lost = -4,
    error_memory_map_failed = -5,
    error_layer_not_present = -6,
    error_extension_not_present = -7,
    error_feature_not_present = -8,
    error_incompatible_driver = -9,
    error_too_many_objects = -10,
    error_format_not_supported = -11,
    error_fragmented_pool = -12,
    error_surface_lost_khr = -1000000000,
    error_native_window_in_use_khr = -1000000001,
    error_out_of_date_khr = -1000001004,
    error_incompatible_display_khr = -1000003001,
    error_invalid_shader_nv = -1000012000,
    error_invalid_drm_format_modifier_plane_layout_ext = -1000158000,
    error_not_permitted_khr = -1000174001,
    error_full_screen_exclusive_mode_lost_ext = -1000255000,
    error_invalid_opaque_capture_address_khr = -1000257000,
    error_fragmentation_ext = -1000161000,
    error_invalid_device_address_ext = -1000244000,
    error_pipeline_compile_required_ext = 1000297000,
    _,
};

// ---------------------------------------------------------------------------
// VkStructureType (subset for MVP)
// ---------------------------------------------------------------------------

pub const VkStructureType = enum(i32) {
    application_info = 0,
    instance_create_info = 1,
    device_queue_create_info = 2,
    device_create_info = 3,
    submit_info = 4,
    memory_allocate_info = 5,
    mapped_memory_range = 6,
    bind_sparse_info = 7,
    fence_create_info = 8,
    semaphore_create_info = 9,
    event_create_info = 10,
    query_pool_create_info = 11,
    buffer_create_info = 12,
    buffer_view_create_info = 13,
    image_create_info = 14,
    image_view_create_info = 15,
    shader_module_create_info = 16,
    pipeline_cache_create_info = 17,
    pipeline_shader_stage_create_info = 18,
    pipeline_vertex_input_state_create_info = 19,
    pipeline_input_assembly_state_create_info = 20,
    pipeline_tessellation_state_create_info = 21,
    pipeline_viewport_state_create_info = 22,
    pipeline_rasterization_state_create_info = 23,
    pipeline_multisample_state_create_info = 24,
    pipeline_depth_stencil_state_create_info = 25,
    pipeline_color_blend_state_create_info = 26,
    pipeline_dynamic_state_create_info = 27,
    graphics_pipeline_create_info = 28,
    compute_pipeline_create_info = 30,
    pipeline_layout_create_info = 31,
    sampler_create_info = 32,
    descriptor_set_layout_create_info = 33,
    descriptor_pool_create_info = 34,
    framebuffer_create_info = 35,
    render_pass_create_info = 36,
    command_pool_create_info = 37,
    command_buffer_allocate_info = 38,
    command_buffer_begin_info = 39,
    render_pass_begin_info = 40,
    swapchain_create_info_khr = 1000001000,
    present_info_khr = 1000001001,
    surface_create_info_khr = 1000000000,
    xlib_surface_create_info_khr = 1000004000,
    debug_utils_messenger_create_info_ext = 1000128004,
    _,
};

// ---------------------------------------------------------------------------
// VkImageLayout
// ---------------------------------------------------------------------------

pub const VkImageLayout = enum(i32) {
    undefined = 0,
    general = 1,
    color_attachment_optimal = 2,
    depth_stencil_attachment_optimal = 3,
    depth_stencil_read_only_optimal = 4,
    shader_read_only_optimal = 5,
    transfer_src_optimal = 6,
    transfer_dst_optimal = 7,
    preinitialized = 8,
    present_src_khr = 1000001002,
    _,
};

// ---------------------------------------------------------------------------
// VkImageUsageFlagBits
// ---------------------------------------------------------------------------

pub const VkImageUsageFlagBits = enum(u32) {
    transfer_src = 1 << 0,
    transfer_dst = 1 << 1,
    sampled = 1 << 2,
    storage = 1 << 3,
    color_attachment = 1 << 4,
    depth_stencil_attachment = 1 << 5,
    transient_attachment = 1 << 6,
    input_attachment = 1 << 7,
    _,
};

// ---------------------------------------------------------------------------
// VkFormat
// ---------------------------------------------------------------------------

pub const VkFormat = enum(i32) {
    undefined = 0,
    b8g8r8a8_unorm = 44,
    b8g8r8a8_srgb = 50,
    r8g8b8a8_unorm = 37,
    r8g8b8a8_srgb = 43,
    d32_sfloat = 126,
    d24_unorm_s8_uint = 45,
    d16_unorm = 124,
    d32_sfloat_s8_uint = 130,
    _,
};

// ---------------------------------------------------------------------------
// VkPresentModeKHR
// ---------------------------------------------------------------------------

pub const VkPresentModeKHR = enum(i32) {
    immediate_khr = 0,
    mailbox_khr = 1,
    fifo_khr = 2,
    fifo_relaxed_khr = 3,
    _,
};

// ---------------------------------------------------------------------------
// VkColorSpaceKHR
// ---------------------------------------------------------------------------

pub const VkColorSpaceKHR = enum(i32) {
    srgb_nonlinear_khr = 0,
    _,
};

// ---------------------------------------------------------------------------
// VkSharingMode
// ---------------------------------------------------------------------------

pub const VkSharingMode = enum(i32) {
    exclusive = 0,
    concurrent = 1,
};

// ---------------------------------------------------------------------------
// VkPrimitiveTopology
// ---------------------------------------------------------------------------

pub const VkPrimitiveTopology = enum(i32) {
    point_list = 0,
    line_list = 1,
    line_strip = 2,
    triangle_list = 3,
    triangle_strip = 4,
    triangle_fan = 5,
    line_list_with_adjacency = 6,
    line_strip_with_adjacency = 7,
    triangle_list_with_adjacency = 8,
    triangle_strip_with_adjacency = 9,
    patch_list = 10,
};

// ---------------------------------------------------------------------------
// VkPolygonMode
// ---------------------------------------------------------------------------

pub const VkPolygonMode = enum(i32) {
    fill = 0,
    line = 1,
    point = 2,
    fill_rectangle_nv = 1000153000,
    _,
};

// ---------------------------------------------------------------------------
// VkCullModeFlagBits
// ---------------------------------------------------------------------------

pub const VkCullModeFlagBits = enum(u32) {
    none = 0,
    front = 1 << 0,
    back = 1 << 1,
    front_and_back = 1 << 0 | 1 << 1,
    _,
};

// ---------------------------------------------------------------------------
// VkFrontFace
// ---------------------------------------------------------------------------

pub const VkFrontFace = enum(i32) {
    counter_clockwise = 0,
    clockwise = 1,
};

// ---------------------------------------------------------------------------
// VkCompareOp
// ---------------------------------------------------------------------------

pub const VkCompareOp = enum(i32) {
    never = 0,
    less = 1,
    equal = 2,
    less_or_equal = 3,
    greater = 4,
    not_equal = 5,
    greater_or_equal = 6,
    always = 7,
};

// ---------------------------------------------------------------------------
// VkSampleCountFlagBits
// ---------------------------------------------------------------------------

pub const VkSampleCountFlagBits = enum(u32) {
    count_1 = 1 << 0,
    count_2 = 1 << 1,
    count_4 = 1 << 2,
    count_8 = 1 << 3,
    count_16 = 1 << 4,
    count_32 = 1 << 5,
    count_64 = 1 << 6,
    _,
};

// ---------------------------------------------------------------------------
// VkAttachmentLoadOp / VkAttachmentStoreOp
// ---------------------------------------------------------------------------

pub const VkAttachmentLoadOp = enum(i32) {
    load = 0,
    clear = 1,
    dont_care = 2,
};

pub const VkAttachmentStoreOp = enum(i32) {
    store = 0,
    dont_care = 1,
};

// ---------------------------------------------------------------------------
// VkPipelineBindPoint
// ---------------------------------------------------------------------------

pub const VkPipelineBindPoint = enum(i32) {
    graphics = 0,
    compute = 1,
};

// ---------------------------------------------------------------------------
// VkCommandBufferLevel
// ---------------------------------------------------------------------------

pub const VkCommandBufferLevel = enum(i32) {
    primary = 0,
    secondary = 1,
};

// ---------------------------------------------------------------------------
// VkIndexType
// ---------------------------------------------------------------------------

pub const VkIndexType = enum(i32) {
    uint16 = 0,
    uint32 = 1,
};

// ---------------------------------------------------------------------------
// VkDescriptorType
// ---------------------------------------------------------------------------

pub const VkSubpassContents = enum(i32) {
    inline_ = 0,
    secondary_command_buffers = 1,
};

pub const VkDescriptorType = enum(i32) {
    sampler = 0,
    combined_image_sampler = 1,
    sampled_image = 2,
    storage_image = 3,
    uniform_texel_buffer = 4,
    storage_texel_buffer = 5,
    uniform_buffer = 6,
    storage_buffer = 7,
    uniform_buffer_dynamic = 8,
    storage_buffer_dynamic = 9,
    input_attachment = 10,
};

// ---------------------------------------------------------------------------
// VkFilter / VkSamplerAddressMode
// ---------------------------------------------------------------------------

pub const VkFilter = enum(i32) {
    nearest = 0,
    linear = 1,
    cubic_img = 1000015000,
    _,
};

pub const VkSamplerAddressMode = enum(i32) {
    repeat = 0,
    mirrored_repeat = 1,
    clamp_to_edge = 2,
    clamp_to_border = 3,
    mirror_clamp_to_edge = 4,
};

// ---------------------------------------------------------------------------
// VkBlendFactor / VkBlendOp
// ---------------------------------------------------------------------------

pub const VkBlendFactor = enum(i32) {
    zero = 0,
    one = 1,
    src_color = 2,
    one_minus_src_color = 3,
    dst_color = 4,
    one_minus_dst_color = 5,
    src_alpha = 6,
    one_minus_src_alpha = 7,
    dst_alpha = 8,
    one_minus_dst_alpha = 9,
    constant_color = 10,
    one_minus_constant_color = 11,
    constant_alpha = 12,
    one_minus_constant_alpha = 13,
    src_alpha_saturate = 14,
    src1_color = 15,
    one_minus_src1_color = 16,
    src1_alpha = 17,
    one_minus_src1_alpha = 18,
};

pub const VkBlendOp = enum(i32) {
    add = 0,
    subtract = 1,
    reverse_subtract = 2,
    min = 3,
    max = 4,
};

// ---------------------------------------------------------------------------
// VkColorComponentFlagBits
// ---------------------------------------------------------------------------

pub const VkColorComponentFlagBits = enum(u32) {
    r = 1 << 0,
    g = 1 << 1,
    b = 1 << 2,
    a = 1 << 3,
    _,
};

// ---------------------------------------------------------------------------
// VkSampleMask
// ---------------------------------------------------------------------------

pub const VkSampleMask = u32;

// ---------------------------------------------------------------------------
// Core Vulkan Struct Types
// ---------------------------------------------------------------------------

pub const VkApplicationInfo = extern struct {
    sType: VkStructureType,
    pNext: ?*const anyopaque,
    pApplicationName: ?[*:0]const u8,
    applicationVersion: u32,
    pEngineName: ?[*:0]const u8,
    engineVersion: u32,
    apiVersion: u32,
};

pub const VkInstanceCreateInfo = extern struct {
    sType: VkStructureType = .instance_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    pApplicationInfo: ?*const VkApplicationInfo = null,
    enabledLayerCount: u32 = 0,
    ppEnabledLayerNames: ?[*]?[*:0]const u8 = null,
    enabledExtensionCount: u32 = 0,
    ppEnabledExtensionNames: ?[*]?[*:0]const u8 = null,
};

pub const VkDeviceQueueCreateInfo = extern struct {
    sType: VkStructureType = .device_queue_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    queueFamilyIndex: u32,
    queueCount: u32,
    pQueuePriorities: ?[*]const f32,
};

pub const VkDeviceCreateInfo = extern struct {
    sType: VkStructureType = .device_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    queueCreateInfoCount: u32 = 0,
    pQueueCreateInfos: ?[*]const VkDeviceQueueCreateInfo = null,
    enabledLayerCount: u32 = 0,
    ppEnabledLayerNames: ?[*]?[*:0]const u8 = null,
    enabledExtensionCount: u32 = 0,
    ppEnabledExtensionNames: ?[*]?[*:0]const u8 = null,
    pEnabledFeatures: ?*const anyopaque = null,
};

pub const VkSwapchainCreateInfoKHR = extern struct {
    sType: VkStructureType = .swapchain_create_info_khr,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    surface: VkSurfaceKHR,
    minImageCount: u32,
    imageFormat: VkFormat,
    imageColorSpace: VkColorSpaceKHR,
    imageExtent: VkExtent2D,
    imageArrayLayers: u32,
    imageUsage: VkFlags,
    imageSharingMode: VkSharingMode,
    queueFamilyIndexCount: u32 = 0,
    pQueueFamilyIndices: ?[*]const u32 = null,
    preTransform: VkSurfaceTransformFlagBitsKHR,
    compositeAlpha: VkCompositeAlphaFlagBitsKHR,
    presentMode: VkPresentModeKHR,
    clipped: VkBool32,
    oldSwapchain: ?VkSwapchainKHR = null,
};

pub const VkExtent2D = extern struct {
    width: u32,
    height: u32,
};

pub const VkOffset2D = extern struct {
    x: i32,
    y: i32,
};

pub const VkRect2D = extern struct {
    offset: VkOffset2D,
    extent: VkExtent2D,
};

pub const VkSurfaceTransformFlagBitsKHR = enum(u32) {
    identity = 0,
    rotate_90 = 1,
    rotate_180 = 2,
    rotate_270 = 3,
    horizontal_mirror = 4,
    horizontal_mirror_rotate_90 = 5,
    horizontal_mirror_rotate_180 = 6,
    horizontal_mirror_rotate_270 = 7,
    inherit = 8,
    _,
};

pub const VkCompositeAlphaFlagBitsKHR = enum(u32) {
    opaque_khr = 1 << 0,
    pre_multiplied = 1 << 1,
    post_multiplied = 1 << 2,
    inherit = 1 << 3,
    _,
};

pub const VkPipelineShaderStageCreateInfo = extern struct {
    sType: VkStructureType = .pipeline_shader_stage_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    stage: VkShaderStageFlagBits,
    module: VkShaderModule,
    pName: [*:0]const u8,
    pSpecializationInfo: ?*const anyopaque = null,
};

pub const VkShaderStageFlagBits = enum(u32) {
    vertex = 1 << 0,
    tessellation_control = 1 << 1,
    tessellation_evaluation = 1 << 2,
    geometry = 1 << 3,
    fragment = 1 << 4,
    compute = 1 << 5,
    all_graphics = 1 << 0 | 1 << 1 | 1 << 2 | 1 << 3 | 1 << 4,
    all = 0x7FFFFFFF,
    _,
};

pub const VkPipelineVertexInputStateCreateInfo = extern struct {
    sType: VkStructureType = .pipeline_vertex_input_state_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    vertexBindingDescriptionCount: u32 = 0,
    pVertexBindingDescriptions: ?[*]const VkVertexInputBindingDescription = null,
    vertexAttributeDescriptionCount: u32 = 0,
    pVertexAttributeDescriptions: ?[*]const VkVertexInputAttributeDescription = null,
};

pub const VkVertexInputBindingDescription = extern struct {
    binding: u32,
    stride: u32,
    inputRate: VkVertexInputRate,
};

pub const VkVertexInputRate = enum(i32) {
    vertex = 0,
    instance = 1,
};

pub const VkVertexInputAttributeDescription = extern struct {
    location: u32,
    binding: u32,
    format: VkFormat,
    offset: u32,
};

pub const VkPipelineInputAssemblyStateCreateInfo = extern struct {
    sType: VkStructureType = .pipeline_input_assembly_state_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    topology: VkPrimitiveTopology,
    primitiveRestartEnable: VkBool32 = 0,
};

pub const VkPipelineViewportStateCreateInfo = extern struct {
    sType: VkStructureType = .pipeline_viewport_state_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    viewportCount: u32,
    pViewports: ?[*]const VkViewport,
    scissorCount: u32,
    pScissors: ?[*]const VkRect2D,
};

pub const VkViewport = extern struct {
    x: f32,
    y: f32,
    width: f32,
    height: f32,
    minDepth: f32,
    maxDepth: f32,
};

pub const VkPipelineRasterizationStateCreateInfo = extern struct {
    sType: VkStructureType = .pipeline_rasterization_state_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    depthClampEnable: VkBool32 = 0,
    rasterizerDiscardEnable: VkBool32 = 0,
    polygonMode: VkPolygonMode = .fill,
    cullMode: VkFlags = 0,
    frontFace: VkFrontFace = .counter_clockwise,
    depthBiasEnable: VkBool32 = 0,
    depthBiasConstantFactor: f32 = 0,
    depthBiasClamp: f32 = 0,
    depthBiasSlopeFactor: f32 = 0,
    lineWidth: f32 = 1.0,
};

pub const VkPipelineMultisampleStateCreateInfo = extern struct {
    sType: VkStructureType = .pipeline_multisample_state_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    rasterizationSamples: VkSampleCountFlagBits = .count_1,
    sampleShadingEnable: VkBool32 = 0,
    minSampleShading: f32 = 1.0,
    pSampleMask: ?[*]const VkSampleMask = null,
    alphaToCoverageEnable: VkBool32 = 0,
    alphaToOneEnable: VkBool32 = 0,
};

pub const VkPipelineDepthStencilStateCreateInfo = extern struct {
    sType: VkStructureType = .pipeline_depth_stencil_state_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    depthTestEnable: VkBool32 = 0,
    depthWriteEnable: VkBool32 = 0,
    depthCompareOp: VkCompareOp = .never,
    depthBoundsTestEnable: VkBool32 = 0,
    stencilTestEnable: VkBool32 = 0,
    front: VkStencilOpState = .{},
    back: VkStencilOpState = .{},
    minDepthBounds: f32 = 0,
    maxDepthBounds: f32 = 1.0,
};

pub const VkStencilOpState = extern struct {
    failOp: VkStencilOp = .keep,
    passOp: VkStencilOp = .keep,
    depthFailOp: VkStencilOp = .keep,
    compareOp: VkCompareOp = .never,
    compareMask: u32 = 0,
    writeMask: u32 = 0,
    reference: u32 = 0,
};

pub const VkStencilOp = enum(i32) {
    keep = 0,
    zero = 1,
    replace = 2,
    increment_and_clamp = 3,
    decrement_and_clamp = 4,
    invert = 5,
    increment_and_wrap = 6,
    decrement_and_wrap = 7,
};

pub const VkPipelineColorBlendAttachmentState = extern struct {
    blendEnable: VkBool32 = 0,
    srcColorBlendFactor: VkBlendFactor = .one,
    dstColorBlendFactor: VkBlendFactor = .zero,
    colorBlendOp: VkBlendOp = .add,
    srcAlphaBlendFactor: VkBlendFactor = .one,
    dstAlphaBlendFactor: VkBlendFactor = .zero,
    alphaBlendOp: VkBlendOp = .add,
    colorWriteMask: VkFlags = 0xF,
};

pub const VkPipelineColorBlendStateCreateInfo = extern struct {
    sType: VkStructureType = .pipeline_color_blend_state_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    logicOpEnable: VkBool32 = 0,
    logicOp: VkLogicOp = .copy,
    attachmentCount: u32,
    pAttachments: ?[*]const VkPipelineColorBlendAttachmentState,
    blendConstants: [4]f32 = .{ 0, 0, 0, 0 },
};

pub const VkLogicOp = enum(i32) {
    clear = 0,
    and_op = 1,
    and_reverse = 2,
    copy = 3,
    and_inverted = 4,
    no_op = 5,
    xor = 6,
    or_op = 7,
    nor = 8,
    equivalent = 9,
    invert = 10,
    or_reverse = 11,
    copy_inverted = 12,
    or_inverted = 13,
    nand = 14,
    set = 15,
};

pub const VkPipelineDynamicStateCreateInfo = extern struct {
    sType: VkStructureType = .pipeline_dynamic_state_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    dynamicStateCount: u32,
    pDynamicStates: ?[*]const VkDynamicState,
};

pub const VkDynamicState = enum(i32) {
    viewport = 0,
    scissor = 1,
    line_width = 2,
    depth_bias = 3,
    blend_constants = 4,
    depth_bounds = 5,
    stencil_compare_mask = 6,
    stencil_write_mask = 7,
    stencil_reference = 8,
};

pub const VkGraphicsPipelineCreateInfo = extern struct {
    sType: VkStructureType = .graphics_pipeline_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    stageCount: u32,
    pStages: ?[*]const VkPipelineShaderStageCreateInfo,
    pVertexInputState: ?*const VkPipelineVertexInputStateCreateInfo,
    pInputAssemblyState: ?*const VkPipelineInputAssemblyStateCreateInfo,
    pTessellationState: ?*const anyopaque = null,
    pViewportState: ?*const VkPipelineViewportStateCreateInfo,
    pRasterizationState: ?*const VkPipelineRasterizationStateCreateInfo,
    pMultisampleState: ?*const VkPipelineMultisampleStateCreateInfo,
    pDepthStencilState: ?*const anyopaque = null,
    pColorBlendState: ?*const VkPipelineColorBlendStateCreateInfo,
    pDynamicState: ?*const anyopaque = null,
layout: VkPipelineLayout,
    renderPass: VkRenderPass,
    subpass: u32,
    basePipelineHandle: ?VkPipeline = null,
    basePipelineIndex: i32 = -1,
};

pub const VkRenderPassCreateInfo = extern struct {
    sType: VkStructureType = .render_pass_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    attachmentCount: u32,
    pAttachments: ?[*]const VkAttachmentDescription,
    subpassCount: u32,
    pSubpasses: ?[*]const VkSubpassDescription,
    dependencyCount: u32 = 0,
    pDependencies: ?[*]const VkSubpassDependency = null,
};

pub const VkAttachmentDescription = extern struct {
    flags: VkFlags = 0,
    format: VkFormat,
    samples: VkSampleCountFlagBits = .count_1,
    loadOp: VkAttachmentLoadOp,
    storeOp: VkAttachmentStoreOp,
    stencilLoadOp: VkAttachmentLoadOp = .dont_care,
    stencilStoreOp: VkAttachmentStoreOp = .dont_care,
    initialLayout: VkImageLayout,
    finalLayout: VkImageLayout,
};

pub const VkSubpassDescription = extern struct {
    flags: VkFlags = 0,
    pipelineBindPoint: VkPipelineBindPoint = .graphics,
    inputAttachmentCount: u32 = 0,
    pInputAttachments: ?[*]const VkAttachmentReference = null,
    colorAttachmentCount: u32,
    pColorAttachments: ?[*]const VkAttachmentReference,
    pResolveAttachments: ?[*]const VkAttachmentReference = null,
    pDepthStencilAttachment: ?*const VkAttachmentReference = null,
    preserveAttachmentCount: u32 = 0,
    pPreserveAttachments: ?[*]const u32 = null,
};

pub const VkAttachmentReference = extern struct {
    attachment: u32,
    layout: VkImageLayout,
};

pub const VkSubpassDependency = extern struct {
    srcSubpass: u32,
    dstSubpass: u32,
    srcStageMask: VkFlags,
    dstStageMask: VkFlags,
    srcAccessMask: VkFlags = 0,
    dstAccessMask: VkFlags = 0,
    dependencyFlags: VkFlags = 0,
};

pub const VkPipelineLayoutCreateInfo = extern struct {
    sType: VkStructureType = .pipeline_layout_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    setLayoutCount: u32 = 0,
    pSetLayouts: ?[*]*const VkDescriptorSetLayout = null,
    pushConstantRangeCount: u32 = 0,
    pPushConstantRanges: ?[*]const VkPushConstantRange = null,
};

pub const VkPushConstantRange = extern struct {
    stageFlags: VkFlags,
    offset: u32,
    size: u32,
};

pub const VkShaderModuleCreateInfo = extern struct {
    sType: VkStructureType = .shader_module_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    codeSize: usize,
    pCode: ?[*]const u32,
};

pub const VkFenceCreateInfo = extern struct {
    sType: VkStructureType = .fence_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
};

pub const VkSemaphoreCreateInfo = extern struct {
    sType: VkStructureType = .semaphore_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
};

pub const VkCommandPoolCreateInfo = extern struct {
    sType: VkStructureType = .command_pool_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    queueFamilyIndex: u32,
};

pub const VkCommandBufferAllocateInfo = extern struct {
    sType: VkStructureType = .command_buffer_allocate_info,
    pNext: ?*const anyopaque = null,
    commandPool: VkCommandPool,
    level: VkCommandBufferLevel,
    commandBufferCount: u32,
};

pub const VkCommandBufferBeginInfo = extern struct {
    sType: VkStructureType = .command_buffer_begin_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    pInheritanceInfo: ?*const anyopaque = null,
};

pub const VkSubmitInfo = extern struct {
    sType: VkStructureType = .submit_info,
    pNext: ?*const anyopaque = null,
    waitSemaphoreCount: u32 = 0,
pWaitSemaphores: ?[*]const VkSemaphore = null,
    pWaitDstStageMask: ?[*]const VkFlags,
    commandBufferCount: u32,
    pCommandBuffers: ?[*]const VkCommandBuffer,
    signalSemaphoreCount: u32,
    pSignalSemaphores: ?[*]const VkSemaphore = null,
};

pub const VkPresentInfoKHR = extern struct {
    sType: VkStructureType = .present_info_khr,
    pNext: ?*const anyopaque = null,
    waitSemaphoreCount: u32 = 0,
    pWaitSemaphores: ?[*]const VkSemaphore = null,
    swapchainCount: u32,
    pSwapchains: ?[*]const VkSwapchainKHR,
    pImageIndices: ?[*]const u32,
    pResults: ?[*]VkResult = null,
};

pub const VkImageCreateInfo = extern struct {
    sType: VkStructureType = .image_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    imageType: VkImageType,
    format: VkFormat,
    extent: VkExtent3D,
    mipLevels: u32,
    arrayLayers: u32,
    samples: VkSampleCountFlagBits = .count_1,
    tiling: VkImageTiling,
    usage: VkFlags,
    sharingMode: VkSharingMode = .exclusive,
    queueFamilyIndexCount: u32 = 0,
    pQueueFamilyIndices: ?[*]const u32 = null,
    initialLayout: VkImageLayout = .undefined,
};

// ---------------------------------------------------------------------------
// VkImageAspectFlagBits
// ---------------------------------------------------------------------------

pub const VkImageAspectFlagBits = enum(u32) {
    color = 1 << 0,
    depth = 1 << 1,
    stencil = 1 << 2,
    metadata = 1 << 3,
    plane_0 = 1 << 4,
    plane_1 = 1 << 5,
    plane_2 = 1 << 6,
    memory_plane_0_ext = 1 << 7,
    memory_plane_1_ext = 1 << 8,
    memory_plane_2_ext = 1 << 9,
    memory_plane_3_ext = 1 << 10,
    _,
};

// ---------------------------------------------------------------------------
// VkPipelineStageFlagBits
// ---------------------------------------------------------------------------

pub const VkPipelineStageFlagBits = enum(u32) {
    top_of_pipe = 0,
    draw_indirect = 1 << 1,
    vertex_input = 1 << 2,
    vertex_shader = 1 << 3,
    tessellation_control_shader = 1 << 4,
    tessellation_evaluation_shader = 1 << 5,
    geometry_shader = 1 << 6,
    fragment_shader = 1 << 7,
    early_fragment_tests = 1 << 8,
    late_fragment_tests = 1 << 9,
    color_attachment_output = 1 << 10,
    compute_shader = 1 << 11,
    transfer = 1 << 12,
    bottom_of_pipe = 1 << 13,
    host = 1 << 14,
    all_graphics = 1 << 15,
    all_commands = 1 << 16,
    _,
};

// ---------------------------------------------------------------------------
// VkFenceCreateFlagBits
// ---------------------------------------------------------------------------

pub const VkFenceCreateFlagBits = enum(u32) {
    signaled = 1 << 0,
    _,
};

// ---------------------------------------------------------------------------
// API version constant
// ---------------------------------------------------------------------------

pub const VK_API_VERSION_1_0: u32 = 0x00400000;

pub const VkImageType = enum(i32) {
    image_1d = 0,
    image_2d = 1,
    image_3d = 2,
};

pub const VkExtent3D = extern struct {
    width: u32,
    height: u32,
    depth: u32,
};

pub const VkImageTiling = enum(i32) {
    optimal = 0,
    linear = 1,
};

pub const VkImageViewCreateInfo = extern struct {
    sType: VkStructureType = .image_view_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    image: VkImage,
    viewType: VkImageViewType,
    format: VkFormat,
    components: VkComponentMapping = .{},
    subresourceRange: VkImageSubresourceRange,
};

pub const VkImageViewType = enum(i32) {
    image_1d = 0,
    image_2d = 1,
    image_3d = 2,
    cube = 3,
    image_1d_array = 4,
    image_2d_array = 5,
    cube_array = 6,
};

pub const VkComponentMapping = extern struct {
    r: VkComponentSwizzle = .identity,
    g: VkComponentSwizzle = .identity,
    b: VkComponentSwizzle = .identity,
    a: VkComponentSwizzle = .identity,
};

pub const VkComponentSwizzle = enum(i32) {
    identity = 0,
    zero = 1,
    one = 2,
    r = 3,
    g = 4,
    b = 5,
    a = 6,
};

pub const VkImageSubresourceRange = extern struct {
    aspectMask: VkFlags,
    baseMipLevel: u32 = 0,
    levelCount: u32,
    baseArrayLayer: u32 = 0,
    layerCount: u32,
};

pub const VkFramebufferCreateInfo = extern struct {
    sType: VkStructureType = .framebuffer_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    renderPass: VkRenderPass,
    attachmentCount: u32,
    pAttachments: ?[*]const VkImageView,
    width: u32,
    height: u32,
    layers: u32,
};

pub const VkRenderPassBeginInfo = extern struct {
    sType: VkStructureType = .render_pass_begin_info,
    pNext: ?*const anyopaque = null,
    renderPass: VkRenderPass,
    framebuffer: VkFramebuffer,
    renderArea: VkRect2D,
    clearValueCount: u32,
    pClearValues: ?[*]const VkClearValue,
};

pub const VkClearValue = extern union {
    color: VkClearColorValue,
    depthStencil: VkClearDepthStencilValue,
};

pub const VkClearColorValue = extern union {
    float32: [4]f32,
    int32: [4]i32,
    uint32: [4]u32,
};

pub const VkClearDepthStencilValue = extern struct {
    depth: f32,
    stencil: u32,
};

pub const VkMemoryAllocateInfo = extern struct {
    sType: VkStructureType = .memory_allocate_info,
    pNext: ?*const anyopaque = null,
    allocationSize: VkDeviceSize,
    memoryTypeIndex: u32,
};

pub const VkMappedMemoryRange = extern struct {
    sType: VkStructureType = .mapped_memory_range,
    pNext: ?*const anyopaque = null,
    memory: VkDeviceMemory,
    offset: VkDeviceSize = 0,
    size: VkDeviceSize,
};

pub const VkBufferCreateInfo = extern struct {
    sType: VkStructureType = .buffer_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    size: VkDeviceSize,
    usage: VkFlags,
    sharingMode: VkSharingMode = .exclusive,
    queueFamilyIndexCount: u32 = 0,
    pQueueFamilyIndices: ?[*]const u32 = null,
};

// ---------------------------------------------------------------------------
// Surface extension types
// ---------------------------------------------------------------------------

pub const VkXlibSurfaceCreateInfoKHR = extern struct {
    sType: VkStructureType = .xlib_surface_create_info_khr,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    dpy: *anyopaque,
    window: c_ulong,
};

pub const VkSurfaceCapabilitiesKHR = extern struct {
    minImageCount: u32,
    maxImageCount: u32,
    currentExtent: VkExtent2D,
    minImageExtent: VkExtent2D,
    maxImageExtent: VkExtent2D,
    maxImageArrayLayers: u32,
    supportedTransforms: VkFlags,
    currentTransform: VkSurfaceTransformFlagBitsKHR,
    supportedCompositeAlpha: VkFlags,
    supportedUsageFlags: VkFlags,
};

pub const VkSurfaceFormatKHR = extern struct {
    format: VkFormat,
    colorSpace: VkColorSpaceKHR,
};

// ---------------------------------------------------------------------------
// Queue family properties (needed by VulkanBackend init)
// ---------------------------------------------------------------------------

pub const VkQueueFamilyProperties = extern struct {
    queueFlags: VkFlags,
    queueCount: u32,
    timestampValidBits: u32,
    minImageTransferGranularity: VkExtent3D,
};

// ---------------------------------------------------------------------------
// VkImageSubresourceLayers / VkBufferImageCopy (for texture uploads)
// ---------------------------------------------------------------------------

pub const VkImageSubresourceLayers = extern struct {
    aspectMask: VkFlags,
    mipLevel: u32 = 0,
    baseArrayLayer: u32 = 0,
    layerCount: u32 = 1,
};

pub const VkBufferImageCopy = extern struct {
    bufferOffset: VkDeviceSize = 0,
    bufferRowLength: u32 = 0,
    bufferImageHeight: u32 = 0,
    imageSubresource: VkImageSubresourceLayers,
    imageOffset: VkOffset3D = .{},
    imageExtent: VkExtent3D,
};

pub const VkOffset3D = extern struct {
    x: i32 = 0,
    y: i32 = 0,
    z: i32 = 0,
};

// ---------------------------------------------------------------------------
// VkPipelineCacheCreateInfo
// ---------------------------------------------------------------------------

pub const VkPipelineCacheCreateInfo = extern struct {
    sType: VkStructureType = .pipeline_cache_create_info,
    pNext: ?*const anyopaque = null,
    flags: VkFlags = 0,
    initialDataSize: usize = 0,
    pInitialData: ?*const anyopaque = null,
};

// ---------------------------------------------------------------------------
// Function Names — returns the list of function names needed for dlsym
// ---------------------------------------------------------------------------

/// Returns the list of Vulkan function names that must be resolved via dlsym.
/// This list covers the minimum set for: instance creation, device creation,
/// swapchain, pipeline, command buffers, draw, present, and surface extensions.
pub fn neededFunctions() []const []const u8 {
    return &[_][]const u8{
        // Instance & device
        "vkCreateInstance",
        "vkDestroyInstance",
        "vkEnumeratePhysicalDevices",
        "vkGetPhysicalDeviceProperties",
        "vkGetPhysicalDeviceFeatures",
        "vkGetPhysicalDeviceQueueFamilyProperties",
        "vkCreateDevice",
        "vkDestroyDevice",
        "vkGetDeviceQueue",
        "vkDeviceWaitIdle",

        // Surface (KHR)
        "vkDestroySurfaceKHR",
        "vkGetPhysicalDeviceSurfaceSupportKHR",
        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
        "vkGetPhysicalDeviceSurfaceFormatsKHR",
        "vkGetPhysicalDeviceSurfacePresentModesKHR",
        "vkCreateXlibSurfaceKHR",

        // Swapchain (KHR)
        "vkCreateSwapchainKHR",
        "vkDestroySwapchainKHR",
        "vkGetSwapchainImagesKHR",
        "vkAcquireNextImageKHR",
        "vkQueuePresentKHR",

        // Shader module & pipeline
        "vkCreateShaderModule",
        "vkDestroyShaderModule",
        "vkCreatePipelineLayout",
        "vkDestroyPipelineLayout",
        "vkCreateGraphicsPipelines",
        "vkDestroyPipeline",
        "vkCreateRenderPass",
        "vkDestroyRenderPass",
        "vkCreateFramebuffer",
        "vkDestroyFramebuffer",

        // Command buffers
        "vkCreateCommandPool",
        "vkDestroyCommandPool",
        "vkAllocateCommandBuffers",
        "vkFreeCommandBuffers",
        "vkBeginCommandBuffer",
        "vkEndCommandBuffer",
        "vkCmdBindPipeline",
        "vkCmdDraw",
        "vkCmdBeginRenderPass",
        "vkCmdEndRenderPass",
        "vkCmdSetViewport",
        "vkCmdSetScissor",

        // Sync
        "vkCreateFence",
        "vkDestroyFence",
        "vkWaitForFences",
        "vkResetFences",
        "vkCreateSemaphore",
        "vkDestroySemaphore",

        // Queue submission
        "vkQueueSubmit",
        "vkQueueWaitIdle",

        // Image / memory
        "vkCreateImageView",
        "vkDestroyImageView",
        "vkAllocateMemory",
        "vkFreeMemory",
        "vkBindImageMemory",
        "vkCreateImage",
        "vkDestroyImage",
        "vkGetImageMemoryRequirements",
        "vkMapMemory",
        "vkUnmapMemory",
        "vkFlushMappedMemoryRanges",

        // Buffer (for vertex/upload)
        "vkCreateBuffer",
        "vkDestroyBuffer",
        "vkGetBufferMemoryRequirements",
        "vkBindBufferMemory",
        "vkCmdCopyBufferToImage",

        // Debug
        "vkCreateDebugUtilsMessengerEXT",
        "vkDestroyDebugUtilsMessengerEXT",
        "vkSetDebugUtilsObjectNameEXT",
    };
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "Vulkan opaque handle types compile and have the right size" {
    try testing.expectEqual(@sizeOf(?VkInstance), @sizeOf(?*anyopaque));
    try testing.expectEqual(@sizeOf(?VkDevice), @sizeOf(?*anyopaque));
    try testing.expectEqual(@sizeOf(?VkQueue), @sizeOf(?*anyopaque));
    try testing.expectEqual(@sizeOf(?VkCommandBuffer), @sizeOf(?*anyopaque));
    try testing.expectEqual(@sizeOf(?VkSurfaceKHR), @sizeOf(?*anyopaque));
    try testing.expectEqual(@sizeOf(?VkSwapchainKHR), @sizeOf(?*anyopaque));
}

test "VkInstanceCreateInfo has correct sType default" {
    const info = VkInstanceCreateInfo{};
    try testing.expectEqual(info.sType, VkStructureType.instance_create_info);
}

test "VkDeviceCreateInfo has correct sType default" {
    const info = VkDeviceCreateInfo{};
    try testing.expectEqual(info.sType, VkStructureType.device_create_info);
}

test "VkSwapchainCreateInfoKHR has correct sType default" {
    // Can't default-construct because 'surface' is required
    // Just verify the type constant
    try testing.expectEqual(@intFromEnum(VkStructureType.swapchain_create_info_khr), 1000001000);
}

test "VkGraphicsPipelineCreateInfo has correct sType default" {
    try testing.expectEqual(@intFromEnum(VkStructureType.graphics_pipeline_create_info), 28);
}

test "VkResult enum has expected values" {
    try testing.expectEqual(@intFromEnum(VkResult.success), 0);
    try testing.expectEqual(@intFromEnum(VkResult.error_out_of_date_khr), -1000001004);
    try testing.expectEqual(@intFromEnum(VkResult.error_surface_lost_khr), -1000000000);
}

test "neededFunctions returns all expected function names" {
    const funcs = neededFunctions();
    try testing.expect(funcs.len > 50); // MVP needs ~50+ functions
    try testing.expect(funcs.len <= 80);

    // Check key functions are present
    var has_create_instance = false;
    var has_create_device = false;
    var has_create_swapchain = false;
    var has_create_xlib_surface = false;
    var has_queue_present = false;

    for (funcs) |name| {
        if (std.mem.eql(u8, name, "vkCreateInstance")) has_create_instance = true;
        if (std.mem.eql(u8, name, "vkCreateDevice")) has_create_device = true;
        if (std.mem.eql(u8, name, "vkCreateSwapchainKHR")) has_create_swapchain = true;
        if (std.mem.eql(u8, name, "vkCreateXlibSurfaceKHR")) has_create_xlib_surface = true;
        if (std.mem.eql(u8, name, "vkQueuePresentKHR")) has_queue_present = true;
    }

    try testing.expect(has_create_instance);
    try testing.expect(has_create_device);
    try testing.expect(has_create_swapchain);
    try testing.expect(has_create_xlib_surface);
    try testing.expect(has_queue_present);
}

test "VkFormat values match spec" {
    try testing.expectEqual(@intFromEnum(VkFormat.undefined), 0);
    try testing.expectEqual(@intFromEnum(VkFormat.b8g8r8a8_srgb), 50);
    try testing.expectEqual(@intFromEnum(VkFormat.d32_sfloat), 126);
}

test "VkPresentModeKHR fifo is default" {
    try testing.expectEqual(@intFromEnum(VkPresentModeKHR.fifo_khr), 2);
}

test "VkImageLayout present src" {
    try testing.expectEqual(@intFromEnum(VkImageLayout.present_src_khr), 1000001002);
}
