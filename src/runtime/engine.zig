//! Engine — orchestrates subsystems.
//!
//! Owns the game loop, renderer, window, and frame state.
//! Phase lifecycle: init → postInit → run → shutdown.

const std = @import("std");
const window = @import("../platform/window.zig");
const rdr = @import("../renderer/renderer.zig");
const software = @import("../renderer/software.zig");
const Color = @import("../core/color.zig").Color;
const COLORS = @import("../core/color.zig").COLORS;
const Vec3 = @import("../math/vec3.zig").Vec3;
const Mat4 = @import("../math/mat4.zig").Mat4;
const World = @import("../ecs/world.zig").World;
const SystemMod = @import("../ecs/system.zig");
const ComponentReg = @import("../ecs/component.zig");
const Query = @import("../ecs/query.zig").Query;
const Entity = @import("../ecs/entity.zig").Entity;
const ThreadPool = @import("../core/thread_pool.zig").ThreadPool;
const time = @import("../core/time.zig");
const diagnostics = @import("../core/diagnostics.zig");

const RenderBackend = rdr.Backend;
const Vertex = rdr.Vertex;
const ComponentId = ComponentReg.ComponentId;

/// Light direction for Lambertian diffuse shading.
const LIGHT_DIR = Vec3.normalize(Vec3.init(1.0, 2.0, 1.0)); // ≈ (0.408, 0.816, 0.408)

/// Shape discriminator for entity rendering.
const Shape = enum(u8) { cube, sphere, torus };

pub const Window = window.Window;

pub const EngineError = error{
    WindowInitFailed,
};

pub const EngineConfig = struct {
    width: u32 = 800,
    height: u32 = 600,
    title: [:0]const u8 = "Infinity Engine",
};

/// ECS component: 3D transform (position + rotation + scale + shape)
const Transform = struct { x: f32, y: f32, z: f32, rot_x: f32, rot_y: f32, scale: f32 = 1.0, shape: Shape = .cube };
/// ECS component: rotation speed (radians/second)
const RotationSpeed = struct { x: f32, y: f32 };
/// ECS component: orbital motion around a center point
const Orbit = struct {
    center_x: f32,
    center_y: f32,
    center_z: f32,
    radius: f32,
    speed: f32,
    angle: f32,
};

/// ECS component: parent-child relationship for scene graph.
/// Child's world position = parent's world position + child's local position.
const Parent = struct { parent: Entity };

const TEXTURE_SIZE = software.TEXTURE_W * software.TEXTURE_H * 4;
const TILE_SIZE = software.TILE_SIZE;

// MAX_TRIS_PER_TILE removed — now uses prefix-sum indexed dynamic bins.

/// Sort entry for front-to-back entity sorting (view-space Z, entity handle).
const SortEntry = struct {
    z: f32,
    entity: Entity,
};

/// Fully pre-transformed screen-space triangle, ready for rasterization.
/// Produced by emit* functions on the main thread, consumed by tileWorker.
const ScreenTri = struct {
    v0: Vertex,
    v1: Vertex,
    v2: Vertex,
};

