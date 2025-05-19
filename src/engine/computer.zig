address: []const u8,
time: f64,
allocator: Allocator,
components: std.StringHashMap(Component),
userdata: *anyopaque,
universe: *Universe,
stack: std.ArrayList(Value),
userError: Error,
isUserErrorAllocated: bool,
architectureData: *anyopaque,
architecture: Universe.Architecture,
architectures: std.ArrayList(Universe.Architecture),
state: State,

pub fn init(address: []const u8, allocator: Allocator, userdata: *anyopaque, architecture: Universe.Architecture, universe: *Universe) Computer {
    const ourAddr = try allocator.dupe(u8, address);
    errdefer allocator.free(ourAddr);

    var c = Computer {
        .address = ourAddr,
        .time = 0,
        .allocator = allocator,
        .components = std.StringHashMap(Component).init(allocator),
        .userdata = userdata,
        .universe = universe,
        .args = std.ArrayList(Value).init(allocator),
        .ret = std.ArrayList(Value).init(allocator),
        .userError = Error {.none = 0},
        .isUserErrorAllocated = false,
        .architectureData = undefined,
        .architecture = architecture,
        .architectures = std.ArrayList(Universe.Architecture).init(allocator),
        .state = State.running,
    };
    c.architectureData = architecture.setup(architecture.udata, &c);
    return c;
}

const std = @import("std");
const Allocator = std.mem.Allocator;
const Computer = @This();
const Component = @import("component.zig");
const Universe = @import("universe.zig");

pub const Error = union(enum) {
    none,
    raw: [*c]const u8,
    allocated: [*c]const u8,
};

pub const State = enum {
    running,
    closing,
    rebooting,
    blackout,
    overworked,
};

pub const Value = union(enum) {
    nil,
    integer: i64,
    number: f64,
    cstring: [*c]const u8,
    string: []const u8,

    pub fn toInteger(self: Value) i64 {
        return switch(self) {
            .integer => |i| i,
            .number => |f| @intFromFloat(f),
            else => 0,
        };
    }
    
    pub fn toNumber(self: Value) f64 {
        return switch(self) {
            .integer => |i| @floatFromInt(i),
            .number => |f| f,
            else => 0,
        };
    }

    pub fn toString(self: Value) ?[]const u8 {
        return switch(self) {
            .string => |s| s,
            .cstring => |c| std.mem.span(c),
            else => null,
        };
    }
    
    pub fn toCString(self: Value) ?[*c]const u8 {
        // normal strings are NOT CAST because it could be a safety violation
        return switch(self) {
            .cstring => |c| c,
            else => null,
        };
    }

    pub fn initNil() Value {
        return Value {.nil = .{}};
    }
    
    pub fn initInteger(i: i64) Value {
        return Value {.integer = i};
    }
    
    pub fn initNumber(f: f64) Value {
        return Value {.number = f};
    }
    
    pub fn initCString(cstr: [*c]const u8, allocator: Allocator) !Value {
        const span = std.mem.span(cstr);
        const mem = try allocator.dupeZ(u8, span);
        return Value {.cstring = mem};
    }
    
    pub fn initString(str: []const u8, allocator: Allocator) !Value {
        const mem = try allocator.dupe(u8, str);
        return Value {.string = mem};
    }

    pub fn deinit(self: Value, allocator: Allocator) void {
        switch(self) {
            .string => |s| allocator.free(s),
            .cstring => |c| allocator.free(c),
            else => {},
        }
    }
};

// Error handling

pub fn clearError(self: *Computer) void {
    switch(self.userError) {
        .allocated => |c| {
            self.allocator.free(c);
        },
        else => {},
    }

    self.userError = Error {.none = .{}};
}

pub fn setCError(self: *Computer, err: [*c]const u8) void {
    self.clearError();
    self.userError = Error {.raw = err};
}

pub fn setError(self: *Computer, err: [*c]const u8) void {
    self.clearError();
    const maybeBuf = self.allocator.dupeZ(u8, std.mem.span(err));
    if(maybeBuf) |buf| {
        self.userError = Error {.allocated = buf};
    } else |e| {
        _ = e;
        self.setCError("out of memory");
    }
}

pub fn getError(self: *const Computer) ?[*c]const u8 {
    return switch(self.userError) {
        .none => null,
        .raw => |c| c,
        .allocated => |c| c,
    };
}

// Component functions

pub fn invoke(self: *Computer, address: []const u8, method: []const u8) void {
    self.setError(null);
    if(self.components.get(address)) |c| {
        c.invoke(method);
        return;
    }
    self.setError("no such component");
}

// end of epilogue, just deletes everything
pub fn resetCall(self: *Computer) void {
    for(self.stack.items) |element| {
        element.deinit(self.allocator);
    }

    // retain capacity for speed
    self.stack.clearRetainingCapacity();
}

pub fn deinit(self: Computer) void {
    // just to be safe
    defer self.components.deinit();
    defer self.stack.deinit();
    defer self.architectures.deinit();

    // absolutely destroy everything
    // burn it all to the ground
    // leave no evidence behind
    self.resetCall();

    self.architecture.demolish(self.architecture.udata, self.architectureData);

    self.clearError();

    var compIter = self.components.iterator();
    while(compIter.next()) |entry| {
        entry.value_ptr.deinit();
    }
}

pub fn process(self: *Computer) void {
    self.clearError();
    self.architecture.tick(self.architecture.udata, self.architectureData, self);
}
