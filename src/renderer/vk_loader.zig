//! Runtime Vulkan loader via dlopen + vkGetInstanceProcAddr.
//!
//! Loads `libvulkan.so.1` at runtime, resolves only `vkGetInstanceProcAddr`
//! (and `vkGetDeviceProcAddr`) from its symbol table, then uses those to
//! resolve ALL remaining Vulkan function pointers. This is the standard Vulkan
//! loader pattern — extension functions like `vkCreateXlibSurfaceKHR` are NOT
//! exported by `libvulkan.so.1` and must be queried via proc-addr after instance
//! creation.
//!
//! The `VkFunctions` struct keeps the dynamic library handle open for the
//! lifetime of the function pointers. Call `deinit()` when done to close it.
//! - No compile-time dependency on libvulkan
//! - Graceful software fallback when Vulkan is absent
//! - Follows the runtime feature detection pattern

const std = @import("std");
const testing = std.testing;
const vk = @import("vk.zig");
const Error = @import("../core/error.zig").Error;

// ---------------------------------------------------------------------------
// ProcAddr types
// ---------------------------------------------------------------------------

pub const GetInstanceProcAddr = *const fn (instance: ?vk.VkInstance, pName: [*:0]const u8) callconv(.c) ?*const anyopaque;
pub const GetDeviceProcAddr = *const fn (device: vk.VkDevice, pName: [*:0]const u8) callconv(.c) ?*const anyopaque;

// ---------------------------------------------------------------------------
// VkFunctions — resolved function pointers
// ---------------------------------------------------------------------------

