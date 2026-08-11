# Aerofly Link

> Open-source FSD connectivity client for Aerofly FS 4.

Aerofly Link is an unofficial community project. It is not affiliated with or endorsed by IPACS.

Aerofly FS 4 第三方联机客户端 — 桥接 FSD 协议服务器（VATSIM / 私有服务器），实现位置共享与 ATC 通讯。

> **注意**：Aerofly FS 4 不支持注入外部飞机模型，因此其他联机玩家的飞机无法在 AFS4 内部显示。Aerofly Link 有一个地图面板来弥补这一限制。

## 功能

- **FSD 协议联机** — 连接 VATSIM 或任何兼容 FSD 协议的服务器
- **实时位置共享** — 将 AFS4 飞机位置（经纬度、高度、航向、速度）上报到 FSD 服务器
- **内置地图面板** — 基于 Leaflet 的 HTML5 地图，显示本机和其他联机飞机
- **应答机控制** — 双轨制应答机（虚拟状态 + DLL 写入），支持 STBY/ALT/IDENT 模式
- **ATC 通讯** — 接收和发送文本通讯
- **飞行计划** — 自动发送最小飞行计划（$FP），确保被服务器纳入广播列表
- **模拟 DLL 模式** — 无需启动 AFS4 即可测试联机功能
- **安装向导** — 内置 PyQt6 安装器，一键部署应用和 AF4 Bridge DLL

## 技术栈

| 组件 | 技术 |
|------|------|
| 核心通信 | Python 3.13 + asyncio |
| UI 框架 | PyQt6 6.11 |
| 地图面板 | QWebEngineView + Leaflet.js |
| AF4 桥接 DLL | C++ (AeroflyBridge.dll) |
| 打包 | PyInstaller 6.21 |

## 项目结构

```
AeroflyLink/
├── main.py                         # 应用入口
├── main_window.py                  # 主窗口（连接页 + 工作区）
├── requirements.txt                # Python 依赖
├── aerofly_link_folder.spec        # PyInstaller 打包配置
│
├── core/                           # 核心逻辑
│   ├── fsd_client.py               # FSD 协议客户端（TCP, asyncio）
│   ├── dll_bridge.py               # AeroflyBridge.dll 双端口桥接
│   ├── transponder.py              # 应答机状态模型
│   ├── transponder_controller.py   # 应答机控制器（双轨制）
│   ├── mock_server.py              # 模拟 DLL 服务器
│   ├── mock_dll_server.py          # Mock DLL 服务器（开发测试）
│   ├── resource_utils.py           # 资源路径工具
│   └── test_*.py                   # 单元测试
│
├── ui/                             # 用户界面
│   ├── connection_panel.py         # 连接配置面板
│   ├── connect_page.py             # 连接页面
│   ├── transponder_panel.py        # 应答机面板
│   ├── flightplan_panel.py         # 飞行计划面板
│   └── log_panel.py                # 日志面板
│
├── assets/                         # 地图资源
│   ├── map.html                    # Leaflet 地图页面
│   ├── leaflet.css / leaflet.js    # Leaflet 库
│   └── images/                     # 地图图标
│
├── dll/                            # AF4 Bridge DLL (C++)
│   ├── dll_main.cpp                # DLL 入口
│   ├── telemetry_server.hpp        # 遥测服务器 (port 12345)
│   ├── command_server.hpp          # 命令服务器 (port 12346)
│   ├── afs4_api.hpp                # AFS4 API 抽象层
│   ├── json.hpp                    # 单头文件 JSON 库
│   └── CMakeLists.txt              # CMake 构建配置
│
├── installer/                      # 安装向导
│   ├── installer.py                # PyQt6 安装器
│   └── installer.spec              # 打包配置
│
├── config/                         # 配置
│   └── settings.example.json       # 配置模板
│
└── tools/                          # 工具脚本
    └── aerofly_link_setup.iss      # Inno Setup 脚本（备用）
```

## 快速开始

### 环境要求

- Python 3.10+
- Aerofly FS 4（可选，用于真实遥测）

### 开发模式运行

```bash
# 1. 安装依赖
pip install -r requirements.txt

# 2. 复制配置模板
cp config/settings.example.json config/settings.json

# 3. 编辑 settings.json，填入你的 VATSIM CID 和密码

# 4. 启动
python main.py
```

### 使用模拟 DLL 模式（无需 AFS4）

1. 启动 Aerofly Link
2. 在连接面板填入坐标（纬度/经度/高度）
3. 点击"模拟DLL"按钮
4. 连接 FSD 服务器

### 构建 AF4 Bridge DLL