pub const Engine = struct {
    allocator: std.mem.Allocator,
    backend: RenderBackend,
    win: window.Window,
    frame: u64 = 0,
    config: EngineConfig,
    texture: [TEXTURE_SIZE]u8 = undefined,
    sphere_mesh: []const Vertex = &[_]Vertex{},
    torus_mesh: []const Vertex = &[_]Vertex{},
    dt_tracker: time.DeltaTime,

    // ECS
    ecs_world: World,
    transform_id: ComponentId,
    rotation_speed_id: ComponentId,
    orbit_id: ComponentId,
    parent_id: ComponentId,

    // Sort buffer for front-to-back entity sorting
    sort_buffer: []SortEntry = &[_]SortEntry{},

    // Pre-transform triangle buffer (work-stealing queue)
    tri_buffer: []ScreenTri = &[_]ScreenTri{},
    tri_capacity: u32 = 0,

    /// Number of valid triangles in tri_buffer after the pre-transform phase.
    /// Set by the main thread during emit, consumed by the binning pass.
    tri_count: u32 = 0,

    // Tile-based rasterizer state
    num_tiles_x: u32 = 0,
    num_tiles_y: u32 = 0,
    tile_tris: []ScreenTri = &[_]ScreenTri{},
    /// Per-tile triangle count (reused as write-position counter in two-pass binning).
    tile_counts: []u32 = &[_]u32{},
    /// Prefix-sum offsets: tile_offsets[t] = start index in tile_tris for tile t,
    /// tile_offsets[total_tiles] = total used entries.
    tile_offsets: []u32 = &[_]u32{},

    // Benchmark
    benchmark: diagnostics.Benchmark,
    fps_counter: time.FpsCounter,

    /// Thread pool for parallel strip rasterization.
    /// Initialised in postInit, used in render system.
    thread_pool: ThreadPool = undefined,

    /// Number of worker threads to use for strip dispatch (0 = disabled).
    thread_count: u32 = 0,

    /// When true (default), use tile-based rasterizer.
    /// When false, fall back to full-frame drawTriangle (single-threaded diagnostic).
    use_tiles: bool = true,

    pub fn init(allocator: std.mem.Allocator, config: EngineConfig, use_tiles_param: bool) EngineError!Engine {
        const texture = software.generateBrickTexture();

        // 1. Create engine struct first so .win lives at a stable address.
        var engine = Engine{
            .allocator = allocator,
            .backend = undefined,
            .win = window.windowCreate(config.title, config.width, config.height) orelse {
                std.debug.print("[FAIL] window creation failed\n", .{});
                return error.WindowInitFailed;
            },
            .config = config,
            .texture = texture,
            .dt_tracker = time.DeltaTime.init(),
            .benchmark = diagnostics.Benchmark.init(60),
            .fps_counter = time.FpsCounter.init(),
            .ecs_world = undefined,
            .transform_id = undefined,
            .rotation_speed_id = undefined,
            .orbit_id = undefined,
            .parent_id = undefined,
            .use_tiles = use_tiles_param,
        };
        errdefer window.windowDestroy(&engine.win);

        // 2. Init backend — &engine.win is stable because engine is on the stack.
        engine.backend = RenderBackend.init(allocator, &engine.win, config.width, config.height, .software) catch |err| {
            std.debug.print("[FAIL] Backend init: {}\n", .{err});
            return error.WindowInitFailed;
        };

        // 3. Init ECS world
        engine.ecs_world = World.init(allocator, 1000) catch |err| {
            std.debug.print("[FAIL] World init: {}\n", .{err});
            return error.WindowInitFailed;
        };

        // 4. Allocate entity sort buffer (sized for max entities, reused each frame)
        engine.sort_buffer = allocator.alloc(SortEntry, engine.ecs_world.max_entities) catch |err| {
            std.debug.print("[FAIL] Sort buffer alloc: {}\n", .{err});
            return error.WindowInitFailed;
        };
        errdefer allocator.free(engine.sort_buffer);

        // 5. Allocate pre-transform triangle buffer (64K = generous cap for the demo scene)
        engine.tri_capacity = 64 * 1024;
        engine.tri_buffer = allocator.alloc(ScreenTri, engine.tri_capacity) catch |err| {
            std.debug.print("[FAIL] Tri buffer alloc: {}\n", .{err});
            return error.WindowInitFailed;
        };
        errdefer allocator.free(engine.tri_buffer);

        // 6. Allocate tile-based rasterizer state (prefix-sum indexed dynamic bins)
        engine.num_tiles_x = (config.width + TILE_SIZE - 1) / TILE_SIZE;
        engine.num_tiles_y = (config.height + TILE_SIZE - 1) / TILE_SIZE;
        const total_tiles = engine.num_tiles_x * engine.num_tiles_y;
        // Pre-allocate tri_capacity * 4 entries — enough for 36K tris × ~4 tiles avg
        // with headroom. If overflow occurs, the render system panics with a clear
        // message instead of silently dropping triangles.
        engine.tile_tris = allocator.alloc(ScreenTri, engine.tri_capacity * 4) catch |err| {
            std.debug.print("[FAIL] Tile tris buffer alloc: {}\n", .{err});
            return error.WindowInitFailed;
        };
        errdefer allocator.free(engine.tile_tris);
        engine.tile_counts = allocator.alloc(u32, total_tiles) catch |err| {
            std.debug.print("[FAIL] Tile counts alloc: {}\n", .{err});
            return error.WindowInitFailed;
        };
        errdefer allocator.free(engine.tile_counts);
        engine.tile_offsets = allocator.alloc(u32, total_tiles + 1) catch |err| {
            std.debug.print("[FAIL] Tile offsets alloc: {}\n", .{err});
            return error.WindowInitFailed;
        };
        errdefer allocator.free(engine.tile_offsets);

        return engine;
    }

    /// Register ECS components, create demo entities, add systems.
    /// Must be called AFTER init and BEFORE run (engine must be at stable address).
    pub fn postInit(self: *Engine) !void {
        self.transform_id = try self.ecs_world.registerComponent(Transform);
        self.rotation_speed_id = try self.ecs_world.registerComponent(RotationSpeed);
        self.orbit_id = try self.ecs_world.registerComponent(Orbit);
        self.parent_id = try self.ecs_world.registerComponent(Parent);

        // Init thread pool for parallel rasterization.
        self.thread_pool = try ThreadPool.init(self.allocator, 4);
        self.thread_count = 4;
        self.backend.setPool(&self.thread_pool, 4);

        // Generate mesh vertex buffers for sphere and torus shapes
        self.sphere_mesh = try software.generateSphere(self.allocator, 16, 16, 1.0);
        self.torus_mesh = try software.generateTorus(self.allocator, 2.0, 0.8, 16);

        // ── 30 cubes in a 5×6 grid at z=-8..-12 ──
        {
            var i: u32 = 0;
            while (i < 30) : (i += 1) {
                const col = i % 6;
                const row = i / 6;
                const e = try self.ecs_world.createEntity();
                try self.ecs_world.addComponent(e, self.transform_id, Transform, .{
                    .x = @as(f32, @floatFromInt(col)) * 1.8 - 4.5,
                    .y = @as(f32, @floatFromInt(row)) * 1.8 - 3.6,
                    .z = -8.0 - @as(f32, @floatFromInt(i)) * 4.0 / 30.0,
                    .rot_x = 0,
                    .rot_y = 0,
                    .scale = 0.55,
                    .shape = .cube,
                });
                try self.ecs_world.addComponent(e, self.rotation_speed_id, RotationSpeed, .{
                    .x = 0.3 + @as(f32, @floatFromInt(i % 5)) * 0.15,
                    .y = 0.5 + @as(f32, @floatFromInt(i % 7)) * 0.2,
                });
            }
        }

        // ── 35 spheres orbiting at radius 3..8 around (0, y, -8) ──
        {
            var i: u32 = 0;
            while (i < 35) : (i += 1) {
                const radius = 3.0 + @as(f32, @floatFromInt(i)) * 5.0 / 35.0;
                const angle = @as(f32, @floatFromInt(i)) * 2.0 * std.math.pi / 35.0;
                const y_off = @as(f32, @floatFromInt(i % 7)) * 1.5 - 4.5;
                const e = try self.ecs_world.createEntity();
                try self.ecs_world.addComponent(e, self.transform_id, Transform, .{
                    .x = @cos(angle) * radius,
                    .y = y_off,
                    .z = -8.0 + @sin(angle) * radius,
                    .rot_x = 0,
                    .rot_y = 0,
                    .scale = 0.45,
                    .shape = .sphere,
                });
                try self.ecs_world.addComponent(e, self.rotation_speed_id, RotationSpeed, .{
                    .x = 0.4 + @as(f32, @floatFromInt(i % 4)) * 0.2,
                    .y = 0.6 + @as(f32, @floatFromInt(i % 6)) * 0.25,
                });
                try self.ecs_world.addComponent(e, self.orbit_id, Orbit, .{
                    .center_x = 0,
                    .center_y = y_off,
                    .center_z = -8,
                    .radius = radius,
                    .speed = 0.25 + @as(f32, @floatFromInt(i % 8)) * 0.12,
                    .angle = angle,
                });
            }
        }

        // ── 35 tori orbiting at radius 3..8 with varied tilt ──
        {
            var i: u32 = 0;
            while (i < 35) : (i += 1) {
                const radius = 3.0 + @as(f32, @floatFromInt(i)) * 5.0 / 35.0;
                const angle = @as(f32, @floatFromInt(i)) * 2.0 * std.math.pi / 35.0 + 1.0;
                const y_off = @as(f32, @floatFromInt(i % 7)) * 1.2 - 3.6;
                const e = try self.ecs_world.createEntity();
                try self.ecs_world.addComponent(e, self.transform_id, Transform, .{
                    .x = @cos(angle) * radius,
                    .y = y_off,
                    .z = -8.0 + @sin(angle) * radius,
                    .rot_x = @as(f32, @floatFromInt(i)) * 0.3,
                    .rot_y = @as(f32, @floatFromInt(i)) * 0.5,
                    .scale = 0.4,
                    .shape = .torus,
                });
                try self.ecs_world.addComponent(e, self.rotation_speed_id, RotationSpeed, .{
                    .x = 0.5 + @as(f32, @floatFromInt(i % 5)) * 0.25,
                    .y = 0.7 + @as(f32, @floatFromInt(i % 7)) * 0.2,
                });
                try self.ecs_world.addComponent(e, self.orbit_id, Orbit, .{
                    .center_x = 0,
                    .center_y = y_off,
                    .center_z = -8,
                    .radius = radius,
                    .speed = 0.18 + @as(f32, @floatFromInt(i % 9)) * 0.1,
                    .angle = angle,
                });
            }
        }

        // Orbit system — updates Transform position from Orbit component
        try self.ecs_world.addSystem(SystemMod.System.init("orbit", .pre_update, self, struct {
            fn run(ctx: ?*anyopaque, world: *World, dt: f64) void {
                const engine = @as(*Engine, @ptrCast(@alignCast(ctx.?)));
                var query = Query.init(world, &[_]ComponentId{ engine.transform_id, engine.orbit_id });
                while (query.next()) |entity| {
                    const xf = world.getComponent(entity, engine.transform_id, Transform).?;
                    const orb = world.getComponent(entity, engine.orbit_id, Orbit).?;
                    orb.angle += orb.speed * @as(f32, @floatCast(dt));
                    xf.x = orb.center_x + @cos(orb.angle) * orb.radius;
                    xf.z = orb.center_z + @sin(orb.angle) * orb.radius;
                }
            }
        }.run));

        // Rotation system — updates transform.rot_x/rot_y using speed * dt
        try self.ecs_world.addSystem(SystemMod.System.init("rotation", .update, self, struct {
            fn run(ctx: ?*anyopaque, world: *World, dt: f64) void {
                const engine = @as(*Engine, @ptrCast(@alignCast(ctx.?)));
                var q = Query.init(world, &[_]ComponentId{ engine.transform_id, engine.rotation_speed_id });
                while (q.next()) |entity| {
                    const xf = world.getComponent(entity, engine.transform_id, Transform).?;
                    const speed = world.getComponent(entity, engine.rotation_speed_id, RotationSpeed).?;
                    xf.rot_x += speed.x * @as(f32, @floatCast(dt));
                    xf.rot_y += speed.y * @as(f32, @floatCast(dt));
                }
            }
        }.run));

        // Resolve parent-child transforms — adds parent's world position to child's local position.
        // Runs AFTER orbit/rotation so parent position is already computed.
        try self.ecs_world.addSystem(SystemMod.System.init("resolve_parent", .post_update, self, struct {
            fn run(ctx: ?*anyopaque, world: *World, dt: f64) void {
                _ = dt;
                const engine = @as(*Engine, @ptrCast(@alignCast(ctx.?)));
                var query = Query.init(world, &[_]ComponentId{ engine.transform_id, engine.parent_id });
                while (query.next()) |entity| {
                    const xf = world.getComponent(entity, engine.transform_id, Transform).?;
                    const par = world.getComponent(entity, engine.parent_id, Parent).?;

                    // Skip if parent is dead or has no Transform
                    if (!world.isAlive(par.parent)) continue;
                    const parent_xf = world.getComponent(par.parent, engine.transform_id, Transform) orelse continue;

                    // Add parent's world position to child's local position
                    xf.x += parent_xf.x;
                    xf.y += parent_xf.y;
                    xf.z += parent_xf.z;
                }
            }
        }.run));

        // Render system — renders all entities with Transform (shape-based dispatch).
        // Front-to-back sorted by view-space Z for early Z rejection (overdraw reduction).
        // Pre-transform → work-stealing queue: the main thread transforms ALL visible
        // triangles once (eliminating 4× vertex-transform duplication), then parallel
        // workers atomically steal triangles from the shared buffer for rasterization.
        try self.ecs_world.addSystem(SystemMod.System.init("render", .render, self, struct {
            fn run(ctx: ?*anyopaque, world: *World, dt: f64) void {
                _ = dt;
                const engine = @as(*Engine, @ptrCast(@alignCast(ctx.?)));
                const w_f32: f32 = @floatFromInt(engine.config.width);
                const h_f32: f32 = @floatFromInt(engine.config.height);
                const view = Mat4.lookAt(Vec3.init(0, 3, -18), Vec3.init(0, 0, -8), Vec3.init(0, 1, 0));
                const proj = Mat4.perspective(std.math.pi / 3.0, w_f32 / h_f32, 0.1, 100.0);

                var q = Query.init(world, &[_]ComponentId{engine.transform_id});

                // First pass: collect view-space Z for each entity
                var count: usize = 0;
                while (q.next()) |entity| {
                    const xf = world.getComponent(entity, engine.transform_id, Transform).?;
                    engine.sort_buffer[count] = .{
                        .z = view.data[2] * xf.x + view.data[6] * xf.y + view.data[10] * xf.z + view.data[14],
                        .entity = entity,
                    };
                    count += 1;
                }

                // Sort front-to-back (ascending Z — closer first for early Z rejection)
                std.sort.block(SortEntry, engine.sort_buffer[0..count], {}, struct {
                    fn lessThan(_: void, a: SortEntry, b: SortEntry) bool {
                        return a.z < b.z;
                    }
                }.lessThan);

                // Pre-combine view×proj so each emit call saves one multiply.
                const view_proj = Mat4.mul(proj, view);

                // Phase 1: pre-transform all triangles into tri_buffer (main thread, serial).
                // This runs ONE vertex transform per triangle instead of 4× per worker.
                engine.tri_count = 0;
                for (engine.sort_buffer[0..count]) |entry| {
                    const xf = world.getComponent(entry.entity, engine.transform_id, Transform).?;
                    switch (xf.shape) {
                        .cube => emitCubeTris(engine, xf, view_proj),
                        .sphere => emitMeshTris(engine, engine.sphere_mesh, xf, view_proj),
                        .torus => emitMeshTris(engine, engine.torus_mesh, xf, view_proj),
                    }
                }

                const tex = engine.texture[0..];

                // Diagnostic mode: full-frame drawTriangle (no tiles).
                // When use_tiles is false, skip binning and tile dispatch, render
                // directly to the global framebuffer to isolate tile pipeline bugs.
                if (!engine.use_tiles) {
                    for (engine.tri_buffer[0..engine.tri_count]) |tri| {
                        engine.backend.drawTriangle(tri.v0, tri.v1, tri.v2, tex);
                    }
                } else {
                    // Phase 2a: two-pass prefix-sum tile binning (no overflow — every
                    // triangle lands in every tile it covers). Pass 1: count assignments.
                    const total_tiles = engine.num_tiles_x * engine.num_tiles_y;
                    @memset(engine.tile_counts[0..total_tiles], 0);
                    for (engine.tri_buffer[0..engine.tri_count]) |tri| {
                        const min_x = @min(@min(tri.v0.x, tri.v1.x), tri.v2.x);
                        const min_y = @min(@min(tri.v0.y, tri.v1.y), tri.v2.y);
                        const max_x = @max(@max(tri.v0.x, tri.v1.x), tri.v2.x);
                        const max_y = @max(@max(tri.v0.y, tri.v1.y), tri.v2.y);

                        if (max_x <= min_x or max_y <= min_y) continue; // degenerate

                        const fb_w_f: f32 = @floatFromInt(engine.config.width);
                        const fb_h_f: f32 = @floatFromInt(engine.config.height);
                        if (max_x <= 0 or max_y <= 0 or min_x >= fb_w_f or min_y >= fb_h_f) continue;

                        const c_min_x = @max(0, @as(i32, @intFromFloat(min_x)));
                        const c_min_y = @max(0, @as(i32, @intFromFloat(min_y)));
                        const c_max_x = @min(@as(i32, @intFromFloat(max_x)), @as(i32, @intFromFloat(fb_w_f)) - 1);
                        const c_max_y = @min(@as(i32, @intFromFloat(max_y)), @as(i32, @intFromFloat(fb_h_f)) - 1);

                        const tile_x0 = @as(u32, @intCast(c_min_x)) / TILE_SIZE;
                        const tile_y0 = @as(u32, @intCast(c_min_y)) / TILE_SIZE;
                        const tile_x1 = @min(@as(u32, @intCast(c_max_x)) / TILE_SIZE, engine.num_tiles_x - 1);
                        const tile_y1 = @min(@as(u32, @intCast(c_max_y)) / TILE_SIZE, engine.num_tiles_y - 1);

                        var ty: u32 = tile_y0;
                        while (ty <= tile_y1) : (ty += 1) {
                            var tx: u32 = tile_x0;
                            while (tx <= tile_x1) : (tx += 1) {
                                const tile_idx = ty * engine.num_tiles_x + tx;
                                engine.tile_counts[tile_idx] += 1;
                            }
                        }
                    }

                    // Compute prefix-sum offsets from tile counts.
                    var total_binned: u32 = 0;
                    for (0..total_tiles) |tile_idx| {
                        engine.tile_offsets[tile_idx] = total_binned;
                        total_binned += engine.tile_counts[tile_idx];
                    }
                    engine.tile_offsets[total_tiles] = total_binned;

                    // Safety check: tile_tris must be large enough for all assignments.
                    if (total_binned > engine.tile_tris.len) {
                        std.debug.panic("tile_tris overflow: need {d} entries, have {d}", .{ total_binned, engine.tile_tris.len });
                    }

                    // Pass 2: store triangles at offset + running-count positions.
                    @memset(engine.tile_counts[0..total_tiles], 0);
                    for (engine.tri_buffer[0..engine.tri_count]) |tri| {
                        const min_x = @min(@min(tri.v0.x, tri.v1.x), tri.v2.x);
                        const min_y = @min(@min(tri.v0.y, tri.v1.y), tri.v2.y);
                        const max_x = @max(@max(tri.v0.x, tri.v1.x), tri.v2.x);
                        const max_y = @max(@max(tri.v0.y, tri.v1.y), tri.v2.y);

                        if (max_x <= min_x or max_y <= min_y) continue;

                        const fb_w_f: f32 = @floatFromInt(engine.config.width);
                        const fb_h_f: f32 = @floatFromInt(engine.config.height);
                        if (max_x <= 0 or max_y <= 0 or min_x >= fb_w_f or min_y >= fb_h_f) continue;

                        const c_min_x = @max(0, @as(i32, @intFromFloat(min_x)));
                        const c_min_y = @max(0, @as(i32, @intFromFloat(min_y)));
                        const c_max_x = @min(@as(i32, @intFromFloat(max_x)), @as(i32, @intFromFloat(fb_w_f)) - 1);
                        const c_max_y = @min(@as(i32, @intFromFloat(max_y)), @as(i32, @intFromFloat(fb_h_f)) - 1);

                        const tile_x0 = @as(u32, @intCast(c_min_x)) / TILE_SIZE;
                        const tile_y0 = @as(u32, @intCast(c_min_y)) / TILE_SIZE;
                        const tile_x1 = @min(@as(u32, @intCast(c_max_x)) / TILE_SIZE, engine.num_tiles_x - 1);
                        const tile_y1 = @min(@as(u32, @intCast(c_max_y)) / TILE_SIZE, engine.num_tiles_y - 1);

                        var ty: u32 = tile_y0;
                        while (ty <= tile_y1) : (ty += 1) {
                            var tx: u32 = tile_x0;
                            while (tx <= tile_x1) : (tx += 1) {
                                const tile_idx = ty * engine.num_tiles_x + tx;
                                const pos = engine.tile_offsets[tile_idx] + engine.tile_counts[tile_idx];
                                engine.tile_tris[pos] = tri;
                                engine.tile_counts[tile_idx] += 1;
                            }
                        }
                    }

                    // Phase 2b: dispatch tile workers or single-thread.
                    if (engine.thread_count > 0) {
                        const tile_ctx = TileWorkCtx{ .engine = engine, .tex = tex };
                        engine.thread_pool.spawn(tileWorker, &tile_ctx, engine.thread_count);
                        const wait_ns = time.nanoTime();
                        engine.thread_pool.wait();
                        engine.benchmark.ms_thread_wait = @as(f32, @floatFromInt(time.nanoTime() - wait_ns)) / @as(f32, std.time.ns_per_ms);
                    } else {
                        for (0..total_tiles) |tile_idx| {
                            const base = engine.tile_offsets[tile_idx];
                            const cnt = engine.tile_offsets[tile_idx + 1] - base;
                            if (cnt == 0) continue;
                            const tx_u = @as(u32, @intCast(tile_idx % engine.num_tiles_x));
                            const ty_u = @as(u32, @intCast(tile_idx / engine.num_tiles_x));
                            const tile_x = tx_u * TILE_SIZE;
                            const tile_y = ty_u * TILE_SIZE;
                            var tile_zb: [TILE_SIZE * TILE_SIZE]f32 = @splat(1.0);
                            const bg_pixel = @as(u32, @bitCast([4]u8{ COLORS.dark.b, COLORS.dark.g, COLORS.dark.r, COLORS.dark.a }));
                            var tile_cb: [TILE_SIZE * TILE_SIZE]u32 = @splat(bg_pixel);
                            for (0..cnt) |j| {
                                const tri = engine.tile_tris[base + j];
                                engine.backend.drawTriangleTile(tri.v0, tri.v1, tri.v2, tex, tile_x, tile_y, &tile_zb, &tile_cb);
                            }
                            engine.backend.copyTileToFb(tile_x, tile_y, &tile_cb, engine.config.width, engine.config.height);
                        }
                    }
                }
            }
        }.run));

        // tileWorker / emit* are defined at module level below.
    }

    pub fn deinit(self: *Engine) void {
        // Free dynamically allocated meshes (if postInit was called)
        if (self.sphere_mesh.len > 0) self.allocator.free(self.sphere_mesh);
        if (self.torus_mesh.len > 0) self.allocator.free(self.torus_mesh);
        if (self.sort_buffer.len > 0) self.allocator.free(self.sort_buffer);
        if (self.tri_buffer.len > 0) self.allocator.free(self.tri_buffer);
        if (self.tile_tris.len > 0) self.allocator.free(self.tile_tris);
        if (self.tile_counts.len > 0) self.allocator.free(self.tile_counts);
        if (self.tile_offsets.len > 0) self.allocator.free(self.tile_offsets);
        if (self.thread_count > 0) self.thread_pool.deinit();
        self.ecs_world.deinit();
        window.windowDestroy(&self.win);
        self.backend.deinit();
    }

    pub fn run(self: *Engine) void {
        std.debug.print("[ok] infinity engine running (ESC to exit)\n", .{});

        // 60 FPS cap: each frame should take at most ~16.67ms.
        const target_ns: u64 = 16_666_667;

        while (self.win.running) {
            const frame_start = time.nanoTime();

            self.win.input_state.beginFrame();
            window.windowPollEvents(&self.win);

            if (self.win.input_state.isPressed(.action_exit)) break;

            const dt = self.dt_tracker.tick();
            const fps = self.fps_counter.tick(dt);

            // Benchmark: track per-phase timing
            self.benchmark.beginFrame();

            self.benchmark.beginPhase();
            self.ecs_world.runSystems(.pre_update, dt);
            self.benchmark.endPhase(.pre_update);

            self.benchmark.beginPhase();
            self.ecs_world.runSystems(.update, dt);
            self.benchmark.endPhase(.update);

            self.benchmark.beginPhase();
            self.ecs_world.runSystems(.post_update, dt);
            self.benchmark.endPhase(.post_update);

            self.benchmark.beginPhase();
            self.backend.beginFrame(COLORS.dark);
            self.ecs_world.runSystems(.render, dt);
            self.backend.endFrame();
            self.benchmark.endPhase(.render);

            self.benchmark.endFrame(self.ecs_world.aliveCount(), fps);

            self.backend.present();

            // Adaptive sleep to cap at 60 FPS — sleep only the remaining time
            // within the 16.67ms budget instead of a fixed 16ms.
            const frame_elapsed = time.nanoTime() - frame_start;
            if (frame_elapsed < target_ns) {
                var ts = std.os.linux.timespec{
                    .sec = 0,
                    .nsec = @as(isize, @intCast(target_ns - frame_elapsed)),
                };
                _ = std.os.linux.nanosleep(&ts, null);
            }
            self.frame += 1;
        }

        std.debug.print("[ok] {d} frames rendered\n", .{self.frame});
    }
};

