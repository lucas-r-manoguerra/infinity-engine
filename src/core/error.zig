//! Infinity Engine error set and result types.

const std = @import("std");

/// Top-level error set for the engine.
/// Every subsystem maps its errors into this set at boundary.
pub const Error = error{
    /// Initialization failed (platform, renderer, etc.)
    InitFailed,
    /// Window creation failed
    WindowInitFailed,
    /// Renderer backend failed to initialize
    RendererInitFailed,
    /// Platform-specific error (X11, Vulkan, etc.)
    PlatformError,
    /// Out of memory (allocator returned error)
    OutOfMemory,
    /// Entity or resource not found
    NotFound,
    /// Entity or resource already exists
    AlreadyExists,
    /// Invalid operation for current state
    InvalidOperation,
    /// Maximum capacity reached
    CapacityReached,
    /// Feature not yet implemented
    NotImplemented,

    /// libvulkan.so.1 not found or function resolution failed
    VulkanNotAvailable,
    /// Vulkan init sequence failed (instance/device creation)
    VulkanInitFailed,
    /// VK_KHR_xlib_surface extension not available
    VulkanSurfaceNotSupported,
};

/// Result shorthand for engine operations.
pub fn EngineResult(comptime T: type) type {
    return Error!T;
}
