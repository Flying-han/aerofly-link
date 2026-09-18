@echo off
rem ============================================================
rem Aerofly Link C client - build script (zig cc, explicit cmds)
rem Prerequisite: zig via vfox (vfox add zig / vfox use zig)
rem Outputs: build/libfsd.a (static lib) + build/test_protocol.exe
rem ============================================================
setlocal
cd /d "%~dp0"

where zig >nul 2>&1
if errorlevel 1 (
    echo [error] zig not found in PATH. Install via: vfox add zig ^&^& vfox use zig
    exit /b 1
)

set "CFLAGS=-std=c11 -Wall -Wextra -Wshadow -O2 -Iinclude"
set "BUILD=build"
if not exist "%BUILD%" mkdir "%BUILD%"

echo [1/4] compiling core...
zig cc %CFLAGS% -c src/protocol.c -o %BUILD%/protocol.obj   || exit /b 1
zig cc %CFLAGS% -c src/message.c  -o %BUILD%/message.obj    || exit /b 1
zig cc %CFLAGS% -c src/frame.c    -o %BUILD%/frame.obj      || exit /b 1

echo [2/4] archiving libfsd.a...
zig ar rcs %BUILD%/libfsd.a %BUILD%/protocol.obj %BUILD%/message.obj %BUILD%/frame.obj || exit /b 1

echo [3/4] building tests...
zig cc %CFLAGS% -c tests/test_main.c -o %BUILD%/test_main.obj || exit /b 1
zig cc %BUILD%/test_main.obj %BUILD%/libfsd.a -o %BUILD%/test_protocol.exe || exit /b 1

echo [4/4] running tests...
%BUILD%\test_protocol.exe
exit /b %errorlevel%
