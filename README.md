# clio

> Herodotus 系统的 **C++ 引擎与守护进程**。名字取自希罗多德序言后首先召唤的缪斯（卷一即 Clio），历史与记录之缪斯，其名意为「使传扬」。
>
> 可执行文件就叫 **`clio`**，不带 `d` 后缀。

> 文档标记见主仓库 [`../README.md`](../README.md)；决策与排期见 [`../docs/PLAN.md`](../docs/PLAN.md)；不变量见 [`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md) §2。

## 职责

`clio` 是系统中**唯一持有状态、唯一写存储**的组件。它同时产出两样东西：

| 产物 | 说明 |
| --- | --- |
| 库 `herodotus` | 宿主无关的引擎库——账本、索引、记忆库、知识库、蒸馏、召回、人格、治理 |
| 可执行 `clio` | 守护进程——把上面的能力包成 HTTP API 与 MCP server |

两侧客户端（`dsh-herodotus`、`astrbot_plugin_herodotus`）**不直接访问数据库**，只通过 `clio` 的 HTTP 接口通信。契约见主仓库 [`docs/PROTOCOL.md`](../docs/PROTOCOL.md)。

## 目录布局

```
clio\
├─ CMakeLists.txt              库 `herodotus` + 可执行 `clio`
├─ CMakePresets.json           共享 presets（Ninja Multi-Config）
├─ CMakeUserPresets.json       本机 presets：编译器路径等（gitignore）
├─ cmake\deps.cmake            依赖声明（vcpkg manifest 对应的 find_package）
├─ core\                       引擎本体
│   ├─ clio.cppm               汇总模块：`import herodotus.clio;` 即得全部领域
│   ├─ easy.cppm               `JK::Meta` / `JK::Wrap` 的别名（只给 core 用）
│   ├─ error.cppm              `Exception`：std::system_error + source_location
│   ├─ version.cppm            版本与协议常量
│   ├─ ledger\                 账本：SQLite，只增不改
│   ├─ index\                  索引：FTS5，可整体丢弃重建
│   ├─ text\                   分词：cppjieba 词 ∪ 拉丁词 ∪ CJK 二元组
│   ├─ transfer\               JSONL 导出 / 导入
│   ├─ prompts\                蒸馏 Prompt
│   └─ serve\                  HTTP API + MCP server
├─ daemon\main.cpp             可执行入口
├─ tools\                      store_cli / tokenize_spike
├─ tests\                      四个模块级测试
└─ docs\                       编码约定（跨模块的实现约定）
```

> **模块布局约定**
>
> - 每个领域一个具名模块 `herodotus.clio.<domain>`；**不使用** `include/` + `src/` 的头文件布局。
> - 模块接口单元（`.cppm`，含 `export module`）与其实现单元（`.cpp`，`module herodotus.clio.<domain>;`）**放在同一目录下并列**。
> - 模块接口单元用 `import std;`；涉及第三方头（sqlite3 / asio / simdjson）的实现单元改用 `module;` + `#include`——同一 TU 内标准库头与 `import std;` 不可混用。
> - 汇总模块**不重导出 `std`**，这样普通 TU（如 `daemon/main.cpp`）仍可自由 `#include` 标准库头以取得宏（`SIGINT` 等）。
> - `import std;` 需要 Ninja ≥ 1.11，以及 CMake 的实验性开关 `CMAKE_EXPERIMENTAL_CXX_IMPORT_STD`（UUID 与 CMake 版本绑定）。该开关与编译器路径同属机器相关配置，一并放在 `CMakeUserPresets.json`（不入库）。

> 上表是**当前实际结构**。尚未建立：`memory` / `knowledge` / `distill` / `recall` / `persona` / `governance`，落地阶段见主仓库 [`docs/PLAN.md`](../docs/PLAN.md) §7。

## 选型 · 不变量 · 约定

这三样都在别处定义，本文件不复述：

| 内容 | 在哪 |
| --- | --- |
| 技术选型 | 主仓库 [`docs/PLAN.md`](../docs/PLAN.md) §3 |
| 不变量（含编号与状态） | 主仓库 [`docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md) §2 |
| 存储结构与表结构 | 主仓库 [`docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md) §3 |
| 编码约定 | 本仓 [`docs/coding-discipline.md`](docs/coding-discipline.md) |

## 构建与工具链

### 现在由什么构成

| 环节 | 现状 |
| --- | --- |
| **构建** | CMake Preset 3.28+ + **Ninja Multi-Config**。C++23 + C++ 模块。 |
| **编译器** | **MSYS2 clang64**（clang + MinGW ABI + UCRT + libc++）。 |
| **依赖** | **vcpkg manifest 模式**：`vcpkg.json` 只写依赖与 feature，`cmake/deps.cmake` 只写 `find_package`。第三方库不走 `FetchContent`（仅 cppjieba、argparse 例外）。 |
| **私有端口** | **`joker-registry`**（git registry）提供 `pylai` / `jk` / `asio` 三个端口。 |

### 目前(2026-10-22)的工具链的局限性

1. MSVC: `pylai` 使用了模块并且 export 的接口涉及 asio::io_context，目前它无法正确导出

2. clang-cl: 本项目采用嵌套异常链追踪调用栈，而 clang-cl 在这个模式下有上游缺陷。结论与适用范围见主仓库
   [`docs/REFERENCES.md`](../docs/REFERENCES.md) §4。

## 许可

**GPL-3.0**。归属声明见主仓库 [`NOTICE`](../NOTICE)——分层蒸馏管线与 Prompt 参考 `dsh-layered-memory`（MIT）改写，治理模式借鉴 `whale-persona`（MIT）。

**发布物不捆绑任何模型。**