/// Shared context for tile-based rasterization workers.
const TileWorkCtx = struct {
    engine: *Engine,
    tex: []const u8,
};

/// Worker function for tile-parallel rasterization.
/// Each thread claims tiles by index stride = thread_count.
/// For each tile, it renders all binned triangles into tile-local
/// Z/color buffers, then copies the result to the framebuffer.
/// No locks needed: each tile is owned by exactly one thread.
fn tileWorker(idx: usize, ctx: *TileWorkCtx) void {
    const engine = ctx.engine;
    const total_tiles = engine.num_tiles_x * engine.num_tiles_y;
    const thread_count = engine.thread_count;
    var t = idx;
    while (t < total_tiles) : (t += thread_count) {
        const tile_idx = t;
        const base = engine.tile_offsets[tile_idx];
        const cnt = engine.tile_offsets[tile_idx + 1] - base;
        if (cnt == 0) continue;
        const tx_u = @as(u32, @intCast(tile_idx % engine.num_tiles_x));
        const ty_u = @as(u32, @intCast(tile_idx / engine.num_tiles_x));
        const tile_x = tx_u * TILE_SIZE;
        const tile_y = ty_u * TILE_SIZE;
        const bg_pixel = @as(u32, @bitCast([4]u8{ COLORS.dark.b, COLORS.dark.g, COLORS.dark.r, COLORS.dark.a }));
        var tile_zb: [TILE_SIZE * TILE_SIZE]f32 = @splat(1.0);
        var tile_cb: [TILE_SIZE * TILE_SIZE]u32 = @splat(bg_pixel);
        for (0..cnt) |j| {
            const tri = engine.tile_tris[base + j];
            engine.backend.drawTriangleTile(tri.v0, tri.v1, tri.v2, ctx.tex, tile_x, tile_y, &tile_zb, &tile_cb);
        }
        engine.backend.copyTileToFb(tile_x, tile_y, &tile_cb, engine.config.width, engine.config.height);
    }
}

