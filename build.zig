const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // -----------------------------------------------------------------------
    // Executable
    // -----------------------------------------------------------------------

    const exe = b.addExecutable(.{
        .name = "infinity-engine",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/main.zig"),
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });
    exe.root_module.addLibraryPath(.{ .cwd_relative = ".zig-cache/lib" });
    exe.root_module.linkSystemLibrary("X11", .{});
    exe.root_module.linkSystemLibrary("dl", .{});
    b.installArtifact(exe);

    // Run step
    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| run_cmd.addArgs(args);
    const run_step = b.step("run", "Run Infinity Engine");
    run_step.dependOn(&run_cmd.step);

    // -----------------------------------------------------------------------
    // Benchmarks
    // -----------------------------------------------------------------------

    const bench = b.addExecutable(.{
        .name = "infinity-engine-bench",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/benchs_main.zig"),
            .target = target,
            .optimize = .ReleaseSafe,
            .link_libc = true,
        }),
    });
    bench.root_module.addLibraryPath(.{ .cwd_relative = ".zig-cache/lib" });
    bench.root_module.linkSystemLibrary("X11", .{});
    bench.root_module.linkSystemLibrary("dl", .{});

    const bench_run = b.addRunArtifact(bench);
    const bench_step = b.step("bench", "Run performance benchmarks (ReleaseSafe)");
    bench_step.dependOn(&bench_run.step);

    // -----------------------------------------------------------------------
    // Tests
    // -----------------------------------------------------------------------

    const test_runner = b.addTest(.{
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/test_runner.zig"),
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });
    test_runner.root_module.addLibraryPath(.{ .cwd_relative = ".zig-cache/lib" });
    test_runner.root_module.linkSystemLibrary("X11", .{});
    test_runner.root_module.linkSystemLibrary("dl", .{});

    const run_tests = b.addRunArtifact(test_runner);
    const test_step = b.step("test", "Run all engine tests");
    test_step.dependOn(&run_tests.step);
}
