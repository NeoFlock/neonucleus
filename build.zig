const std = @import("std");
const builtin = @import("builtin");

const LibBuildOpts = struct {
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    baremetal: bool,
    bit32: bool,
};

fn addEngineSources(b: *std.Build, opts: LibBuildOpts) *std.Build.Module {
    const dataMod = b.createModule(.{
        .root_source_file = b.path("src/data.zig"),
        .target = opts.target,
        .optimize = opts.optimize,
        .single_threaded = true,
        .sanitize_c = true,
        .valgrind = true,
        .stack_check = true,
        .stack_protector = true,
    });

    dataMod.addCSourceFiles(.{
        .files = &[_][]const u8{
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
            "src/components/drive.c",
            "src/components/screen.c",
            "src/components/gpu.c",
            "src/components/keyboard.c",
        },
        .flags = &.{
            if(opts.baremetal) "-DNN_BAREMETAL" else "",
            if(opts.bit32) "-DNN_BIT32" else "",
        },
    });

    if(!opts.baremetal) {
        dataMod.link_libc = true; // we need a libc
        dataMod.addCSourceFiles(.{
            .files = &.{
                "src/tinycthread.c",
            },
        });
    }

    dataMod.addIncludePath(b.path("src"));

    return dataMod;
}

const LuaVersion = enum {
    lua52,
    lua53,
    lua54,
};

// For the test architecture, we specify the target Lua version we so desire.
// This can be checked for with Lua's _VERSION

fn compileTheRightLua(b: *std.Build, target: std.Build.ResolvedTarget, version: LuaVersion) !*std.Build.Step.Compile {
    const alloc = b.allocator;
    const dirName = @tagName(version);

    const c = b.addObject(.{
        .name = "lua",
        .link_libc = true,
        .optimize = .ReleaseSafe,
        .target = target,
    });

    const rootPath = try std.mem.join(alloc, std.fs.path.sep_str, &.{ "foreign", dirName });

    c.addIncludePath(b.path(rootPath));

    // get all the .c files
    var files = std.ArrayList([]const u8).init(alloc);
    errdefer files.deinit();

    var dir = try std.fs.cwd().openDir(rootPath, std.fs.Dir.OpenDirOptions{ .iterate = true });
    defer dir.close();

    var iter = dir.iterate();

    while (try iter.next()) |e| {
        if (std.mem.startsWith(u8, e.name, "l") and std.mem.endsWith(u8, e.name, ".c") and !std.mem.eql(u8, e.name, "lua.c")) {
            const name = try alloc.dupe(u8, e.name);
            try files.append(name);
        }
    }

    c.addCSourceFiles(.{
        .root = b.path(rootPath),
        .files = files.items,
    });

    return c;
}

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});

    const os = target.result.os.tag;

    const optimize = b.standardOptimizeOption(.{});

    const opts = LibBuildOpts {
        .target = target,
        .optimize = optimize,
        .baremetal = b.option(bool, "baremetal", "Compiles without libc integration") orelse false,
        .bit32 = target.result.ptrBitWidth() == 32,
    };

    const noEmu = b.option(bool, "noEmu", "Disable compiling the emulator (fixes some build system quirks)") orelse false;

    const includeFiles = b.addInstallHeaderFile(b.path("src/neonucleus.h"), "neonucleus.h");
   
    const engineMod = addEngineSources(b, opts);

    const engineStatic = b.addStaticLibrary(.{
        .name = "neonucleus",
        .root_module = engineMod,
    });
    
    const engineShared = b.addSharedLibrary(.{
        .name = if(os == .windows) "neonucleusdll" else "neonucleus",
        .root_module = engineMod,
    });

    const engineStep = b.step("engine", "Builds the engine as a static library");
    engineStep.dependOn(&engineStatic.step);
    engineStep.dependOn(&includeFiles.step);
    engineStep.dependOn(&b.addInstallArtifact(engineStatic, .{}).step);

    const sharedStep = b.step("shared", "Builds the engine as a shared library");
    sharedStep.dependOn(&engineShared.step);
    sharedStep.dependOn(&includeFiles.step);
    sharedStep.dependOn(&b.addInstallArtifact(engineShared, .{}).step);

    if(!noEmu) {
        const emulator = b.addExecutable(.{
            .name = "neonucleus",
            .target = target,
            .optimize = optimize,
        });
        emulator.linkLibC();

        const sysraylib_flag = b.option(bool, "sysraylib", "Use the system raylib instead of compiling raylib") orelse false;
        if (sysraylib_flag) {
            emulator.linkSystemLibrary("raylib");
        } else {
            const raylib = b.dependency("raylib", .{ .target = target, .optimize = optimize });
            emulator.addIncludePath(raylib.path(raylib.builder.h_dir));
            emulator.linkLibrary(raylib.artifact("raylib"));
        }

        const luaVer = b.option(LuaVersion, "lua", "The version of Lua to use.") orelse LuaVersion.lua54;
        emulator.addCSourceFiles(.{
            .files = &.{
                "src/testLuaArch.c",
                "src/emulator.c",
            },
            .flags = &.{
                if(opts.baremetal) "-DNN_BAREMETAL" else "",
                if(opts.bit32) "-DNN_BIT32" else "",
            },
        });
        const l = compileTheRightLua(b, target, luaVer) catch unreachable;

        // forces us to link in everything too
        emulator.addObject(l);
        emulator.linkLibrary(engineStatic);

        const emulatorStep = b.step("emulator", "Builds the emulator");
        emulatorStep.dependOn(&emulator.step);
        emulatorStep.dependOn(&b.addInstallArtifact(emulator, .{}).step);

        var run_cmd = b.addRunArtifact(emulator);

        if (b.args) |args| {
            run_cmd.addArgs(args);
        }

        const run_step = b.step("run", "Run the emulator");
        run_step.dependOn(emulatorStep);
        run_step.dependOn(&run_cmd.step);
    }
}
