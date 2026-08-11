/**
 * dll_main.cpp —— AFS4 Bridge DLL 入口
 * ================================================================
 * 本 DLL 被 Aerofly FS 4 加载，启动两个本地 TCP 服务器：
 *   localhost:12345  遥测数据输出（DLL → 客户端，~20Hz）
 *   localhost:12346  控制命令输入（客户端 → DLL）
 *
 * 开发阶段可使用 MockAFS4Api 在无 AFS4 环境下测试。
 * 生产环境需替换为 RealAFS4Api（待补充 AFS4 SDK 调用）。
 *
 * 编译（MSVC）:
 *   cl /LD dll_main.cpp /Fe:AeroflyBridge.dll /EHsc
 *
 * 独立测试模式（控制台程序，不生成 DLL）:
 *   cl /DSTANDALONE_TEST dll_main.cpp /Fe:test_dll.exe /EHsc
 *   test_dll.exe [--no-mode-write]
 */

#include "command_server.hpp"
#include "telemetry_server.hpp"
#include "afs4_api.hpp"

#include <windows.h>
#include <memory>
#include <iostream>
#include <string>
#include <csignal>

// 全局服务器实例（DLL 生命周期内持有）
static std::shared_ptr<afs4::CommandServer>   g_cmd_server;
static std::shared_ptr<afs4::TelemetryServer> g_tel_server;
static std::shared_ptr<afs4::IAFS4Api>        g_api;

// ════════════════════════════════════════════════════════════════════
//  初始化：创建 API 实例并启动两个服务器
// ════════════════════════════════════════════════════════════════════

static bool initialize(bool use_mock = true, bool mock_mode_write = true) {
    // ── 创建 AFS4 API 实例 ──
    if (use_mock) {
        g_api = std::make_shared<afs4::MockAFS4Api>(mock_mode_write, true);
        std::cout << "[DLL] 使用 MockAFS4Api（模拟模式写入="
                  << (mock_mode_write ? "支持" : "不支持") << "）" << std::endl;
    } else {
        // TODO: 替换为真实 AFS4 API 实现
        // g_api = std::make_shared<afs4::RealAFS4Api>();
        std::cerr << "[DLL] RealAFS4Api 尚未实现，回退到 Mock" << std::endl;
        g_api = std::make_shared<afs4::MockAFS4Api>(mock_mode_write, true);
    }

    // ── 启动命令输入端口 (12346) ──
    g_cmd_server = std::make_shared<afs4::CommandServer>(g_api);
    if (!g_cmd_server->start()) {
        std::cerr << "[DLL] 命令端口启动失败" << std::endl;
        return false;
    }

    // ── 启动遥测输出端口 (12345) ──
    g_tel_server = std::make_shared<afs4::TelemetryServer>(g_api);
    if (!g_tel_server->start()) {
        std::cerr << "[DLL] 遥测端口启动失败" << std::endl;
        return false;
    }

    std::cout << "[DLL] Aerofly Link Bridge DLL 已启动" << std::endl;
    std::cout << "[DLL]   遥测输出: localhost:12345" << std::endl;
    std::cout << "[DLL]   命令输入: localhost:12346" << std::endl;
    return true;
}

static void shutdown_servers() {
    if (g_cmd_server) { g_cmd_server->stop(); g_cmd_server.reset(); }
    if (g_tel_server) { g_tel_server->stop(); g_tel_server.reset(); }
    g_api.reset();
    std::cout << "[DLL] 服务器已关闭" << std::endl;
}

// ════════════════════════════════════════════════════════════════════
//  DLL 模式：DLL 入口点
// ════════════════════════════════════════════════════════════════════
#ifndef STANDALONE_TEST

/**
 * DLL 入口函数。
 *
 * TODO: AFS4 加载 DLL 时会调用特定的导出函数（具体名称需参照
 *       AFS4 External DLL Sample）。此处提供 DllMain 作为基础框架。
 *       实际使用时需要实现 AFS4 要求的导出函数签名，例如：
 *         extern "C" __declspec(dllexport) void AeroflyInit(...)
 */
BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            // DLL 被加载 —— 启动服务器
            // TODO: 确认 AFS4 的加载时序，可能需要在导出函数中初始化而非 DllMain
            initialize(true);   // 默认 Mock 模式，生产环境改为 false
            break;

        case DLL_PROCESS_DETACH:
            // DLL 被卸载 —— 清理
            shutdown_servers();
            break;
    }
    return TRUE;
}

// TODO: AFS4 External DLL 导出函数（具体签名参照官方 Sample）
// extern "C" __declspec(dllexport)
// int AeroflyDLLInit(void* afs4_context) {
//     g_afs4_context = afs4_context;
//     initialize(false);  // 使用真实 API
//     return 0;
// }

#else  // STANDALONE_TEST
// ════════════════════════════════════════════════════════════════════
//  独立测试模式：控制台程序
// ════════════════════════════════════════════════════════════════════

static std::atomic<bool> g_should_exit(false);

static void signal_handler(int) {
    g_should_exit.store(true);
}

int main(int argc, char* argv[]) {
    bool mock_mode_write = true;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--no-mode-write") {
            mock_mode_write = false;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "用法: test_dll [--no-mode-write]\n"
                      << "  --no-mode-write  模拟 AFS4 不支持外部写入应答机模式\n";
            return 0;
        }
    }

    std::signal(SIGINT, signal_handler);

    if (!initialize(true, mock_mode_write)) {
        std::cerr << "初始化失败" << std::endl;
        return 1;
    }

    std::cout << "\n按 Ctrl+C 退出\n" << std::endl;

    // 主循环等待
    while (!g_should_exit.load()) {
        Sleep(100);
    }

    std::cout << "\n正在关闭..." << std::endl;
    shutdown_servers();
    return 0;
}

#endif  // STANDALONE_TEST
