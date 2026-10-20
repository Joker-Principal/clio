# 黄金语料集（P0）

由参考实现的 DSH-MemBench 场景库转换而来（`dsh-layered-memory/bench/scenarios/*.json`，MIT，见主仓库 `NOTICE` §一）。转换脚本：`scripts/build_golden.py`（可重复生成，输出稳定排序）。

## 文件

| 文件 | 内容 |
| --- | --- |
| `corpus.jsonl` | 65 条语料：每行一个会话消息（`id` / `scene` / `family` / `kind` / `text`） |
| `queries.jsonl` | 140 条查询：`id` / `scene` / `type` / `query` / `gold` / `stale` |

当前规模：**20 个场景 · 65 条语料 · 140 条查询**（其中 120 条带 `gold`，20 条为拒答题）。

题型分布：`extraction` 20 · `multihop` 20 · `temporal` 20 · `update` 20 · `scene` 20 · `abstention` 20 · `accretive` 5 · `update-chain` 5 · `ordering` 5 · `paraphrase` 5。

## 评测口径（与参考实现 `retrieval-metrics.mjs` 对齐）

- **语料**：P0 用会话原文（尚未蒸馏）。P2 之后可换成 clio 自己的 L1 记录，**查询集不变**，用于区分「蒸馏质量」与「检索质量」。
- **召回判定**：检索结果的文本包含某条 `gold` 要点即算命中；`coverage` = 命中 gold 要点数 / 该题 gold 总数。
- **MRR**：第一条命中 gold 的结果的名次倒数。
- **更新泄漏**：`update` / `update-chain` 题的 `stale` 要点出现在检索结果中即计入泄漏（说明旧版本未被覆盖）。
- **拒答题**（`abstention`，无 `gold`）：不参与 recall，单独统计是否错误返回了结果。
- 建议指标：`recall@5`、`coverage`、`MRR`、`stale 泄漏率`。

## 重新生成

```powershell
python scripts/build_golden.py
# 参考实现不在此路径时：
python scripts/build_golden.py --src <path-to-dsh-layered-memory>
```
