# clio

> Herodotus 系统的 **C++ 引擎与守护进程**。名字取自希罗多德序言后首先召唤的缪斯（卷一即 Clio），历史与记录之缪斯，其名意为「使传扬」。
>
> 可执行文件就叫 **`clio`**，不带 `d` 后缀。

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
├─ CMakeLists.txt          （P0）
├─ vcpkg.json              （P0）
├─ include\herodotus\      公开头；库目标名 `herodotus`，可执行目标名 `clio`
├─ src\
│   ├─ ledger\             账本：SQLite，INSERT-only，事件溯源
│   ├─ index\              索引：SQLite FTS5 + USearch 向量，可整体丢弃重建
│   ├─ memory\             记忆库：per-persona、L0–L3、时效衰减
│   ├─ knowledge\          知识库：全局共享、带出处与 char 区间、不衰减
│   ├─ distill\            蒸馏管线：L1 抽取 / 去重合并 / L2 场景 / L3 画像
│   ├─ recall\             混合召回：FTS + 向量 + RRF k=60 + 时效衰减 + 重排
│   ├─ persona\            人格注册与路由
│   ├─ governance\         治理：proposed→confirm
│   ├─ text\               分词：cppjieba 词 ∪ 拉丁词 ∪ CJK 二元组
│   └─ serve\              HTTP API + MCP server
├─ daemon\main.cpp         可执行入口
├─ prompts\                蒸馏 Prompt（移植自 dsh-layered-memory）
└─ tests\
```

> 上述目录已建好占位，**实现留待 P0 起各阶段**。

## 规划中的技术栈

| 需要 | 选择 | 备注 |
| --- | --- | --- |
| 语言 / 标准 | **C++20** | |
| 构建 | **CMake + vcpkg**（manifest 模式） | |
| 全文检索 | **SQLite FTS5**（先） / **Xapian**（备选） | 见 `docs/PLAN.md` §3.2 的 D1 |
| 向量 ANN | **USearch** | header-only，可直接 vendor |
| 中文分词 | **cppjieba** | |
| 账本 / 索引 | **SQLite** | |
| HTTP / JSON | `cpp-httplib` + `nlohmann/json` | 均 header-only |
| 推理 | **llama-server**（独立进程，OpenAI 兼容 HTTP） | 不静态链接 |

**关键约束（见 `docs/ARCHITECTURE.md`）**

1. 只监听 **127.0.0.1**，不作为 DSH 子进程启动。
2. **账本与索引永不合并**；索引必须能从账本重建。
3. 分词并集里的 **CJK 二元组是召回底线，不可移除**。
4. Xapian 未捕获的 `Xapian::Error` 会直接 `terminate`——所有调用必须包 `try/catch`；`WritableDatabase` 非线程安全且为排他写锁，须单写者 + 加锁。

## 许可

**GPL-3.0**。归属声明见主仓库 [`NOTICE`](../NOTICE)——本组件的分层蒸馏管线与 Prompt 移植自 `dsh-layered-memory`（MIT），治理模式借鉴 `whale-persona`（MIT）。

**发布物绝不捆绑任何模型。**
