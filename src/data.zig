const std = @import("std");
const c = @cImport({
    @cInclude("neonucleus.h");
});

pub export fn nn_data_crc32(inBuf: [*]const u8, len: usize, outBuf: [*]u8) void {
    var digest = std.hash.Crc32.hash(inBuf[0..len]);
    digest = std.mem.nativeToLittle(u32, digest);

    const digestBuf: [4]u8 = @bitCast(digest);
    std.mem.copyForwards(u8, outBuf[0..4], &digestBuf);
}

pub export fn nn_data_md5(inBuf: [*]const u8, len: usize, outBuf: [*]u8) void {
    std.crypto.hash.Md5.hash(inBuf[0..len], @ptrCast(outBuf), .{});
}

pub export fn nn_data_sha256(inBuf: [*]const u8, len: usize, outBuf: [*]u8) void {
    std.crypto.hash.sha2.Sha256.hash(inBuf[0..len], @ptrCast(outBuf), .{});
}
