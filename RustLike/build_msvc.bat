@echo off
REM Open "x64 Native Tools Command Prompt for VS" and run this file
cl /O2 /EHsc /std:c++17 main.cpp /Fe:RustLike.exe /link user32.lib gdi32.lib opengl32.lib /SUBSYSTEM:WINDOWS
pause