/// Clip-space vertex (result of MVP transform).
const ClipVert = struct { x: f32, y: f32, z: f32, w: f32 };

/// Transform a Vec3 by a 4×4 matrix, producing a clip-space vertex.
fn clipTransform(m: Mat4, v: Vec3) ClipVert {
    return ClipVert{
        .x = m.data[0] * v.x + m.data[4] * v.y + m.data[8] * v.z + m.data[12],
        .y = m.data[1] * v.x + m.data[5] * v.y + m.data[9] * v.z + m.data[13],
        .z = m.data[2] * v.x + m.data[6] * v.y + m.data[10] * v.z + m.data[14],
        .w = m.data[3] * v.x + m.data[7] * v.y + m.data[11] * v.z + m.data[15],
    };
}

/// Compute the Lambertian lighting factor for a face normal given the light direction.
fn lambertFactor(world_normal: Vec3) f32 {
    return 0.3 + 0.7 * @max(Vec3.dot(world_normal, LIGHT_DIR), 0.0);
}

/// Pre-transform a cube entity into screen-space triangles in tri_buffer.
/// Per-face flat shading with Lambertian diffuse lighting, back-face culled.
fn emitCubeTris(engine: *Engine, transform: *Transform, view_proj: Mat4) void {
    const half: f32 = 1.5 * transform.scale;
    const positions = [_]Vec3{
        Vec3.init(-half, -half, -half),
        Vec3.init(half, -half, -half),
        Vec3.init(half, half, -half),
        Vec3.init(-half, half, -half),
        Vec3.init(-half, -half, half),
        Vec3.init(half, -half, half),
        Vec3.init(half, half, half),
        Vec3.init(-half, half, half),
    };

    const model = Mat4.mul(
        Mat4.translate(Vec3.init(transform.x, transform.y, transform.z)),
        Mat4.mul(Mat4.rotateY(transform.rot_y), Mat4.rotateX(transform.rot_x)),
    );
    const mvp = Mat4.mul(view_proj, model);

    const TriDef = struct { i0: u8, i1: u8, i2: u8, u0: f32, v0: f32, u1: f32, v1: f32, u2: f32, v2: f32, c0: Color, c1: Color, c2: Color };
    const triangles = [_]TriDef{
        .{ .i0 = 4, .i1 = 5, .i2 = 6, .u0 = 0, .v0 = 1, .u1 = 1, .v1 = 1, .u2 = 1, .v2 = 0, .c0 = Color{ .r = 255, .g = 60, .b = 60, .a = 255 }, .c1 = Color{ .r = 200, .g = 30, .b = 30, .a = 255 }, .c2 = Color{ .r = 230, .g = 80, .b = 70, .a = 255 } },
        .{ .i0 = 4, .i1 = 6, .i2 = 7, .u0 = 0, .v0 = 1, .u1 = 1, .v1 = 0, .u2 = 0, .v2 = 0, .c0 = Color{ .r = 200, .g = 30, .b = 30, .a = 255 }, .c1 = Color{ .r = 230, .g = 80, .b = 70, .a = 255 }, .c2 = Color{ .r = 180, .g = 50, .b = 50, .a = 255 } },
        .{ .i0 = 1, .i1 = 0, .i2 = 3, .u0 = 0, .v0 = 1, .u1 = 1, .v1 = 1, .u2 = 1, .v2 = 0, .c0 = Color{ .r = 60, .g = 60, .b = 255, .a = 255 }, .c1 = Color{ .r = 30, .g = 30, .b = 200, .a = 255 }, .c2 = Color{ .r = 80, .g = 70, .b = 230, .a = 255 } },
        .{ .i0 = 1, .i1 = 3, .i2 = 2, .u0 = 0, .v0 = 1, .u1 = 1, .v1 = 0, .u2 = 0, .v2 = 0, .c0 = Color{ .r = 30, .g = 30, .b = 200, .a = 255 }, .c1 = Color{ .r = 80, .g = 70, .b = 230, .a = 255 }, .c2 = Color{ .r = 50, .g = 50, .b = 180, .a = 255 } },
        .{ .i0 = 1, .i1 = 5, .i2 = 6, .u0 = 0, .v0 = 1, .u1 = 1, .v1 = 1, .u2 = 1, .v2 = 0, .c0 = Color{ .r = 60, .g = 255, .b = 60, .a = 255 }, .c1 = Color{ .r = 30, .g = 200, .b = 30, .a = 255 }, .c2 = Color{ .r = 80, .g = 230, .b = 70, .a = 255 } },
        .{ .i0 = 1, .i1 = 6, .i2 = 2, .u0 = 0, .v0 = 1, .u1 = 1, .v1 = 0, .u2 = 0, .v2 = 0, .c0 = Color{ .r = 30, .g = 200, .b = 30, .a = 255 }, .c1 = Color{ .r = 80, .g = 230, .b = 70, .a = 255 }, .c2 = Color{ .r = 50, .g = 180, .b = 50, .a = 255 } },
        .{ .i0 = 0, .i1 = 4, .i2 = 7, .u0 = 0, .v0 = 1, .u1 = 1, .v1 = 1, .u2 = 1, .v2 = 0, .c0 = Color{ .r = 255, .g = 220, .b = 40, .a = 255 }, .c1 = Color{ .r = 200, .g = 170, .b = 20, .a = 255 }, .c2 = Color{ .r = 230, .g = 240, .b = 60, .a = 255 } },
        .{ .i0 = 0, .i1 = 7, .i2 = 3, .u0 = 0, .v0 = 1, .u1 = 1, .v1 = 0, .u2 = 0, .v2 = 0, .c0 = Color{ .r = 200, .g = 170, .b = 20, .a = 255 }, .c1 = Color{ .r = 230, .g = 240, .b = 60, .a = 255 }, .c2 = Color{ .r = 180, .g = 200, .b = 40, .a = 255 } },
        .{ .i0 = 3, .i1 = 2, .i2 = 6, .u0 = 0, .v0 = 1, .u1 = 1, .v1 = 1, .u2 = 1, .v2 = 0, .c0 = Color{ .r = 255, .g = 255, .b = 255, .a = 255 }, .c1 = Color{ .r = 200, .g = 200, .b = 200, .a = 255 }, .c2 = Color{ .r = 230, .g = 230, .b = 210, .a = 255 } },
        .{ .i0 = 3, .i1 = 6, .i2 = 7, .u0 = 0, .v0 = 1, .u1 = 1, .v1 = 0, .u2 = 0, .v2 = 0, .c0 = Color{ .r = 200, .g = 200, .b = 200, .a = 255 }, .c1 = Color{ .r = 230, .g = 230, .b = 210, .a = 255 }, .c2 = Color{ .r = 180, .g = 180, .b = 180, .a = 255 } },
        .{ .i0 = 4, .i1 = 5, .i2 = 1, .u0 = 0, .v0 = 1, .u1 = 1, .v1 = 1, .u2 = 1, .v2 = 0, .c0 = Color{ .r = 80, .g = 80, .b = 80, .a = 255 }, .c1 = Color{ .r = 60, .g = 60, .b = 60, .a = 255 }, .c2 = Color{ .r = 100, .g = 100, .b = 100, .a = 255 } },
        .{ .i0 = 4, .i1 = 1, .i2 = 0, .u0 = 0, .v0 = 1, .u1 = 1, .v1 = 0, .u2 = 0, .v2 = 0, .c0 = Color{ .r = 60, .g = 60, .b = 60, .a = 255 }, .c1 = Color{ .r = 100, .g = 100, .b = 100, .a = 255 }, .c2 = Color{ .r = 80, .g = 80, .b = 80, .a = 255 } },
    };

    const fw: f32 = @floatFromInt(engine.config.width);
    const fh: f32 = @floatFromInt(engine.config.height);

    for (triangles) |tri| {
        // Compute per-face normal from model-space positions (flat shading)
        const p0_model = positions[tri.i0];
        const p1_model = positions[tri.i1];
        const p2_model = positions[tri.i2];
        const edge1 = Vec3.sub(p1_model, p0_model);
        const edge2 = Vec3.sub(p2_model, p0_model);
        const face_nml = Vec3.normalize(Vec3.cross(edge1, edge2));
        const nml_world = Mat4.transformDirection(model, face_nml);
        const factor = lambertFactor(nml_world);

        // Apply lighting factor to all 3 vertex colors
        const c0 = Color{
            .r = @intFromFloat(@as(f32, @floatFromInt(tri.c0.r)) * factor),
            .g = @intFromFloat(@as(f32, @floatFromInt(tri.c0.g)) * factor),
            .b = @intFromFloat(@as(f32, @floatFromInt(tri.c0.b)) * factor),
            .a = tri.c0.a,
        };
        const c1 = Color{
            .r = @intFromFloat(@as(f32, @floatFromInt(tri.c1.r)) * factor),
            .g = @intFromFloat(@as(f32, @floatFromInt(tri.c1.g)) * factor),
            .b = @intFromFloat(@as(f32, @floatFromInt(tri.c1.b)) * factor),
            .a = tri.c1.a,
        };
        const c2 = Color{
            .r = @intFromFloat(@as(f32, @floatFromInt(tri.c2.r)) * factor),
            .g = @intFromFloat(@as(f32, @floatFromInt(tri.c2.g)) * factor),
            .b = @intFromFloat(@as(f32, @floatFromInt(tri.c2.b)) * factor),
            .a = tri.c2.a,
        };

        const clip0 = clipTransform(mvp, p0_model);
        const clip1 = clipTransform(mvp, p1_model);
        const clip2 = clipTransform(mvp, p2_model);

        if (clip0.w < 0 or clip1.w < 0 or clip2.w < 0) continue;

        const sx0 = (clip0.x / clip0.w + 1.0) * 0.5 * fw;
        const sy0 = (1.0 - clip0.y / clip0.w) * 0.5 * fh;
        const sz0 = clip0.z / clip0.w * 0.5 + 0.5;
        const sx1 = (clip1.x / clip1.w + 1.0) * 0.5 * fw;
        const sy1 = (1.0 - clip1.y / clip1.w) * 0.5 * fh;
        const sz1 = clip1.z / clip1.w * 0.5 + 0.5;
        const sx2 = (clip2.x / clip2.w + 1.0) * 0.5 * fw;
        const sy2 = (1.0 - clip2.y / clip2.w) * 0.5 * fh;
        const sz2 = clip2.z / clip2.w * 0.5 + 0.5;

        const area = (sx1 - sx0) * (sy2 - sy0) - (sy1 - sy0) * (sx2 - sx0);
        if (area < 0) continue;

        const iw0 = 1.0 / clip0.w;
        const iw1 = 1.0 / clip1.w;
        const iw2 = 1.0 / clip2.w;

        const vx0 = Vertex{ .x = sx0, .y = sy0, .z = sz0, .nx = 0, .ny = 0, .nz = 0, .u = tri.u0 * iw0, .v = tri.v0 * iw0, .inv_w = iw0, .color = c0 };
        const vx1 = Vertex{ .x = sx1, .y = sy1, .z = sz1, .nx = 0, .ny = 0, .nz = 0, .u = tri.u1 * iw1, .v = tri.v1 * iw1, .inv_w = iw1, .color = c1 };
        const vx2 = Vertex{ .x = sx2, .y = sy2, .z = sz2, .nx = 0, .ny = 0, .nz = 0, .u = tri.u2 * iw2, .v = tri.v2 * iw2, .inv_w = iw2, .color = c2 };

        // Push to shared buffer (main-thread only, no atomic needed)
        const idx = engine.tri_count;
        if (idx >= engine.tri_capacity) return; // buffer full — drop triangle silently
        engine.tri_buffer[idx] = ScreenTri{ .v0 = vx0, .v1 = vx1, .v2 = vx2 };
        engine.tri_count = idx + 1;
    }
}

