module;

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

export module herodotus.clio.index;

import herodotus.clio.easy;
import herodotus.clio.ledger;
import herodotus.clio.text;

export namespace herodotus::clio::index {

/// 索引自身的业务约束。
enum class IndexErrc
{
    init_failed = 1,
    open_index_db_failed,
	watermark_invalid,
	document_not_found,
	internal,
	busy_timeout_failed,
	init_schema_failed,
};

[[nodiscard]] const std::error_category& indexCategory() noexcept;
[[nodiscard]] std::error_code makeError(IndexErrc code) noexcept;

/// 一条关键词命中（只对应记录的最新版本）。
struct Hit
{
	std::string recordId;
	std::string personaId;
	int version{0};
	double score{0.0}; ///< bm25 原始分，越小越相关
};

/// 关键词索引（一个 `index.db` 一个实例）。可随时删除整个文件重建。
class Index
{
public:
	explicit Index(const std::string& indexPath, const text::Tokenizer& tokenizer);
	~Index() = default;

	Index(const Index&) = delete;
	Index& operator=(const Index&) = delete;

	/// 写入或覆盖某条记录的最新版本（同 `(personaId, recordId)` 只保留一份）。返回是否新建。
	std::expected<bool, std::error_code> upsert(const ledger::L1Version& version);

	/// 删除某条记录的索引。返回是否确实存在过。
	std::expected<bool, std::error_code> remove(std::string_view personaId, std::string_view recordId);

	/// 关键词检索。`query` 传原文，内部按分词口径生成 MATCH 表达式。
	[[nodiscard]] std::expected<std::vector<Hit>, std::error_code> search(
		std::string_view personaId,
		std::string_view query,
		int limit = 10
	) const;

	/// 清空后按账本写入顺序全量重建，并把水位推到账本最大 seq。
	std::expected<void, std::error_code> rebuildAll(const ledger::Store& store);

	/// 只处理账本中 `seq` 大于水位的版本；返回处理的条数。
	std::expected<std::int64_t, std::error_code> rebuildIncremental(const ledger::Store& store);

	/// 已索引到的最大账本 `seq`（未建立时为 0）。
	[[nodiscard]] std::expected<std::int64_t, std::error_code> indexedSeq() const;

	[[nodiscard]] std::expected<std::int64_t, std::error_code> docCount() const;

	[[nodiscard]] const std::string& path() const noexcept;

private:
	/// 不做事务边界；调用方负责包裹（批量重建时避免逐条提交）。
	[[nodiscard]] Result<void> upsertNoTxn(const ledger::L1Version& version, bool& created);

	/// 删除一条记录的索引行（不做事务边界）。`existed` 指示原本是否在索引中。
	[[nodiscard]] Result<void> removeNoTxn(std::string_view personaId, std::string_view recordId, bool& existed);

	/// 写入水位（不做事务边界）。
	[[nodiscard]] Result<void> setIndexedSeqNoTxn(std::int64_t seq);

	/// 读水位（不做事务边界）。
	[[nodiscard]] Result<std::int64_t> readWatermark() const;

private:
	/// 分词器必须比本对象活得久（只存引用）。
	const text::Tokenizer& tokenizer_;
	mutable Sqlite::Conn conn_;
	std::string path_;
};

} // namespace herodotus::clio::index
