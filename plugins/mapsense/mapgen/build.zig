const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const libd2 = b.dependency("libd2", .{
        .target = target,
        .optimize = optimize,
    });

    const exe = b.addExecutable(.{
        .name = "RuffnecKkMapSenseMapgen",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/main.zig"),
            .target = target,
            .optimize = optimize,
        }),
    });
    exe.root_module.addImport("d2-drlg", libd2.module("d2-drlg"));
    exe.root_module.addImport("d2-render", libd2.module("d2-render"));
    b.installArtifact(exe);
}
