# ════════════════════════════════════════════════════════════════════
#  Aerofly Link AFS4 Bridge DLL — CMake 构建配置
# ════════════════════════════════════════════════════════════════════
# 生成两个目标：
#   1. AeroflyBridge.dll   —— 被 AFS4 加载的 Bridge DLL
#   2. test_dll.exe         —— 独立测试程序（Mock 模式，无需 AFS4）
#
# 使用方法:
#   mkdir build && cd build
#   cmake -G "Visual Studio 17 2022" ..
#   cmake --build . --config Release
# ════════════════════════════════════════════════════════════════════

cmake_minimum_required(VERSION 3.16)
project(AeroflyBridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── DLL 目标（被 AFS4 加载）──
add_library(AeroflyBridge SHARED
    dll_main.cpp
)
target_link_libraries(AeroflyBridge PRIVATE ws2_32)
target_compile_definitions(AeroflyBridge PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# ── 独立测试程序（控制台，Mock 模式）──
add_executable(test_dll
    dll_main.cpp
)
target_compile_definitions(test_dll PRIVATE
    STANDALONE_TEST
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_link_libraries(test_dll PRIVATE ws2_32)
