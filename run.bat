@echo off
zig build
copy raylib\lib\raylib.dll zig-out\bin
zig build run