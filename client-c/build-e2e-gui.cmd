@echo off
rem Windows CMD requires CRLF in this script; enforced by .gitattributes.
rem Build a test-only GUI variant that accepts a synthetic E2E password from
rem AEROFLYLINK_SMOKE_PASSWORD and injects only a local JWT test provider.
setlocal
cd /d "%~dp0"

where zig >nul 2>&1
if errorlevel 1 (
    echo [error] zig not found in PATH. Activate the existing vfox-managed Zig SDK.
    exit /b 1
)
if not exist build\libfsd.a (
    echo [error] run build.cmd first so the core library and Nuklear object exist.
    exit /b 1
)

set "CFLAGS=-std=c11 -Wall -Wextra -Wshadow -O2 -Iinclude -DWIN32_LEAN_AND_MEAN -DNOMINMAX"
set "LIBS=-lws2_32 -ldwmapi -luxtheme -lgdi32 -lcomctl32 -lcomdlg32 -lwinhttp -lmsimg32"
set "BUILD=build"

echo [1/3] compiling E2E-only GUI and local JWT provider...
zig cc %CFLAGS% -DAEROFLYLINK_E2E_GUI -c src/gui.c -o %BUILD%/e2e_gui.obj || exit /b 1
zig cc %CFLAGS% -c tests/e2e_jwt_provider_stub.c -o %BUILD%/e2e_jwt_provider_stub.obj || exit /b 1

echo [2/3] linking isolated test GUI...
zig cc -static %BUILD%/e2e_gui.obj %BUILD%/nuklear.obj %BUILD%/e2e_jwt_provider_stub.obj %BUILD%/libfsd.a %LIBS% -Wl,/subsystem:windows -o %BUILD%/aeroflylink-e2e.exe || exit /b 1
copy /y aeroflylink.exe.manifest %BUILD%\aeroflylink-e2e.exe.manifest >nul

echo [3/3] E2E GUI ready: %BUILD%\aeroflylink-e2e.exe
exit /b 0
