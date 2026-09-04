const std = @import("std");

pub fn build(b: *std.Build) void {
    // This executable is a public runtime companion, not a host-optimized
    // developer tool. Keep the target immutable so a release built on a newer
    // workstation cannot silently inherit AVX, AVX2, or AVX-512 instructions.
    const target = b.resolveTargetQuery(.{
        .cpu_arch = .x86_64,
        .cpu_model = .baseline,
        .os_tag = .windows,
        .abi = .gnu,
    });
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
            // Strip host/cache path metadata so fixed-seed release builds are
            // byte-identical even when their Zig caches are independent.
            .strip = true,
        }),
    });
    // A random build ID changes the PE timestamp and `.buildid` section even
    // when every source input is identical. Release hashes must be byte-exact
    // across clean builds, so omit that non-functional identifier.
    exe.build_id = .none;
    exe.root_module.addImport("d2-drlg", libd2.module("d2-drlg"));
    exe.root_module.addImport("d2-render", libd2.module("d2-render"));
    b.installArtifact(exe);
}
