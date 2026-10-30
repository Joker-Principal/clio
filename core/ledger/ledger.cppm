module;

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

export module herodotus.clio.ledger;

import herodotus.clio.easy;

export namespace herodotus::clio::ledger {

/// 账本自身的业务约束。
enum class LedgerErrc
{
	init_failed = 1,
	version_not_continuous,
	integrity_failed,
	record_not_found,
	invalid_op,
	forget_content_not_empty,
	internal,
	open_ledger_db_failed,
	busy_timeout_failed,
	init_schema_failed,
};

[[nodiscard]] const std::error_category& ledgerCategory() noexcept;
[[nodiscard]] std::error_code makeError(LedgerErrc code) noexcept;

/// 账本存储的启动期探测结果。
struct LedgerStatus
{
	bool ok{false};
	bool fts5{false};
	std::string sqliteVersion;
	std::string dbPath;
	std::string error; ///< ok == false 时的失败原因
};

/// 打开 / 创建账本并做一次完整性自检。
[[nodiscard]] LedgerStatus probe(const std::string& dataDir);

/// L0：原始消息（一条消息一行，只增）。
struct L0Entry
{
	std::int64_t seq{0}; ///< 账本写入序号（只在读取时填充）
	std::string recordId; ///< 调用方生成的稳定 id（幂等键）
	std::string personaId;
	std::string sessionId;
	std::string role; ///< user | assistant
	std::string content;
	std::int64_t occurredAt{0}; ///< 消息发生时间（epoch ms）
	std::int64_t recordedAt{0}; ///< 服务接收时间（epoch ms）
	std::string source;         ///< dsh | astrbot | manual
};

/// L1：一条原子事实的一个版本（只增；同 record_id 多行）。
struct L1Version
{
	std::int64_t seq{0}; ///< 账本写入序号（只在读取时填充）
	std::string recordId;
	std::string personaId;
	int version{0};
	std::string content;
	std::string type; ///< persona | episodic | instruction | work_fact | work_task | work_method | work_artifact
	int priority{0};
	std::string family; ///< chat | work
	std::string sceneName;
	std::string timestampsJson{"{}"};
	std::string sourceMessageIdsJson{"[]"}; ///< 只进账本，不进索引
	std::string metadataJson{"{}"};
	std::string op; ///< add | merge | edit | forget（forget 为逻辑删除，内容必须为空）
	std::int64_t createdAt{0};
	std::int64_t updatedAt{0};
};

struct VerifyReport
{
	bool ok{false};
	std::int64_t l0Rows{0};
	std::int64_t l1Rows{0};
	std::int64_t l0MaxSeq{0};
	std::int64_t l1MaxSeq{0};
	std::string error;
};

/// 账本存储（一个 `ledger.db` 一个实例）。只提供追加与读取，不提供 UPDATE/DELETE。
class Store
{
public:
	explicit Store(std::string ledgerPath);
	~Store() = default;

	Store(const Store&) = delete;
	Store& operator=(const Store&) = delete;

	/// 幂等追加一条 L0；同 `(personaId, recordId)` 已存在时返回 false。
	std::expected<bool, std::error_code> appendL0(const L0Entry& entry);

	/// 追加一个 L1 版本，返回其账本 `seq`。
	/// 版本必须连续：新记录从 0 开始，其它必须是当前最大版本 + 1。
	std::expected<std::int64_t, std::error_code> appendL1Version(const L1Version& version);

	[[nodiscard]] std::expected<std::vector<L1Version>, std::error_code> versionsOf(
		std::string_view personaId,
		std::string_view recordId
	) const;

	[[nodiscard]] std::expected<std::optional<L1Version>, std::error_code> latestOf(
		std::string_view personaId,
		std::string_view recordId
	) const;

	/// 按写入顺序（seq）返回全部 L1 版本，供索引重建重放。
	[[nodiscard]] std::expected<std::vector<L1Version>, std::error_code> replayL1() const;

	/// 返回 `seq > afterSeq` 的 L1 版本（增量重建用）。
	[[nodiscard]] std::expected<std::vector<L1Version>, std::error_code> replayL1Since(std::int64_t afterSeq) const;

	/// 按写入顺序（seq）返回全部 L0 消息（JSONL 导出用）。
	[[nodiscard]] std::expected<std::vector<L0Entry>, std::error_code> replayL0() const;

	/// `PRAGMA integrity_check` + 行数与最大 seq（只增的轻量校验）。报告式接口，不抛异常。
	[[nodiscard]] VerifyReport verify() const;

	[[nodiscard]] const std::string& path() const noexcept;

private:
	mutable Sqlite::Conn conn_;
	std::string path_;
};

} // namespace herodotus::clio::ledger
