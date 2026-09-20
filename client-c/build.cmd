@echo off
rem ============================================================
rem Aerofly Link C client - build script (zig cc, explicit cmds)
rem Prerequisite: zig via vfox (vfox add zig / vfox use zig)
rem Outputs:
rem   build/libfsd.a            core protocol + app library
rem   build/test_protocol.exe   protocol unit tests (P0)
rem   build/test_p2.exe         transport/bridge/config tests (P1+P2)
rem   build/aeroflylink-cli.exe headless client (P2)
rem   build/aeroflylink.exe     GUI client (P3, 静态 CRT 单文件)
rem ============================================================
setlocal
cd /d "%~dp0"

where zig >nul 2>&1
if errorlevel 1 (
    echo [error] zig not found in PATH. Install via: vfox add zig ^&^& vfox use zig
    exit /b 1
)

set "CFLAGS=-std=c11 -Wall -Wextra -Wshadow -O2 -Iinclude -DWIN32_LEAN_AND_MEAN -DNOMINMAX"
set "LIBS=-lws2_32 -ldwmapi -luxtheme -lgdi32 -lcomctl32 -lcomdlg32"
rem 静态链接 CRT（G5：单文件无运行时依赖）；版本号来自 VERSION 单一来源
set "LDFLAGS=-static"
set /p VER=<VERSION
set "VERFLAGS=-DAEROFLYLINK_VERSION=\""%VER%"\""
set "BUILD=build"
if not exist "%BUILD%" mkdir "%BUILD%"

echo [1/6] compiling core...
zig cc %CFLAGS% -c src/protocol.c        -o %BUILD%/protocol.obj        || exit /b 1
zig cc %CFLAGS% -c src/message.c         -o %BUILD%/message.obj         || exit /b 1
zig cc %CFLAGS% -c src/frame.c           -o %BUILD%/frame.obj           || exit /b 1
zig cc %CFLAGS% -c src/json.c            -o %BUILD%/json.obj            || exit /b 1
zig cc %CFLAGS% -c src/config.c          -o %BUILD%/config.obj          || exit /b 1
zig cc %CFLAGS% -c src/net.c             -o %BUILD%/net.obj             || exit /b 1
zig cc %CFLAGS% -c src/session.c         -o %BUILD%/session.obj         || exit /b 1
zig cc %CFLAGS% -c src/bridge.c          -o %BUILD%/bridge.obj          || exit /b 1
zig cc %CFLAGS% -c src/transponder.c     -o %BUILD%/transponder.obj     || exit /b 1
zig cc %CFLAGS% -c src/mock.c            -o %BUILD%/mock.obj            || exit /b 1
zig cc %CFLAGS% -c src/app.c             -o %BUILD%/app.obj             || exit /b 1

echo [2/6] archiving libfsd.a...
zig ar rcs %BUILD%/libfsd.a %BUILD%/protocol.obj %BUILD%/message.obj %BUILD%/frame.obj %BUILD%/json.obj %BUILD%/config.obj %BUILD%/net.obj %BUILD%/session.obj %BUILD%/bridge.obj %BUILD%/transponder.obj %BUILD%/mock.obj %BUILD%/app.obj || exit /b 1

echo [3/6] building protocol tests...
zig cc %CFLAGS% -c tests/test_main.c     -o %BUILD%/test_main.obj       || exit /b 1
zig cc %LDFLAGS% %BUILD%/test_main.obj %BUILD%/libfsd.a %LIBS% -o %BUILD%/test_protocol.exe || exit /b 1

echo [4/6] building transport/app tests + headless client...
zig cc %CFLAGS% -c tests/test_p2.c       -o %BUILD%/test_p2.obj         || exit /b 1
zig cc %LDFLAGS% %BUILD%/test_p2.obj %BUILD%/libfsd.a %LIBS% -o %BUILD%/test_p2.exe || exit /b 1
zig cc %CFLAGS% %VERFLAGS% -c tests/cli_main.c -o %BUILD%/cli_main.obj  || exit /b 1
zig cc %LDFLAGS% %BUILD%/cli_main.obj %BUILD%/libfsd.a %LIBS% -o %BUILD%/aeroflylink-cli.exe || exit /b 1

echo [5/6] building GUI client...
zig cc %CFLAGS% -c src/gui.c             -o %BUILD%/gui.obj             || exit /b 1
zig cc %CFLAGS% -c src/gui_pages.c       -o %BUILD%/gui_pages.obj       || exit /b 1
zig cc %LDFLAGS% %BUILD%/gui.obj %BUILD%/gui_pages.obj %BUILD%/libfsd.a %LIBS% -mwindows -o %BUILD%/aeroflylink.exe || exit /b 1
copy /y aeroflylink.exe.manifest %BUILD%\aeroflylink.exe.manifest >nul

echo [6/6] running tests...
%BUILD%\test_protocol.exe || exit /b 1
%BUILD%\test_p2.exe || exit /b 1
exit /b 0
