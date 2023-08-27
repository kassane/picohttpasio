const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const libasio_dep = b.dependency("asio", .{
        .target = target,
        .optimize = optimize,
    });
    const libasio = libasio_dep.artifact("asio");
    const exe = b.addExecutable(.{
        .name = "picoAsio",
        .target = target,
        .optimize = optimize,
    });
    exe.addIncludePath(.{ .path = "include" });
    for (libasio.include_dirs.items) |include| {
        exe.include_dirs.append(include) catch {};
    }
    exe.addCSourceFiles(&.{
        "src/main.cpp",
        "src/picohttpparser.c",
    }, cflags);
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

const cflags = &.{
    "-Wall",
    "-Wextra",
    // "-Wshadow",
};
