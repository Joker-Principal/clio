# 编码约定（clio）

> 本文件只记**跨模块的实现约定**：模块之间必须一致、但读单个文件看不出来的东西。
> 单个函数的实现细节、为什么这样设计，写进对应模块头部或提交说明。

## 1. 错误：`std::expected` 优先，异常只跨越不可控边界

- 对外接口一律 `std::expected<T, std::error_code>`。
- **错误码跟着所属模块走**：`ledger` 的 `LedgerErrc`、`index` 的 `IndexErrc`、`transfer` 的 `TransferErrc`，各自带 `xxxCategory()` 与 `makeError()`。别的模块要解释它时才引入它——**不设集中式「大错误库」**。
- 参数校验与业务约束**直接返回**错误码，不用 `try/catch` 包裹（快速退出）。
- 只有 **SQLite / 文件系统 / 序列化**这类不可控边界才抛 `Exception`：
  它是 `std::system_error` 的派生类，构造时取默认实参 `std::source_location`；**不要传 msg**，靠 `location()` 定位，构造期不做字符串格式化。
- 因果链用 `std::nested_exception` 串起来，打印侧逐层 `rethrow_if_nested`，并设**深度上限**防自引用死循环。

> 上层限制：MS-ABI 下嵌套异常链有长度代价（见主仓库 `docs/REFERENCES.md` §4）。**不要为了追踪而随意加深嵌套**。

## 2. 事务：用 `JK::Meta::Cleanup`，不手写 RAII 类

```cpp
auto guard = JK::Meta::Cleanup{[&] { /* rollback */ }};
// ... 提交成功 ...
guard.resetCleans();   // 成功路径上把回滚动作清掉；异常路径由析构兜底
```

`appendL1Version` 即此形态。理由：手写 RAII 类要为每处事务复制一套"提交标志 + 析构判断"，而 `Cleanup` 把这两步压成一个对象。

## 3. 模块布局

见 [`../README.md`](../README.md) 的「模块布局约定」（具名模块 `herodotus.clio.<domain>`、`.cppm` 与 `.cpp` 同目录并列、汇总模块不重导出 `std`）。
