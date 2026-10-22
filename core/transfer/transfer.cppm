module;

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

export module herodotus.clio.transfer;

import herodotus.clio.ledger;

export namespace herodotus::clio::transfer {

/// 导入 / 导出的业务约束。
enum class TransferErrc
{
	target_not_empty = 1,
	field_missing,
	record_conflict,
	format_invalid,
	internal,
};

[[nodiscard]] const std::error_category& transferCategory() noexcept;
[[nodiscard]] std::error_code makeError(TransferErrc code) noexcept;

/// 导出筛选：字段为空表示该维度不过滤。
struct ExportFilter
{
	std::optional<std::string_view> personaId;
	std::optional<std::string_view> family;
	std::optional<std::string_view> sceneName;
	std::int64_t afterSeq{0}; ///< 只导出账本 `seq` 大于该值的记录
};

struct ExportStats
{
	std::int64_t l0Rows{0};
	std::int64_t l1Rows{0};
	std::string l0Path;
	std::string l1Path;
};

enum class ImportMode
{
	emptyOnly, ///< 目标账本必须为空，否则 `target_not_empty`
	merge,     ///< 按 (persona, record) 的版本位置续接，已在目标中的前缀跳过
};

struct ImportStats
{
	std::int64_t l0Rows{0};
	std::int64_t l1Rows{0};
	std::int64_t l0Skipped{0}; ///< 因 `(persona, record)` 已存在而跳过的 L0
	std::int64_t l1Skipped{0}; ///< 已在目标版本链中的 L1 前缀
};

/// 账本 → `<outDir>/l0.jsonl` 与 `<outDir>/l1.jsonl`，一行一条记录，按 `seq` 升序。
[[nodiscard]] std::expected<ExportStats, std::error_code> exportJsonl(
	const ledger::Store& store,
	std::string_view outDir,
	const ExportFilter& filter = {}
);

/// `<srcDir>` 下的 JSONL → 账本。`seq` 由目标账本重新分配，`version` 按目标版本链续接。
[[nodiscard]] std::expected<ImportStats, std::error_code> importJsonl(
	std::string_view srcDir,
	ledger::Store& store,
	ImportMode mode
);

/// 单条记录的 JSON（不含换行）。
[[nodiscard]] std::string toJson(const ledger::L0Entry& entry);
[[nodiscard]] std::string toJson(const ledger::L1Version& version);

} // namespace herodotus::clio::transfer
