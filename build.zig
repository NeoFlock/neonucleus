const std = @import("std");
const builtin = @import("builtin");

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
            "src/unicode.c",
            // components
            "src/components/eeprom.c",
            "src/components/filesystem.c",
            "src/components/screen.c",
            "src/components/gpu.c",
            "src/components/keyboard.c",
        },
    });
}

const LuaVersion = enum {
    lua52,
    lua53,
    lua54,
};

// For the test architecture, we specify the target Lua version we so desire.
// This can be checked for with Lua's _VERSION

fn compileTheRightLua(b: *std.Build, c: *std.Build.Step.Compile, version: LuaVersion) !void {
    const alloc = b.allocator;
    const dirName = @tagName(version);

    const rootPath = try std.mem.join(alloc, std.fs.path.sep_str, &.{"foreign", dirName});

    c.addIncludePath(b.path(rootPath));

    // get all the .c files
    var files = std.ArrayList([]const u8).init(alloc);
    errdefer files.deinit();

    var dir = try std.fs.cwd().openDir(rootPath, std.fs.Dir.OpenDirOptions {.iterate = true});
    defer dir.close();

    var iter = dir.iterate();

    while(try iter.next()) |e| {
        if(std.mem.startsWith(u8, e.name, "l") and std.mem.endsWith(u8, e.name, ".c") and !std.mem.eql(u8, e.name, "lua.c")) {
            const name = try alloc.dupe(u8, e.name);
            try files.append(name);
        }
    }

    c.addCSourceFiles(.{
        .root = b.path(rootPath),
        .files = files.items,
    });
}

pub fn build(b: *std.Build) void {
    const os = builtin.target.os.tag;

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

    if (os == .windows) {
        // use the msvc win64 dll versions and copy them to raylib/ and lua/
        // get raylib from https://github.com/raysan5/raylib/releases
        emulator.addIncludePath(b.path("raylib/include"));
        emulator.addObjectFile(b.path("raylib/lib/raylibdll.lib"));
    } else {
        emulator.linkSystemLibrary("raylib");
    }
    const luaVer = b.option(LuaVersion, "lua", "The version of Lua to use.") orelse LuaVersion.lua54;
    emulator.addCSourceFiles(.{
        .files = &.{
            "src/testLuaArch.c",
            "src/emulator.c",
        },
    });
    compileTheRightLua(b, emulator, luaVer) catch unreachable;

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
