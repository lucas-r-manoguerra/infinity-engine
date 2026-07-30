//! Memory management: arena allocator and pool allocator.
//!
//! The engine uses explicit allocators everywhere. No global malloc.
//! - Arena: for frame-scoped allocations (reset every frame)
//! - Pool: for fixed-size objects (entities, components)

const std = @import("std");
const builtin = @import("builtin");

/// Arena allocator for frame-scoped allocations.
/// All memory is freed at once when `reset()` is called.
pub const Arena = struct {
    buffer: []u8,
    offset: usize,
    alignment: u16,

    const Align = struct {
        pub const DEFAULT: u16 = 16;
    };

    pub fn init(buffer: []u8) Arena {
        return .{
            .buffer = buffer,
            .offset = 0,
            .alignment = Align.DEFAULT,
        };
    }

    pub fn allocator(self: *Arena) std.mem.Allocator {
        return .{
            .ptr = self,
            .vtable = &.{
                .alloc = allocImpl,
                .resize = resizeImpl,
                .free = freeImpl,
                .remap = remapImpl,
            },
        };
    }

    fn allocImpl(ctx: *anyopaque, len: usize, alignment: std.mem.Alignment, ra: usize) ?[*]u8 {
        _ = ra;
        const self: *Arena = @ptrCast(@alignCast(ctx));
        const align_bytes = alignment.toByteUnits();
        const aligned_offset = std.mem.alignForward(usize, self.offset, align_bytes);
        const new_offset = aligned_offset + len;
        if (new_offset > self.buffer.len) return null;
        self.offset = new_offset;
        return self.buffer.ptr + aligned_offset;
    }

    fn resizeImpl(ctx: *anyopaque, buf: []u8, alignment: std.mem.Alignment, new_len: usize, ra: usize) bool {
        _ = ctx;
        _ = buf;
        _ = alignment;
        _ = new_len;
        _ = ra;
        return false; // Arena does not support individual resizes
    }

    fn freeImpl(ctx: *anyopaque, buf: []u8, alignment: std.mem.Alignment, ra: usize) void {
        _ = ctx;
        _ = buf;
        _ = alignment;
        _ = ra;
        // Arena: free is a no-op
    }

    fn remapImpl(ctx: *anyopaque, buf: []u8, alignment: std.mem.Alignment, new_len: usize, ra: usize) ?[*]u8 {
        _ = ctx;
        _ = buf;
        _ = alignment;
        _ = new_len;
        _ = ra;
        return null; // Arena does not support remapping
    }

    /// Reset the arena, freeing all memory at once.
    pub fn reset(self: *Arena) void {
        self.offset = 0;
    }

    /// Returns the number of bytes used.
    pub fn used(self: Arena) usize {
        return self.offset;
    }

    /// Returns the number of bytes remaining.
    pub fn remaining(self: Arena) usize {
        return self.buffer.len - self.offset;
    }
};

/// Fixed-size object pool.
/// Efficient allocation/deallocation of same-sized items.
pub fn Pool(comptime T: type) type {
    return struct {
        const Self = @This();

        items: []T,
        free_list: std.ArrayListUnmanaged(usize),
        allocator: std.mem.Allocator,
        capacity: usize,

        pub fn init(allocator: std.mem.Allocator, capacity: usize) !Self {
            const items = try allocator.alloc(T, capacity);
            var free_list = try std.ArrayListUnmanaged(usize).initCapacity(allocator, capacity);
            // Initialize free list with all indices
            var i: usize = 0;
            while (i < capacity) : (i += 1) {
                free_list.appendAssumeCapacity(i);
            }
            return .{
                .items = items,
                .free_list = free_list,
                .allocator = allocator,
                .capacity = capacity,
            };
        }

        pub fn deinit(self: *Self) void {
            self.allocator.free(self.items);
            self.free_list.deinit(self.allocator);
        }

        /// Acquire an item from the pool. Returns null if pool is exhausted.
        pub fn acquire(self: *Self) ?*T {
            const index = self.free_list.pop() orelse return null;
            return &self.items[index];
        }

        /// Release an item back to the pool.
        pub fn release(self: *Self, item: *T) void {
            const index = @as(usize, @intCast(@intFromPtr(item) - @intFromPtr(self.items.ptr))) / @sizeOf(T);
            self.free_list.appendAssumeCapacity(index);
        }

        /// Number of available slots.
        pub fn available(self: Self) usize {
            return self.free_list.items.len;
        }

        /// Number of used slots.
        pub fn used(self: Self) usize {
            return self.capacity - self.free_list.items.len;
        }
    };
}

/// Frame allocator manager.
/// Tracks two arenas: one for the current frame, one for double-buffering.
pub const FrameAllocator = struct {
    arena_a: Arena,
    arena_b: Arena,
    current: *Arena,
    buffer_a: []u8,
    buffer_b: []u8,

    pub fn init(frame_buffer_size: usize) !FrameAllocator {
        const page_alloc = std.heap.page_allocator;
        const buffer_a = try page_alloc.alloc(u8, frame_buffer_size);
        const buffer_b = try page_alloc.alloc(u8, frame_buffer_size);
        return .{
            .buffer_a = buffer_a,
            .buffer_b = buffer_b,
            .arena_a = Arena.init(buffer_a),
            .arena_b = Arena.init(buffer_b),
            .current = undefined,
        };
    }

    pub fn deinit(self: *FrameAllocator) void {
        const page_alloc = std.heap.page_allocator;
        page_alloc.free(self.buffer_a);
        page_alloc.free(self.buffer_b);
    }

    /// Get the current frame's allocator.
    pub fn frameAllocator(self: *FrameAllocator) std.mem.Allocator {
        return self.current.allocator();
    }

    /// Begin a new frame: swap buffers and reset.
    pub fn beginFrame(self: *FrameAllocator) void {
        if (@intFromPtr(self.current) == @intFromPtr(&self.arena_a)) {
            self.current = &self.arena_b;
        } else {
            self.current = &self.arena_a;
        }
        self.current.reset();
    }
};

test "arena allocator basic operations" {
    var buffer: [1024]u8 = undefined;
    var arena = Arena.init(&buffer);
    const allocator = arena.allocator();

    const slice = try allocator.alloc(u8, 100);
    defer allocator.free(slice);

    try std.testing.expectEqual(@as(usize, 100), slice.len);
    try std.testing.expect(arena.used() >= 100);
}

test "arena reset frees all memory" {
    var buffer: [1024]u8 = undefined;
    var arena = Arena.init(&buffer);
    const allocator = arena.allocator();

    const slice = try allocator.alloc(u8, 200);
    _ = slice;
    try std.testing.expect(arena.used() >= 200);

    arena.reset();
    try std.testing.expectEqual(@as(usize, 0), arena.used());
}

test "pool acquire and release" {
    const allocator = std.testing.allocator;
    var pool = try Pool(u64).init(allocator, 4);
    defer pool.deinit();

    try std.testing.expectEqual(@as(usize, 4), pool.available());

    const item = pool.acquire().?;
    item.* = 42;
    try std.testing.expectEqual(@as(usize, 3), pool.available());

    pool.release(item);
    try std.testing.expectEqual(@as(usize, 4), pool.available());
}

test "pool exhaustion returns null" {
    const allocator = std.testing.allocator;
    var pool = try Pool(u64).init(allocator, 2);
    defer pool.deinit();

    _ = pool.acquire().?;
    _ = pool.acquire().?;
    try std.testing.expect(pool.acquire() == null);
}
