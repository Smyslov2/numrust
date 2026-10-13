@echo off
REM Requires MinGW-w64 (g++ in PATH)
g++ -O2 -std=c++17 main.cpp -o RustLike.exe -static -mwindows -lopengl32 -lgdi32 -luser32
pause
