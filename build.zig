const std = @import("std");

fn addEngineSources(c: *std.Build.Step.Compile) void {
    c.linkLibC(); // we need a libc

    c.addCSourceFiles(.{
        .files = &[_][]const u8{
            "src/tinycthread.c",
            "src/lock.c",
            "src/utils.c",
            "src/value.c",
            "src/component.c",
            "src/computer.c",
            "src/universe.c",
            // components
            "src/components/eeprom.c",
            "src/components/filesystem.c",
        },
    });
}

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});

    const optimize = b.standardOptimizeOption(.{});

    const engineStatic = b.addStaticLibrary(.{
        .name = "neonucleus",
        //.root_source_file = b.path("src/engine.zig"),
        .target = target,
        .optimize = optimize,
    });

    addEngineSources(engineStatic);

    const install = b.getInstallStep();

    b.installArtifact(engineStatic);
    
    const engineShared = b.addSharedLibrary(.{
        .name = "neonucleus",
        .target = target,
        .optimize = optimize,
    });
    
    addEngineSources(engineShared);

    b.installArtifact(engineShared);

    const engineStep = b.step("engine", "Builds the engine as a static library");
    engineStep.dependOn(&engineStatic.step);
    engineStep.dependOn(install);

    const sharedStep = b.step("shared", "Builds the engine as a shared library");
    sharedStep.dependOn(&engineShared.step);
    sharedStep.dependOn(install);

    const emulator = b.addExecutable(.{
        .name = "neonucleus",
        .target = target,
        .optimize = optimize,
    });
    emulator.linkLibC();
    emulator.linkSystemLibrary("lua");
    emulator.addCSourceFiles(.{
        .files = &.{
            "src/testLuaArch.c",
            "src/emulator.c",
        },
    });

    // forces us to link in everything too
    emulator.linkLibrary(engineStatic);

    b.installArtifact(emulator);
    b.step("emulator", "Builds the emulator").dependOn(&emulator.step);

    const run_cmd = b.addRunArtifact(emulator);

    run_cmd.step.dependOn(install);

    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    const run_step = b.step("run", "Run the emulator");
    run_step.dependOn(&run_cmd.step);

    const lib_unit_tests = b.addTest(.{
        .root_source_file = b.path("src/engine.zig"),
        .target = target,
        .optimize = optimize,
    });

    const run_lib_unit_tests = b.addRunArtifact(lib_unit_tests);

    const exe_unit_tests = b.addTest(.{
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
    });

    const run_exe_unit_tests = b.addRunArtifact(exe_unit_tests);

    const test_step = b.step("test", "Run unit tests");
    test_step.dependOn(&run_lib_unit_tests.step);
    test_step.dependOn(&run_exe_unit_tests.step);
}