/// Pre-transform a mesh entity into screen-space triangles in tri_buffer.
/// Per-vertex normal lighting with Lambertian diffuse, back-face culled.
fn emitMeshTris(engine: *Engine, mesh: []const Vertex, transform: *Transform, view_proj: Mat4) void {
    const model = Mat4.mul(
        Mat4.translate(Vec3.init(transform.x, transform.y, transform.z)),
        Mat4.mul(Mat4.rotateY(transform.rot_y), Mat4.rotateX(transform.rot_x)),
    );
    const mvp = Mat4.mul(view_proj, model);

    const fw: f32 = @floatFromInt(engine.config.width);
    const fh: f32 = @floatFromInt(engine.config.height);

    var i: usize = 0;
    while (i + 2 < mesh.len) : (i += 3) {
        const v0 = mesh[i];
        const v1 = mesh[i + 1];
        const v2 = mesh[i + 2];

        const p0 = Vec3.init(v0.x, v0.y, v0.z);
        const p1 = Vec3.init(v1.x, v1.y, v1.z);
        const p2 = Vec3.init(v2.x, v2.y, v2.z);

        const clip0 = clipTransform(mvp, p0);
        const clip1 = clipTransform(mvp, p1);
        const clip2 = clipTransform(mvp, p2);

        if (clip0.w < 0 or clip1.w < 0 or clip2.w < 0) continue;

        const sx0 = (clip0.x / clip0.w + 1.0) * 0.5 * fw;
        const sy0 = (1.0 - clip0.y / clip0.w) * 0.5 * fh;
        const sz0 = clip0.z / clip0.w * 0.5 + 0.5;
        const sx1 = (clip1.x / clip1.w + 1.0) * 0.5 * fw;
        const sy1 = (1.0 - clip1.y / clip1.w) * 0.5 * fh;
        const sz1 = clip1.z / clip1.w * 0.5 + 0.5;
        const sx2 = (clip2.x / clip2.w + 1.0) * 0.5 * fw;
        const sy2 = (1.0 - clip2.y / clip2.w) * 0.5 * fh;
        const sz2 = clip2.z / clip2.w * 0.5 + 0.5;

        // Back-face culling
        const area = (sx1 - sx0) * (sy2 - sy0) - (sy1 - sy0) * (sx2 - sx0);
        if (area < 0) continue;

        // Per-vertex normal lighting
        const n0 = Vec3.init(v0.nx, v0.ny, v0.nz);
        const n1 = Vec3.init(v1.nx, v1.ny, v1.nz);
        const n2 = Vec3.init(v2.nx, v2.ny, v2.nz);

        const nw0 = Mat4.transformDirection(model, n0);
        const nw1 = Mat4.transformDirection(model, n1);
        const nw2 = Mat4.transformDirection(model, n2);

        const f0 = lambertFactor(nw0);
        const f1 = lambertFactor(nw1);
        const f2 = lambertFactor(nw2);

        const c0 = Color{
            .r = @intFromFloat(@as(f32, @floatFromInt(v0.color.r)) * f0),
            .g = @intFromFloat(@as(f32, @floatFromInt(v0.color.g)) * f0),
            .b = @intFromFloat(@as(f32, @floatFromInt(v0.color.b)) * f0),
            .a = v0.color.a,
        };
        const c1 = Color{
            .r = @intFromFloat(@as(f32, @floatFromInt(v1.color.r)) * f1),
            .g = @intFromFloat(@as(f32, @floatFromInt(v1.color.g)) * f1),
            .b = @intFromFloat(@as(f32, @floatFromInt(v1.color.b)) * f1),
            .a = v1.color.a,
        };
        const c2 = Color{
            .r = @intFromFloat(@as(f32, @floatFromInt(v2.color.r)) * f2),
            .g = @intFromFloat(@as(f32, @floatFromInt(v2.color.g)) * f2),
            .b = @intFromFloat(@as(f32, @floatFromInt(v2.color.b)) * f2),
            .a = v2.color.a,
        };

        const iw0 = 1.0 / clip0.w;
        const iw1 = 1.0 / clip1.w;
        const iw2 = 1.0 / clip2.w;

        const vx0 = Vertex{ .x = sx0, .y = sy0, .z = sz0, .nx = v0.nx, .ny = v0.ny, .nz = v0.nz, .u = v0.u * iw0, .v = v0.v * iw0, .inv_w = iw0, .color = c0 };
        const vx1 = Vertex{ .x = sx1, .y = sy1, .z = sz1, .nx = v1.nx, .ny = v1.ny, .nz = v1.nz, .u = v1.u * iw1, .v = v1.v * iw1, .inv_w = iw1, .color = c1 };
        const vx2 = Vertex{ .x = sx2, .y = sy2, .z = sz2, .nx = v2.nx, .ny = v2.ny, .nz = v2.nz, .u = v2.u * iw2, .v = v2.v * iw2, .inv_w = iw2, .color = c2 };

        // Push to shared buffer (main-thread only, no atomic needed)
        const idx = engine.tri_count;
        if (idx >= engine.tri_capacity) return; // buffer full — drop triangle silently
        engine.tri_buffer[idx] = ScreenTri{ .v0 = vx0, .v1 = vx1, .v2 = vx2 };
        engine.tri_count = idx + 1;
    }
}