pub const VkFunctions = struct {
    // The dynamic library handle — kept open for the lifetime of these pointers
    lib: std.DynLib,

    // Proc-addr resolvers (the only symbols we resolve directly from the .so)
    vkGetInstanceProcAddr: GetInstanceProcAddr,
    vkGetDeviceProcAddr: GetDeviceProcAddr,

    // Instance & device
    vkCreateInstance: *const fn (pCreateInfo: *const vk.VkInstanceCreateInfo, pAllocator: ?*const anyopaque, pInstance: *?vk.VkInstance) callconv(.c) vk.VkResult,
    vkDestroyInstance: *const fn (instance: vk.VkInstance, pAllocator: ?*const anyopaque) callconv(.c) void,
    vkEnumeratePhysicalDevices: *const fn (instance: vk.VkInstance, pPhysicalDeviceCount: *u32, pPhysicalDevices: ?[*]vk.VkPhysicalDevice) callconv(.c) vk.VkResult,
    vkGetPhysicalDeviceProperties: *const fn (physicalDevice: vk.VkPhysicalDevice, pProperties: *anyopaque) callconv(.c) void,
    vkGetPhysicalDeviceFeatures: *const fn (physicalDevice: vk.VkPhysicalDevice, pFeatures: *anyopaque) callconv(.c) void,
    vkGetPhysicalDeviceQueueFamilyProperties: *const fn (physicalDevice: vk.VkPhysicalDevice, pQueueFamilyPropertyCount: *u32, pQueueFamilyProperties: ?*anyopaque) callconv(.c) void,
    vkCreateDevice: *const fn (physicalDevice: vk.VkPhysicalDevice, pCreateInfo: *const vk.VkDeviceCreateInfo, pAllocator: ?*const anyopaque, pDevice: *?vk.VkDevice) callconv(.c) vk.VkResult,
    vkDestroyDevice: *const fn (device: vk.VkDevice, pAllocator: ?*const anyopaque) callconv(.c) void,
    vkGetDeviceQueue: *const fn (device: vk.VkDevice, queueFamilyIndex: u32, queueIndex: u32, pQueue: *?vk.VkQueue) callconv(.c) void,
    vkDeviceWaitIdle: *const fn (device: vk.VkDevice) callconv(.c) vk.VkResult,

    // Surface (KHR)
    vkDestroySurfaceKHR: *const fn (instance: vk.VkInstance, surface: vk.VkSurfaceKHR, pAllocator: ?*const anyopaque) callconv(.c) void,
    vkGetPhysicalDeviceSurfaceSupportKHR: *const fn (physicalDevice: vk.VkPhysicalDevice, queueFamilyIndex: u32, surface: vk.VkSurfaceKHR, pSupported: *vk.VkBool32) callconv(.c) vk.VkResult,
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR: *const fn (physicalDevice: vk.VkPhysicalDevice, surface: vk.VkSurfaceKHR, pSurfaceCapabilities: *vk.VkSurfaceCapabilitiesKHR) callconv(.c) vk.VkResult,
    vkGetPhysicalDeviceSurfaceFormatsKHR: *const fn (physicalDevice: vk.VkPhysicalDevice, surface: vk.VkSurfaceKHR, pSurfaceFormatCount: *u32, pSurfaceFormats: ?[*]vk.VkSurfaceFormatKHR) callconv(.c) vk.VkResult,
    vkGetPhysicalDeviceSurfacePresentModesKHR: *const fn (physicalDevice: vk.VkPhysicalDevice, surface: vk.VkSurfaceKHR, pPresentModeCount: *u32, pPresentModes: ?[*]vk.VkPresentModeKHR) callconv(.c) vk.VkResult,
    vkCreateXlibSurfaceKHR: *const fn (instance: vk.VkInstance, pCreateInfo: *const vk.VkXlibSurfaceCreateInfoKHR, pAllocator: ?*const anyopaque, pSurface: *?vk.VkSurfaceKHR) callconv(.c) vk.VkResult,

    // Swapchain (KHR)
    vkCreateSwapchainKHR: *const fn (device: vk.VkDevice, pCreateInfo: *const vk.VkSwapchainCreateInfoKHR, pAllocator: ?*const anyopaque, pSwapchain: *?vk.VkSwapchainKHR) callconv(.c) vk.VkResult,
    vkDestroySwapchainKHR: *const fn (device: vk.VkDevice, swapchain: vk.VkSwapchainKHR, pAllocator: ?*const anyopaque) callconv(.c) void,
    vkGetSwapchainImagesKHR: *const fn (device: vk.VkDevice, swapchain: vk.VkSwapchainKHR, pSwapchainImageCount: *u32, pSwapchainImages: ?[*]vk.VkImage) callconv(.c) vk.VkResult,
    vkAcquireNextImageKHR: *const fn (device: vk.VkDevice, swapchain: vk.VkSwapchainKHR, timeout: u64, semaphore: ?vk.VkSemaphore, fence: ?vk.VkFence, pImageIndex: *u32) callconv(.c) vk.VkResult,
    vkQueuePresentKHR: *const fn (queue: vk.VkQueue, pPresentInfo: *const vk.VkPresentInfoKHR) callconv(.c) vk.VkResult,

    // Shader module & pipeline
    vkCreateShaderModule: *const fn (device: vk.VkDevice, pCreateInfo: *const vk.VkShaderModuleCreateInfo, pAllocator: ?*const anyopaque, pShaderModule: *?vk.VkShaderModule) callconv(.c) vk.VkResult,
    vkDestroyShaderModule: *const fn (device: vk.VkDevice, shaderModule: vk.VkShaderModule, pAllocator: ?*const anyopaque) callconv(.c) void,
    vkCreatePipelineLayout: *const fn (device: vk.VkDevice, pCreateInfo: *const vk.VkPipelineLayoutCreateInfo, pAllocator: ?*const anyopaque, pPipelineLayout: *?vk.VkPipelineLayout) callconv(.c) vk.VkResult,
    vkDestroyPipelineLayout: *const fn (device: vk.VkDevice, pipelineLayout: vk.VkPipelineLayout, pAllocator: ?*const anyopaque) callconv(.c) void,
    vkCreateGraphicsPipelines: *const fn (device: vk.VkDevice, pipelineCache: ?*anyopaque, createInfoCount: u32, pCreateInfos: *const vk.VkGraphicsPipelineCreateInfo, pAllocator: ?*const anyopaque, pPipelines: [*]?vk.VkPipeline) callconv(.c) vk.VkResult,
    vkDestroyPipeline: *const fn (device: vk.VkDevice, pipeline: vk.VkPipeline, pAllocator: ?*const anyopaque) callconv(.c) void,
    vkCreateRenderPass: *const fn (device: vk.VkDevice, pCreateInfo: *const vk.VkRenderPassCreateInfo, pAllocator: ?*const anyopaque, pRenderPass: *?vk.VkRenderPass) callconv(.c) vk.VkResult,
    vkDestroyRenderPass: *const fn (device: vk.VkDevice, renderPass: vk.VkRenderPass, pAllocator: ?*const anyopaque) callconv(.c) void,
    vkCreateFramebuffer: *const fn (device: vk.VkDevice, pCreateInfo: *const vk.VkFramebufferCreateInfo, pAllocator: ?*const anyopaque, pFramebuffer: *?vk.VkFramebuffer) callconv(.c) vk.VkResult,
    vkDestroyFramebuffer: *const fn (device: vk.VkDevice, framebuffer: vk.VkFramebuffer, pAllocator: ?*const anyopaque) callconv(.c) void,

    // Command buffers
    vkCreateCommandPool: *const fn (device: vk.VkDevice, pCreateInfo: *const vk.VkCommandPoolCreateInfo, pAllocator: ?*const anyopaque, pCommandPool: *?vk.VkCommandPool) callconv(.c) vk.VkResult,
    vkDestroyCommandPool: *const fn (device: vk.VkDevice, commandPool: vk.VkCommandPool, pAllocator: ?*const anyopaque) callconv(.c) void,
    vkAllocateCommandBuffers: *const fn (device: vk.VkDevice, pAllocateInfo: *const vk.VkCommandBufferAllocateInfo, pCommandBuffers: [*]?vk.VkCommandBuffer) callconv(.c) vk.VkResult,
    vkFreeCommandBuffers: *const fn (device: vk.VkDevice, commandPool: vk.VkCommandPool, commandBufferCount: u32, pCommandBuffers: [*]const vk.VkCommandBuffer) callconv(.c) void,
    vkBeginCommandBuffer: *const fn (commandBuffer: vk.VkCommandBuffer, pBeginInfo: *const vk.VkCommandBufferBeginInfo) callconv(.c) vk.VkResult,
    vkEndCommandBuffer: *const fn (commandBuffer: vk.VkCommandBuffer) callconv(.c) vk.VkResult,
    vkCmdBindPipeline: *const fn (commandBuffer: vk.VkCommandBuffer, pipelineBindPoint: vk.VkPipelineBindPoint, pipeline: vk.VkPipeline) callconv(.c) void,
    vkCmdDraw: *const fn (commandBuffer: vk.VkCommandBuffer, vertexCount: u32, instanceCount: u32, firstVertex: u32, firstInstance: u32) callconv(.c) void,
    vkCmdBeginRenderPass: *const fn (commandBuffer: vk.VkCommandBuffer, pRenderPassBegin: *const vk.VkRenderPassBeginInfo, contents: vk.VkSubpassContents) callconv(.c) void,
    vkCmdEndRenderPass: *const fn (commandBuffer: vk.VkCommandBuffer) callconv(.c) void,
    vkCmdSetViewport: *const fn (commandBuffer: vk.VkCommandBuffer, firstViewport: u32, viewportCount: u32, pViewports: *const vk.VkViewport) callconv(.c) void,
    vkCmdSetScissor: *const fn (commandBuffer: vk.VkCommandBuffer, firstScissor: u32, scissorCount: u32, pScissors: *const vk.VkRect2D) callconv(.c) void,

    // Sync
    vkCreateFence: *const fn (device: vk.VkDevice, pCreateInfo: *const vk.VkFenceCreateInfo, pAllocator: ?*const anyopaque, pFence: *?vk.VkFence) callconv(.c) vk.VkResult,
    vkDestroyFence: *const fn (device: vk.VkDevice, fence: vk.VkFence, pAllocator: ?*const anyopaque) callconv(.c) void,
    vkWaitForFences: *const fn (device: vk.VkDevice, fenceCount: u32, pFences: [*]const vk.VkFence, waitAll: vk.VkBool32, timeout: u64) callconv(.c) vk.VkResult,
    vkResetFences: *const fn (device: vk.VkDevice, fenceCount: u32, pFences: [*]const vk.VkFence) callconv(.c) vk.VkResult,
    vkCreateSemaphore: *const fn (device: vk.VkDevice, pCreateInfo: *const vk.VkSemaphoreCreateInfo, pAllocator: ?*const anyopaque, pSemaphore: *?vk.VkSemaphore) callconv(.c) vk.VkResult,
    vkDestroySemaphore: *const fn (device: vk.VkDevice, semaphore: vk.VkSemaphore, pAllocator: ?*const anyopaque) callconv(.c) void,

    // Queue submission
    vkQueueSubmit: *const fn (queue: vk.VkQueue, submitCount: u32, pSubmits: [*]const vk.VkSubmitInfo, fence: ?vk.VkFence) callconv(.c) vk.VkResult,
    vkQueueWaitIdle: *const fn (queue: vk.VkQueue) callconv(.c) vk.VkResult,

    // Image / memory
    vkCreateImageView: *const fn (device: vk.VkDevice, pCreateInfo: *const vk.VkImageViewCreateInfo, pAllocator: ?*const anyopaque, pView: *?vk.VkImageView) callconv(.c) vk.VkResult,
    vkDestroyImageView: *const fn (device: vk.VkDevice, imageView: vk.VkImageView, pAllocator: ?*const anyopaque) callconv(.c) void,
    vkAllocateMemory: *const fn (device: vk.VkDevice, pAllocateInfo: *const vk.VkMemoryAllocateInfo, pAllocator: ?*const anyopaque, pMemory: *?vk.VkDeviceMemory) callconv(.c) vk.VkResult,
    vkFreeMemory: *const fn (device: vk.VkDevice, memory: vk.VkDeviceMemory, pAllocator: ?*const anyopaque) callconv(.c) void,
    vkBindImageMemory: *const fn (device: vk.VkDevice, image: vk.VkImage, memory: vk.VkDeviceMemory, memoryOffset: vk.VkDeviceSize) callconv(.c) vk.VkResult,
    vkCreateImage: *const fn (device: vk.VkDevice, pCreateInfo: *const vk.VkImageCreateInfo, pAllocator: ?*const anyopaque, pImage: *?vk.VkImage) callconv(.c) vk.VkResult,
    vkDestroyImage: *const fn (device: vk.VkDevice, image: vk.VkImage, pAllocator: ?*const anyopaque) callconv(.c) void,
    vkGetImageMemoryRequirements: *const fn (device: vk.VkDevice, image: vk.VkImage, pMemoryRequirements: *anyopaque) callconv(.c) void,
    vkMapMemory: *const fn (device: vk.VkDevice, memory: vk.VkDeviceMemory, offset: vk.VkDeviceSize, size: vk.VkDeviceSize, flags: vk.VkFlags, ppData: *?*anyopaque) callconv(.c) vk.VkResult,
    vkUnmapMemory: *const fn (device: vk.VkDevice, memory: vk.VkDeviceMemory) callconv(.c) void,
    vkFlushMappedMemoryRanges: *const fn (device: vk.VkDevice, memoryRangeCount: u32, pMemoryRanges: [*]const vk.VkMappedMemoryRange) callconv(.c) vk.VkResult,

    // Buffer (for vertex/upload)
    vkCreateBuffer: *const fn (device: vk.VkDevice, pCreateInfo: *const vk.VkBufferCreateInfo, pAllocator: ?*const anyopaque, pBuffer: *?vk.VkBuffer) callconv(.c) vk.VkResult,
    vkDestroyBuffer: *const fn (device: vk.VkDevice, buffer: vk.VkBuffer, pAllocator: ?*const anyopaque) callconv(.c) void,
    vkGetBufferMemoryRequirements: *const fn (device: vk.VkDevice, buffer: vk.VkBuffer, pMemoryRequirements: *anyopaque) callconv(.c) void,
    vkBindBufferMemory: *const fn (device: vk.VkDevice, buffer: vk.VkBuffer, memory: vk.VkDeviceMemory, memoryOffset: vk.VkDeviceSize) callconv(.c) vk.VkResult,
    vkCmdCopyBufferToImage: *const fn (commandBuffer: vk.VkCommandBuffer, srcBuffer: vk.VkBuffer, dstImage: vk.VkImage, layout: vk.VkImageLayout, regionCount: u32, pRegions: *const vk.VkBufferImageCopy) callconv(.c) void,

    // Debug
    vkCreateDebugUtilsMessengerEXT: *const fn (instance: vk.VkInstance, pCreateInfo: *const anyopaque, pAllocator: ?*const anyopaque, pMessenger: *?vk.VkDebugUtilsMessengerEXT) callconv(.c) vk.VkResult,
    vkDestroyDebugUtilsMessengerEXT: *const fn (instance: vk.VkInstance, messenger: vk.VkDebugUtilsMessengerEXT, pAllocator: ?*const anyopaque) callconv(.c) void,
    vkSetDebugUtilsObjectNameEXT: *const fn (device: vk.VkDevice, pNameInfo: *const anyopaque) callconv(.c) vk.VkResult,

    // ------------------------------------------------------------------
    // Loader methods
    // ------------------------------------------------------------------

    /// Load libvulkan.so.1, resolve vkGetInstanceProcAddr + vkGetDeviceProcAddr,
    /// and resolve all loader-level functions accessible without an instance.
    pub fn load() Error!VkFunctions {
        var lib = std.DynLib.open("libvulkan.so.1") catch {
            return error.VulkanNotAvailable;
        };
        errdefer lib.close();

        const get_proc = lib.lookup(GetInstanceProcAddr, "vkGetInstanceProcAddr") orelse {
            return error.VulkanNotAvailable;
        };
        const get_dev = lib.lookup(GetDeviceProcAddr, "vkGetDeviceProcAddr") orelse {
            return error.VulkanNotAvailable;
        };

        // Pre-allocate — resolve ALL possible fields BEFORE moving lib
        var self: VkFunctions = undefined;

        inline for (std.meta.fields(VkFunctions)) |field| {
            if (comptime isInternalField(field.name)) continue;

            // Try vkGetInstanceProcAddr(null, …) first, fall back to .so symbol
            // table. This resolves core Vulkan functions without needing an
            // instance. Extension functions (KHR/EXT) are NOT exported by the
            // .so and stay undefined — resolved in resolveInstanceFunctions().
            const maybe_ptr = get_proc(null, @ptrCast(field.name)) orelse
                lib.lookup(*const anyopaque, field.name);

            if (maybe_ptr) |ptr| {
                @field(self, field.name) = @ptrCast(@alignCast(ptr));
            }
        }

        // NOW move lib into self (after all lib.lookup calls are done)
        self.lib = lib;
        self.vkGetInstanceProcAddr = get_proc;
        self.vkGetDeviceProcAddr = get_dev;

        return self;
    }

    /// Resolve all remaining function pointers that require a VkInstance.
    /// Call this AFTER creating the instance.
    pub fn resolveInstanceFunctions(self: *VkFunctions, instance: vk.VkInstance) void {
        // Resolve every field via vkGetInstanceProcAddr — already-resolved
        // functions return the same pointer, so this is safe to call on all
        // fields regardless of prior resolution state.
        inline for (std.meta.fields(VkFunctions)) |field| {
            if (comptime isInternalField(field.name)) continue;

            if (self.vkGetInstanceProcAddr(instance, @ptrCast(field.name))) |ptr| {
                @field(self, field.name) = @ptrCast(@alignCast(ptr));
            }
        }
    }

    /// Close the dynamic library handle.
    pub fn deinit(self: *VkFunctions) void {
        self.lib.close();
    }
};

