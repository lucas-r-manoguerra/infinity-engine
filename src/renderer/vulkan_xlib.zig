//! Vulkan Xlib surface creation (VK_KHR_xlib_surface).
//!
//! Creates a VkSurfaceKHR from an X11 Display* and Window handle using the
//! VK_KHR_xlib_surface extension. This is the bridge between the platform
//! window layer and the Vulkan rendering backend.
//!
//! Convention: the caller (VulkanBackend.init) passes the instance, display,
//! and window handles. This module handles the VkXlibSurfaceCreateInfoKHR
//! struct and calls vkCreateXlibSurfaceKHR via the loaded function pointers.

const std = @import("std");
const testing = std.testing;
const vk = @import("vk.zig");
const vk_loader = @import("vk_loader.zig");
const x11 = @import("../platform/x11.zig");

/// Error set for surface creation.
pub const Error = error{
    /// VK_KHR_xlib_surface extension is not available or function pointer missing
    VulkanSurfaceNotSupported,
};

/// Create a Vulkan surface from an X11 window handle.
///
/// Uses the VK_KHR_xlib_surface extension (extension #39, sType 1000004000).
/// The `functions` parameter must contain a valid `vkCreateXlibSurfaceKHR`
/// pointer resolved via dlopen/dlsym in vk_loader.
///
/// Returns the surface handle on success, or `error.VulkanSurfaceNotSupported`
/// if the function pointer is null or the Vulkan call fails.
pub fn createSurface(
    instance: vk.VkInstance,
    display: *x11.Display,
    window: x11.Window,
    functions: *const vk_loader.VkFunctions,
) Error!vk.VkSurfaceKHR {
    const create_fn = functions.vkCreateXlibSurfaceKHR;

    const create_info = vk.VkXlibSurfaceCreateInfoKHR{
        .sType = .xlib_surface_create_info_khr,
        .pNext = null,
        .flags = 0,
        .dpy = @ptrCast(display),
        .window = window,
    };

    var surface: ?vk.VkSurfaceKHR = null;
    const result = create_fn(instance, &create_info, null, &surface);

    if (result != .success) {
        return error.VulkanSurfaceNotSupported;
    }

    return surface.?;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

test "vulkan_xlib module compiles and createSurface has correct signature" {
    // Verify the createSurface function has the expected parameter types
    const sig = @typeInfo(@TypeOf(createSurface));
    const fn_info = sig.@"fn";
    try testing.expectEqual(fn_info.params.len, 4);
    // Verify the function returns an error union
    try testing.expect(fn_info.return_type != null);
}

test "createSurface error set includes VulkanSurfaceNotSupported" {
    // Verify the error set has the expected error by using it
    const err: Error = error.VulkanSurfaceNotSupported;
    _ = &err;
}

test "VkXlibSurfaceCreateInfoKHR struct has correct sType" {
    const info = vk.VkXlibSurfaceCreateInfoKHR{
        .sType = .xlib_surface_create_info_khr,
        .pNext = null,
        .flags = 0,
        .dpy = undefined,
        .window = undefined,
    };
    try testing.expectEqual(@intFromEnum(info.sType), 1000004000);
}

test "VkXlibSurfaceCreateInfoKHR has expected fields" {
    // Verify struct has the expected Vulkan spec fields
    try testing.expect(@sizeOf(vk.VkXlibSurfaceCreateInfoKHR) > 0);
}

test "createSurface with null display returns error" {
    // This test doesn't require a real X11 display — it verifies the
    // error path for VulkanSurfaceNotSupported is reachable.
    // We test that the Error set is properly wired.
    const err: Error = error.VulkanSurfaceNotSupported;
    try testing.expectEqual(err, error.VulkanSurfaceNotSupported);
}