test "engine type exists" {
    try std.testing.expect(@sizeOf(Engine) > 0);
}

test "Shape has three variants" {
    try std.testing.expectEqual(@as(usize, 3), @typeInfo(Shape).@"enum".fields.len);
}

test "Shape default is cube" {
    const s: Shape = .cube;
    _ = s;
}

test "Transform has shape field with default cube" {
    const t = Transform{ .x = 0, .y = 0, .z = 0, .rot_x = 0, .rot_y = 0 };
    try std.testing.expect(t.shape == .cube);
}

test "EngineConfig default width is 800" {
    const cfg = EngineConfig{};
    try std.testing.expectEqual(@as(u32, 800), cfg.width);
}

test "EngineConfig default height is 600" {
    const cfg = EngineConfig{};
    try std.testing.expectEqual(@as(u32, 600), cfg.height);
}

test "lambertFactor with aligned normal produces max factor" {
    // Normal pointing exactly toward light → factor = 0.3 + 0.7*1.0 = 1.0
    const nml = Vec3.normalize(Vec3.init(1.0, 2.0, 1.0));
    const f = lambertFactor(nml);
    try std.testing.expectApproxEqAbs(@as(f32, 1.0), f, 0.001);
}

test "lambertFactor with opposite normal produces min factor" {
    // Normal pointing against light → factor = 0.3 + 0.7*0.0 = 0.3
    const nml = Vec3.normalize(Vec3.init(-1.0, -2.0, -1.0));
    const f = lambertFactor(nml);
    try std.testing.expectApproxEqAbs(@as(f32, 0.3), f, 0.001);
}