> **注意**：本项目使用开源的 [AeroflyBridge.dll](https://github.com/jlgabriel/Aerofly-FS4-Bridge) (v0.3.1+)。
> 如果你的 AFS4 是正版，可以直接使用官方发布的 DLL。非正版需要 hex-patch 导出名 `Aerofly_FS_4_` → `Aerofly_FS_2_`。

```bash
cd dll
mkdir build && cd build
cmake -G "Visual Studio 17 2022" ..
cmake --build . --config Release
```

编译后将 `AeroflyBridge.dll` 放到 `Documents\Aerofly FS 4\external_dll\` 目录。

### 打包发布

```bash
# 1. 打包主程序
pyinstaller aerofly_link_folder.spec --distpath dist --workpath build

# 2. 创建安装包数据
#    将 dist/AeroflyLink/ 打包为 installer/data/aerofly_link.zip
#    将 AeroflyBridge.dll 打包为 installer/data/dll.zip

# 3. 打包安装器
cd installer
pyinstaller installer.spec --distpath dist --workpath build
```

## 通信架构

```
┌─────────────┐     TCP 12345      ┌──────────────┐     TCP 6809     ┌──────────────┐
│  Aerofly FS4 │ ────遥测JSON────→ │  Aerofly Link   │ ────FSD协议────→ │  FSD Server  │
│  + Bridge DLL│ ←──命令JSON────── │   Client      │ ←──交通数据────  │ (VATSIM等)   │
└─────────────┘     TCP 12346      └──────┬───────┘     TCP 6809     └──────────────┘
                                          │
                                    ┌─────┴──────┐
                                    │  PyQt6 UI  │
                                    │  + Leaflet │
                                    │  地图面板   │
                                    └────────────┘
```

### 端口说明

| 端口 | 方向 | 协议 | 用途 |
|------|------|------|------|
| 12345 | DLL → Client | TCP/JSON | 遥测数据（位置、姿态、速度，~20Hz） |
| 12346 | Client → DLL | TCP/JSON | 控制命令（应答机、COM 频率） |
| 6809 | Client ↔ Server | TCP/FSD | FSD 协议（位置报告、通讯、ATC） |

## FSD 协议实现要点

- **位置报告**：`@<mode>:<callsign>:<squawk>:<rating>:<lat>:<lon>:<alt>:<gs>:<pbh>:<alt_diff>`
  - 坐标使用十进制 5 位小数（Swift 兼容）
  - PBH 使用 32-bit 位打包（与 Swift `pbh.h` 一致）
  - 应答机 mode 字母：N=ALT, S=STBY, Y=IDENT（FSD serializer 约定，非 UI 显示用）
- **认证**：`#AP<callsign>:SERVER:<cid>:<password>:<rating>:<revision>:<simtype>:<realname>`
- **飞行计划**：`$FP` 自动发送最小飞行计划
- **兼容性**：处理 `$CQ`/`$CR`/`$DI`/`$ID`/`$PI`/`$PO`/`$ZC`/`$ZR` 等协议消息

### 航向转换

AeroflyBridge.dll 输出航向为**弧度 + 数学约定**（East=0, 逆时针, North=π/2），需要转换为**罗盘约定**（North=0, 顺时针）：

```python
math_deg = (value * RAD_TO_DEG) % 360
compass_hdg = (90 - math_deg) % 360
```

## 配置说明

编辑 `config/settings.json`（从 `settings.example.json` 复制）：

```json
{
  "callsign": "YOUR_CALLSIGN",
  "cid": "YOUR_VATSIM_CID",
  "password": "YOUR_PASSWORD",
  "realname": "Your Name",
  "server": "sweatbox.vatsim.net",
  "port": 6809,
  "rating": 1,
  "mock_lat": "31.1434",
  "mock_lon": "121.8082",
  "mock_alt": "3500"
}
```

| 字段 | 说明 |
|------|------|
| `callsign` | 飞行员呼号（如 `AAL123`） |
| `cid` | VATSIM CID 或服务器用户名 |
| `password` | VATSIM 密码或服务器密码 |
| `rating` | 飞行员等级（1=OBS, 2=S1, 3=S2, 4=S3） |
| `mock_lat/lon/alt` | 模拟 DLL 模式下的初始坐标 |

## 诊断日志

运行时日志写入 `%APPDATA%/Aerofly Link/`：

| 文件 | 用途 |
|------|------|
| `diag.log` | DLL 桥接诊断（连接状态、遥测帧、航向值） |
| `fsd_packets.log` | FSD 位置报告日志（PBH、坐标、高度、速度） |
| `crash.log` | 未捕获异常记录 |

## 致谢

- [AeroflyBridge.dll](https://github.com/jlgabriel/Aerofly-FS4-Bridge) — jlgabriel 的开源 AFS4 桥接 DLL
- [Swift](https://github.com/swift-project/swift) — FSD 协议参考实现
- [Leaflet](https://leafletjs.com/) — 开源 JavaScript 地图库

## License

[LGPL-3.0-only](LICENSE)