/// Returns true for the internal fields that are not Vulkan function pointers.
fn isInternalField(name: []const u8) bool {
    return std.mem.eql(u8, name, "lib") or
        std.mem.eql(u8, name, "vkGetInstanceProcAddr") or
        std.mem.eql(u8, name, "vkGetDeviceProcAddr");
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "vk_loader: load, create instance, resolve, destroy" {
    // End-to-end test: load Vulkan, create a minimal instance, resolve
    // extension function pointers, verify they work, clean up.
    const result = VkFunctions.load();
    if (result) |funcs_val| {
        var funcs = funcs_val;
        defer funcs.deinit();

        const app_info = vk.VkApplicationInfo{
            .sType = .application_info,
            .pNext = null,
            .pApplicationName = "test",
            .applicationVersion = 1,
            .pEngineName = "test",
            .engineVersion = 1,
            .apiVersion = vk.VK_API_VERSION_1_0,
        };
        const create_info = vk.VkInstanceCreateInfo{
            .sType = .instance_create_info,
            .pNext = null,
            .flags = 0,
            .pApplicationInfo = &app_info,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = null,
            .enabledExtensionCount = 0,
            .ppEnabledExtensionNames = null,
        };
        var instance: ?vk.VkInstance = undefined;
        const vr = funcs.vkCreateInstance(&create_info, null, &instance);
        try testing.expectEqual(vk.VkResult.success, vr);

        // Resolve extension functions that need a VkInstance
        funcs.resolveInstanceFunctions(instance.?);

        // Clean up
        funcs.vkDestroyInstance(instance.?, null);
    } else |err| {
        try testing.expectEqual(err, error.VulkanNotAvailable);
    }
}

test "vk_loader: Vulkan function validation test" {
    // Ensures VkFunctions struct has the expected number of Vulkan function fields
    const expected_vk_field_count = vk.neededFunctions().len;
    var vk_field_count: usize = 0;
    inline for (std.meta.fields(VkFunctions)) |field| {
        if (!comptime isInternalField(field.name)) {
            vk_field_count += 1;
        }
    }
    try testing.expectEqual(expected_vk_field_count, vk_field_count);
}

test "vk_loader: each needed function name matches a VkFunctions field" {
    const funcs = vk.neededFunctions();
    const type_info2 = @typeInfo(VkFunctions);
    const fields = switch (type_info2) {
        .@"struct" => |s| s.fields,
        else => @compileError("VkFunctions must be a struct"),
    };

    // Every function name should have a corresponding struct field
    for (funcs) |name| {
        var found = false;
        inline for (fields) |field| {
            if (comptime isInternalField(field.name)) continue;
            if (std.mem.eql(u8, field.name, name)) {
                found = true;
                break;
            }
        }
        try testing.expect(found);
    }

    // Every struct field should have a corresponding function name
    inline for (fields) |field| {
        if (comptime isInternalField(field.name)) continue;
        var found = false;
        for (funcs) |name| {
            if (std.mem.eql(u8, field.name, name)) {
                found = true;
                break;
            }
        }
        try testing.expect(found);
    }
}