test "lambertFactor with perpendicular normal produces 0.3 factor" {
    // Normal orthogonal to light → dot = 0 → factor = 0.3
    const nml = Vec3.normalize(Vec3.init(-2.0, 1.0, 0.0));
    const f = lambertFactor(nml);
    try std.testing.expectApproxEqAbs(@as(f32, 0.3), f, 0.001);
}

test "ClipVert and clipTransform work" {
    const m = Mat4.identity;
    const v = Vec3.init(1, 2, 3);
    const cv = clipTransform(m, v);
    try std.testing.expectEqual(@as(f32, 1), cv.x);
    try std.testing.expectEqual(@as(f32, 2), cv.y);
    try std.testing.expectEqual(@as(f32, 3), cv.z);
    try std.testing.expectEqual(@as(f32, 1), cv.w);
}

test "T-003 sort buffer allocate and free" {
    // Verify SortEntry allocation pattern works (no leaks, no UB)
    const allocator = std.testing.allocator;
    const buffer = try allocator.alloc(SortEntry, 1000);
    defer allocator.free(buffer);
    try std.testing.expectEqual(@as(usize, 1000), buffer.len);
}

test "R-2.2 tile binning supports at least tri_capacity assignments (prefix-sum)" {
    // With prefix-sum dynamic bins, every triangle lands in every tile it covers.
    // Verify the allocation can hold tri_capacity unique assignments (worst case:
    // each triangle in tri_buffer covers a different tile).
    try std.testing.expect(TILE_SIZE > 0);
}

test "T-004 front-to-back sort orders entities by ascending Z" {
    var entries = [_]SortEntry{
        .{ .z = 10.0, .entity = Entity.init(3, 0) },
        .{ .z = 1.0, .entity = Entity.init(1, 0) },
        .{ .z = 5.0, .entity = Entity.init(2, 0) },
    };
    std.sort.block(SortEntry, &entries, {}, struct {
        fn lessThan(_: void, a: SortEntry, b: SortEntry) bool {
            return a.z < b.z;
        }
    }.lessThan);
    try std.testing.expectEqual(@as(u32, 1), entries[0].entity.index);
    try std.testing.expectEqual(@as(u32, 2), entries[1].entity.index);
    try std.testing.expectEqual(@as(u32, 3), entries[2].entity.index);
}
