allocator: Allocator,
components: std.StringHashMap(Computer),
host: Host,

const std = @import("std");
const Allocator = std.mem.Allocator;
const Computer = @import("computer.zig");

pub const Architecture = struct {
    name: [*c]const u8,
    udata: *anyopaque,
    setup: *const fn(udata: *anyopaque, computer: *Computer) callconv(.C) *anyopaque,
    tick: *const fn(udata: *anyopaque, context: *anyopaque, computer: *Computer) callconv(.C) void,
    demolish: *const fn(udata: *anyopaque, context: *anyopaque) callconv(.C) void,
};

pub const Host = struct {
};
