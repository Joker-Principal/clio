# clio

> Herodotus 系统的 **C++ 引擎与守护进程**。名字取自希罗多德序言后首先召唤的缪斯（卷一即 Clio），历史与记录之缪斯，其名意为「使传扬」。
>
> 可执行文件就叫 **`clio`**，不带 `d` 后缀。

> **状态**：本文与 [`../docs/PLAN.md`](../docs/PLAN.md) 同步；标【定】者已确认，未标注默认【待考证】。

## 职责

`clio` 是系统中**唯一持有状态、唯一写存储**的组件。它同时产出两样东西：

| 产物 | 说明 |
| --- | --- |
| 库 `herodotus` | 宿主无关的引擎库——账本、索引、记忆库、知识库、蒸馏、召回、人格、治理 |
| 可执行 `clio` | 守护进程——把上面的能力包成 HTTP API 与 MCP server |

两侧客户端（`dsh-herodotus`、`astrbot_plugin_herodotus`）**永不直接访问数据库**，只通过 `clio` 的 HTTP 接口通信。契约见主仓库 [`docs/PROTOCOL.md`](../docs/PROTOCOL.md)。

## 规划中的目录布局

```
clio\
├─ CMakeLists.txt              CMake 目标定义（库 `herodotus` + 可执行 `clio`）
├─ CMakePresets.json           共享 presets（Ninja Multi-Config）
├─ CMakeUserPresets.json       本机 presets：编译器绝对路径（gitignore）
├─ cmake\deps.cmake            FetchContent 依赖声明
├─ herodotus.clio.cppm         汇总模块：`import herodotus.clio;` 即得全部领域
├─ daemon\main.cpp             可执行入口（普通 TU）
├─ version\version.cppm        版本与协议常量
├─ ledger\                     账本：SQLite，INSERT-only，事件溯源
├─ index\                      索引：SQLite FTS5 + LanceDB 向量，可整体丢弃重建
├─ memory\                     记忆库：per-persona、L0–L3、时效衰减
├─ knowledge\                  知识库：全局共享、带出处与 char 区间、不衰减
├─ distill\                    蒸馏管线：L1 抽取 / 去重合并 / L2 场景 / L3 画像
├─ recall\                     混合召回：FTS + 向量 + RRF k=60 + 时效衰减 + 重排
├─ persona\                    人格注册与路由
├─ governance\                 治理：proposed→confirm
├─ text\                       分词：cppjieba 词 ∪ 拉丁词 ∪ CJK 二元组
├─ serve\                      HTTP API + MCP server
├─ prompts\                    蒸馏 Prompt（移植自 dsh-layered-memory）
└─ tests\
```

> **模块布局约定**
>
> - 每个领域一个具名模块 `herodotus.clio.<domain>`；**不使用** `include/` + `src/` 的头文件布局。
> - 模块接口单元（`.cppm`，含 `export module`）与其实现单元（`.cpp`，`module herodotus.clio.<domain>;`）**放在同一目录下并列**。
> - 模块接口单元用 `import std;`；涉及第三方头（sqlite3 / httplib / simdjson）的实现单元改用 `module;` + `#include`——同一 TU 内标准库头与 `import std;` 不可混用。
> - 汇总模块**不重导出 `std`**，这样普通 TU（如 `daemon/main.cpp`）仍可自由 `#include` 标准库头以取得宏（`SIGINT` 等）。
> - `import std;` 需要 Ninja ≥ 1.11，以及 CMake 的实验性开关 `CMAKE_EXPERIMENTAL_CXX_IMPORT_STD`（UUID 与 CMake 版本绑定）。该开关与编译器路径同属机器相关配置，一并放在 `CMakeUserPresets.json` 的 `local` preset 里（不入库）。

> 目录随各阶段实现逐步建立；**当前 P0 只落地 `version` / `ledger` / `serve` 三个模块与 `daemon`**。

## 规划中的技术栈

| 需要 | 选择 | 备注 |
| --- | --- | --- |
| 语言 / 标准 | **C++23**【定】 | |
| 构建 | **CMake `FetchContent` + `find_package`**【定】 | |
| 全文检索 | **SQLite FTS5**（先） / **Xapian**（备选） | 【待考证】见 `docs/PLAN.md` §8.2 的 D1 |
| 向量库 | **LanceDB**（C-binding，含 ANN）【定】 | |
| 中文分词 | **cppjieba**【定】 | |
| 账本 / 索引 | **SQLite**【定】 | |
| HTTP / JSON | `cpp-httplib` + `simdjson`（C++，Apache-2.0）【定】 | |
| 推理 | **llama-server**（独立进程，OpenAI 兼容 HTTP）【定】 | 不静态链接 |

**关键约束（见 `docs/ARCHITECTURE.md`）**

1. 只监听 **127.0.0.1**，不作为 DSH 子进程启动。【定】
2. 索引必须能从账本重建。【定】**「账本与索引永不合并」【待考证】**。
3. 分词并集里的 **CJK 二元组是召回底线，不可移除**。【定】
4. （若启用 Xapian）未捕获的 `Xapian::Error` 会直接 `terminate`——所有调用须包 `try/catch`；`WritableDatabase` 非线程安全且为排他写锁，须单写者 + 加锁。【待考证】

## 许可

**GPL-3.0**。归属声明见主仓库 [`NOTICE`](../NOTICE)——本组件的分层蒸馏管线与 Prompt 移植自 `dsh-layered-memory`（MIT），治理模式借鉴 `whale-persona`（MIT）。

**发布物绝不捆绑任何模型。**
