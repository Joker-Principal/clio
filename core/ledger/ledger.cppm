module;

#include <string>

export module herodotus.clio.ledger;

export namespace herodotus::clio::ledger {

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
///
/// P0 只用来验证存储链路可用；P1 会替换成真正的账本 Store API
/// （只增日志、多版本追加、从账本重建索引）。
[[nodiscard]] LedgerStatus probe(const std::string& dataDir);

} // namespace herodotus::clio::ledger
