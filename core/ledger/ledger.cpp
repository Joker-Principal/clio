module;
#include <sqlite3.h>

#include <filesystem>
#include <string>
#include <system_error>

module herodotus.clio.ledger;

namespace herodotus::clio::ledger {
namespace {

// P1 会用真正的账本 schema 替换；这里只证明「存储链路可用」。
constexpr const char* kInitSql = "PRAGMA journal_mode=WAL;"
								 "CREATE TABLE IF NOT EXISTS _meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
								 "PRAGMA integrity_check;";

} // namespace

LedgerStatus probe(const std::string& dataDir)
{
	namespace fs = std::filesystem;

	LedgerStatus status;
	status.sqliteVersion = sqlite3_libversion();
	status.fts5 = sqlite3_compileoption_used("ENABLE_FTS5") != 0;

	std::error_code ec;
	fs::create_directories(dataDir, ec);
	if(ec)
	{
		status.error = "无法创建数据目录 " + dataDir + ": " + ec.message();
		return status;
	}

	status.dbPath = (fs::path(dataDir) / "ledger.db").string();

	sqlite3* db = nullptr;
	if(sqlite3_open_v2(status.dbPath.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
	{
		status.error = std::string{"打开账本失败: "} + (db != nullptr ? sqlite3_errmsg(db) : "unknown");
		if(db != nullptr) { sqlite3_close(db); }
		return status;
	}

	char* err = nullptr;
	if(sqlite3_exec(db, kInitSql, nullptr, nullptr, &err) != SQLITE_OK)
	{
		status.error = std::string{"账本初始化失败: "} + (err != nullptr ? err : "unknown");
		sqlite3_free(err);
		sqlite3_close(db);
		return status;
	}

	sqlite3_close(db);
	status.ok = true;
	return status;
}

} // namespace herodotus::clio::ledger
