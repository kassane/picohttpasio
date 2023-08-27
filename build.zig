const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const libasio_dep = b.dependency("asio", .{
        .target = target,
        .optimize = optimize,
    });
    const libasio = libasio_dep.artifact("asio");
    const libpicohttpparser = picolib(b, .{ optimize, target });

    const exe = b.addExecutable(.{
        .name = "picohttp-asio",
        .target = target,
        .optimize = optimize,
    });
    exe.addIncludePath(.{ .path = "include" });
    for (libasio.include_dirs.items) |include| {
        exe.include_dirs.append(include) catch {};
    }
    exe.addCSourceFile(.{
        .file = .{ .path = "src/main.cpp" },
        .flags = cflags,
    });
    exe.disable_sanitize_c = true;
    exe.linkLibrary(libpicohttpparser);
    exe.linkLibrary(libasio);
    exe.linkLibCpp();

    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);

    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }
    const run_step = b.step("run", "Run the app");
    run_step.dependOn(&run_cmd.step);
}

fn picolib(b: *std.Build, property: struct { std.builtin.OptimizeMode, std.zig.CrossTarget }) *std.Build.Step.Compile {
    const lib = b.addStaticLibrary(.{
        .name = "picohttpparser",
        .target = property[1],
        .optimize = property[0],
    });
    lib.addIncludePath(.{ .path = "include" });
    lib.addCSourceFile(.{
        .file = .{ .path = "src/picohttpparser.c" },
        .flags = cflags,
    });
    lib.disable_sanitize_c = true;
    lib.linkLibC();

    return lib;
}

const cflags = &.{
    "-Wall",
    "-Wextra",
    // "-Wshadow",
    // "-Wpedantic",
};
