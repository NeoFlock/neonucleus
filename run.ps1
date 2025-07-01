zig build

copy lua/lua54.dll zig-out/bin
copy raylib/lib/raylib.dll zig-out/bin

zig build run