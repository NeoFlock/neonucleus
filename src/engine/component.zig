address: []const u8,
slot: isize,
allocator: Allocator,
userdata: *anyopaque,
vtable: *const VTable,
computer: *const Computer,

const std = @import("std");
const Allocator = std.mem.Allocator;
const Component = @This();
const Computer = @import("computer.zig");

pub const Method = struct {
    callback: Callback,
    userdata: *anyopaque,
    doc: []const u8,
    direct: bool,

    pub const Callback = *const fn(componentUser: *anyopaque, callbackUser: *anyopaque, computer: *const Computer) callconv(.C) c_int;
};

// not an extern struct because of how C API will work
pub const VTable = struct {
    componentType: []const u8,
    methods: std.StringHashMap(Method),
    resetBudget: ?*const fn(userdata: *anyopaque) callconv(.C) void,
    passive: ?*const fn(userdata: *anyopaque) callconv(.C) void,
    teardown: ?*const fn(userdata: *anyopaque) callconv(.C) void,

    pub fn init(ctype: []const u8, allocator: Allocator) VTable {
        return VTable {
            .componentType = ctype,
            .methods = std.StringHashMap(Method).init(allocator),
            .resetBudget = null,
            .teardown = null,
        };
    }
};

pub fn init(allocator: Allocator, address: []const u8, slot: isize, vtable: *const VTable, userdata: *anyopaque) !Component {
    const ourAddr = try allocator.dupe(u8, address);
    errdefer allocator.free(ourAddr);

    return Component {
        .address = ourAddr,
        .slot = slot,
        .allocator = allocator,
        .userdata = userdata,
        .vtable = vtable,
    };
}

pub fn resetBudget(self: *const Component) void {
    self.vtable.resetBudget(self.userdata);
}

pub fn passive(self: *const Component) void {
    self.vtable.passive(self.userdata);
}

pub fn invoke(self: *const Component, method: []const u8) c_int {
    const res = self.vtable.methods.get(method);
    if(res) |f| {
        return f.callback(self.userdata, f.userdata, self.computer);
    }

    // no such method
    return 0;
}

pub fn deinit(self: Component) void {
    self.vtable.teardown(self.userdata);
    self.allocator.free(self.address);
}
