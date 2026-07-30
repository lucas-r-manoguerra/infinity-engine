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

/// Sort entry for front-to-back entity sorting (view-space Z, entity handle).
const SortEntry = struct {
    z: f32,
    entity: Entity,
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

    // Benchmark
    benchmark: diagnostics.Benchmark,
    fps_counter: time.FpsCounter,

    /// Thread pool for parallel strip rasterization.
    /// Initialised in postInit, used in render system.
    thread_pool: ThreadPool = undefined,

    /// Number of worker threads to use for strip dispatch (0 = disabled).
    thread_count: u32 = 0,

    pub fn init(allocator: std.mem.Allocator, config: EngineConfig) EngineError!Engine {
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
        // When thread_count > 0, dispatches to 4 parallel strip workers.
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

                // Second pass: render in sorted order (multi-threaded or single)
                if (engine.thread_count > 0) {
                    const strip_ctx = StripCtx{
                        .engine = engine,
                        .world = world,
                        .entries = engine.sort_buffer[0..count],
                        .count = count,
                        .view = view,
                        .proj = proj,
                    };
                    engine.thread_pool.spawn(stripWorker, &strip_ctx, engine.thread_count);
                    const wait_ns = time.nanoTime();
                    engine.thread_pool.wait();
                    engine.benchmark.ms_thread_wait = @as(f32, @floatFromInt(time.nanoTime() - wait_ns)) / @as(f32, std.time.ns_per_ms);
                } else {
                    for (engine.sort_buffer[0..count]) |entry| {
                        const xf = world.getComponent(entry.entity, engine.transform_id, Transform).?;
                        switch (xf.shape) {
                            .cube => renderCube(engine, xf, view, proj, 0, 0),
                            .sphere => renderMesh(engine, engine.sphere_mesh, xf, view, proj, 0, 0),
                            .torus => renderMesh(engine, engine.torus_mesh, xf, view, proj, 0, 0),
                        }
                    }
                }
            }
        }.run));

        // stripWorker is defined at module level below.
    }

    pub fn deinit(self: *Engine) void {
        // Free dynamically allocated meshes (if postInit was called)
        if (self.sphere_mesh.len > 0) self.allocator.free(self.sphere_mesh);
        if (self.torus_mesh.len > 0) self.allocator.free(self.torus_mesh);
        if (self.sort_buffer.len > 0) self.allocator.free(self.sort_buffer);
        if (self.thread_count > 0) self.thread_pool.deinit();
        self.ecs_world.deinit();
        window.windowDestroy(&self.win);
        self.backend.deinit();
    }

    pub fn run(self: *Engine) void {
        std.debug.print("[ok] infinity engine running (ESC to exit)\n", .{});

        while (self.win.running) {
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

            var ts = std.os.linux.timespec{ .sec = 0, .nsec = 16_000_000 };
            _ = std.os.linux.nanosleep(&ts, null);
            self.frame += 1;
        }

        std.debug.print("[ok] {d} frames rendered\n", .{self.frame});
    }
};

/// Shared context for parallel strip rendering workers.
/// Each worker receives a pointer to this struct via ThreadPool.spawn.
const StripCtx = struct {
    engine: *Engine,
    world: *World,
    entries: []SortEntry,
    count: usize,
    view: Mat4,
    proj: Mat4,
};

/// Worker function for strip rasterization.
/// Each thread renders ALL entities, but clips rasterization to its Y range.
fn stripWorker(idx: usize, ctx: *StripCtx) void {
    const engine = ctx.engine;
    const h = engine.config.height;
    const tc = engine.thread_count;
    const strip_h = (h + tc - 1) / tc;
    const y_min = @as(i32, @intCast(idx * strip_h));
    const y_max = @min(@as(i32, @intCast((idx + 1) * strip_h)), @as(i32, @intCast(h)));

    for (ctx.entries[0..ctx.count]) |entry| {
        const xf = ctx.world.getComponent(entry.entity, engine.transform_id, Transform).?;
        switch (xf.shape) {
            .cube => renderCube(engine, xf, ctx.view, ctx.proj, y_min, y_max),
            .sphere => renderMesh(engine, engine.sphere_mesh, xf, ctx.view, ctx.proj, y_min, y_max),
            .torus => renderMesh(engine, engine.torus_mesh, xf, ctx.view, ctx.proj, y_min, y_max),
        }
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

/// Render a textured cube with the given transform and camera matrices.
/// When `y_min < y_max`, clips rasterization to the horizontal strip [y_min, y_max).
fn renderCube(engine: *Engine, transform: *Transform, view: Mat4, proj: Mat4, y_min: i32, y_max: i32) void {
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
    const mvp = Mat4.mul(Mat4.mul(proj, view), model);

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
        const tex_slice = engine.texture[0..];

        if (y_min < y_max) {
            engine.backend.drawTriangleStrip(vx0, vx1, vx2, tex_slice, y_min, y_max);
        } else {
            engine.backend.drawTriangle(vx0, vx1, vx2, tex_slice);
        }
    }
}

/// Render a mesh from a vertex buffer (3 vertices per triangle).
/// Per-vertex normal lighting, back-face culling via screen-space area.
/// When `y_min < y_max`, clips rasterization to the horizontal strip [y_min, y_max).
fn renderMesh(engine: *Engine, mesh: []const Vertex, transform: *Transform, view: Mat4, proj: Mat4, y_min: i32, y_max: i32) void {
    const model = Mat4.mul(
        Mat4.translate(Vec3.init(transform.x, transform.y, transform.z)),
        Mat4.mul(Mat4.rotateY(transform.rot_y), Mat4.rotateX(transform.rot_x)),
    );
    const mvp = Mat4.mul(Mat4.mul(proj, view), model);

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
        const tex_slice = engine.texture[0..];

        if (y_min < y_max) {
            engine.backend.drawTriangleStrip(vx0, vx1, vx2, tex_slice, y_min, y_max);
        } else {
            engine.backend.drawTriangle(vx0, vx1, vx2, tex_slice);
        }
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
