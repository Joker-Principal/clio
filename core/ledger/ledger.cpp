module;

#include <sqlite3.h>

#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

module herodotus.clio.ledger;

import herodotus.clio.easy;

namespace herodotus::clio::ledger {

namespace {


// P0 的探测用；与 Store 的 schema 互不冲突。
constexpr const char kProbeSql[] = 
	"PRAGMA journal_mode=WAL;"
	"CREATE TABLE IF NOT EXISTS _meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
	"PRAGMA integrity_check;";

constexpr const char kStoreSchema[] = R"SQL(
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;
CREATE TABLE IF NOT EXISTS l0_ledger(
    seq         INTEGER PRIMARY KEY AUTOINCREMENT,
    record_id   TEXT NOT NULL,
    persona_id  TEXT NOT NULL,
    session_id  TEXT NOT NULL,
    role        TEXT NOT NULL,
    content     TEXT NOT NULL,
    occurred_at INTEGER NOT NULL,
    recorded_at INTEGER NOT NULL,
    source      TEXT NOT NULL,
    UNIQUE(persona_id, record_id)
);
CREATE TABLE IF NOT EXISTS l1_ledger(
    seq                     INTEGER PRIMARY KEY AUTOINCREMENT,
    record_id               TEXT NOT NULL,
    persona_id              TEXT NOT NULL,
    version                 INTEGER NOT NULL,
    content                 TEXT NOT NULL,
    type                    TEXT NOT NULL,
    priority                INTEGER NOT NULL,
    family                  TEXT NOT NULL DEFAULT '',
    scene_name              TEXT NOT NULL DEFAULT '',
    timestamps_json         TEXT NOT NULL DEFAULT '{}',
    source_message_ids_json TEXT NOT NULL DEFAULT '[]',
    metadata_json           TEXT NOT NULL DEFAULT '{}',
    op                      TEXT NOT NULL,
    created_at              INTEGER NOT NULL,
    updated_at              INTEGER NOT NULL,
    UNIQUE(persona_id, record_id, version)
);
CREATE INDEX IF NOT EXISTS idx_l1_record ON l1_ledger(persona_id, record_id, version);
)SQL";

constexpr const char* kL0Columns =
	"seq, record_id, persona_id, session_id, role, content, occurred_at, recorded_at, source";

constexpr const char* kL1Columns =
	"seq, record_id, persona_id, version, content, type, priority, family, scene_name, "
	"timestamps_json, source_message_ids_json, metadata_json, op, created_at, updated_at";

class LedgerCategory final : public std::error_category
{
public:
	const char* name() const noexcept override { return "herodotus.ledger"; }

	std::string message(int condition) const override
	{
		switch(static_cast<LedgerErrc>(condition))
		{
		case LedgerErrc::init_failed: return "账本初始化失败";
		case LedgerErrc::version_not_continuous: return "L1 版本不连续";
		case LedgerErrc::integrity_failed: return "账本完整性校验失败";
		case LedgerErrc::record_not_found: return "记录不存在";
		case LedgerErrc::invalid_op: return "非法的 op（只允许 add | merge | edit | forget）";
		case LedgerErrc::forget_content_not_empty: return "forget 版本不能带内容";
		case LedgerErrc::open_ledger_db_failed: return "打开账本数据库失败";
		case LedgerErrc::busy_timeout_failed: return "设置账本 busy_timeout 失败";
		case LedgerErrc::init_schema_failed: return "初始化账本 schema 失败";
		case LedgerErrc::internal: return "账本内部错误";
		}
		return "未知账本错误";
	}
};

/// 取一行填充 `L0Entry`；返回是否取到行。
Result<bool> fetchL0(Sqlite::Stmt& stmt, L0Entry& e)
{
	return stmt.fetch(
		e.seq,
		e.recordId,
		e.personaId,
		e.sessionId,
		e.role,
		e.content,
		e.occurredAt,
		e.recordedAt,
		e.source
	);
}

/// 取一行填充 `L1Version`；返回是否取到行。
Result<bool> fetchL1(Sqlite::Stmt& stmt, L1Version& v)
{
	return stmt.fetch(
		v.seq,
		v.recordId,
		v.personaId,
		v.version,
		v.content,
		v.type,
		v.priority,
		v.family,
		v.sceneName,
		v.timestampsJson,
		v.sourceMessageIdsJson,
		v.metadataJson,
		v.op,
		v.createdAt,
		v.updatedAt
	);
}

/// 建目录后打开数据库（`Conn` 无默认构造，必须在初始化列表里创建）。
Sqlite::Conn openDb(const std::string& path)
try
{
	std::error_code ec;
	const auto parent = std::filesystem::path{path}.parent_path();
	if(!parent.empty())
	{
		std::filesystem::create_directories(parent, ec);
		if(ec) throw Exception{ec};
	}

	auto result = Sqlite::Conn::open(path);
	if(!result) throw Exception{result.error()};
	return std::move(*result);
}
catch(...)
{
	std::throw_with_nested(Exception{makeError(LedgerErrc::open_ledger_db_failed)});
}

/// 执行一条 PRAGMA / schema；失败时包一层模块码（原因链固定为 2 层）。
/// ⚠️ 不要在它外面再包一层：clang-cl 下第 3 层 nested 会让 current_exception() 产出坏指针。
void execOrThrow(Sqlite::Conn& conn, std::string_view sql, LedgerErrc code)
try
{
	if(auto result = conn.exec(sql); !result) { throw Exception{result.error()}; }
}
catch(...)
{
	std::throw_with_nested(Exception{makeError(code)});
}

} // namespace

// ---------------------------------------------------------------------------
// 错误码
// ---------------------------------------------------------------------------

const std::error_category& ledgerCategory() noexcept
{
	static const LedgerCategory instance;
	return instance;
}

std::error_code makeError(LedgerErrc code) noexcept { return {static_cast<int>(code), ledgerCategory()}; }

// ---------------------------------------------------------------------------
// probe（P0 自检）
// ---------------------------------------------------------------------------

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
	if(sqlite3_exec(db, kProbeSql, nullptr, nullptr, &err) != SQLITE_OK)
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

// ---------------------------------------------------------------------------
// Store
// ---------------------------------------------------------------------------

Store::Store(std::string ledgerPath) : conn_(openDb(ledgerPath)), path_(std::move(ledgerPath))
{
	execOrThrow(conn_, "PRAGMA busy_timeout=5000", LedgerErrc::busy_timeout_failed);
	execOrThrow(conn_, kStoreSchema, LedgerErrc::init_schema_failed);
}

std::expected<bool, std::error_code> Store::appendL0(const L0Entry& entry)
{
	auto stmt = conn_.prepare(
		"INSERT OR IGNORE INTO l0_ledger(record_id, persona_id, session_id, role, content, occurred_at, "
		"recorded_at, source) VALUES(?,?,?,?,?,?,?,?)"
	);
	if(!stmt) { return std::unexpected(stmt.error()); }

	if(auto result = stmt->binds(
		   entry.recordId,
		   entry.personaId,
		   entry.sessionId,
		   entry.role,
		   entry.content,
		   entry.occurredAt,
		   entry.recordedAt,
		   entry.source
	   );
	   !result)
	{
		return std::unexpected(result.error());
	}

	if(auto done = stmt->step(); !done) { return std::unexpected(done.error()); }

	return ::sqlite3_changes(conn_.db) > 0;
}

std::expected<std::int64_t, std::error_code> Store::appendL1Version(const L1Version& version)
{
	// op 与「遗忘不带内容」在开事务前先验掉，不合法就不碰库。
	if(version.op != "add" && version.op != "merge" && version.op != "edit" && version.op != "forget")
	{
		return std::unexpected(makeError(LedgerErrc::invalid_op));
	}
	if(version.op == "forget" && !version.content.empty())
	{
		return std::unexpected(makeError(LedgerErrc::forget_content_not_empty));
	}

	if(auto result = conn_.exec("BEGIN IMMEDIATE"); !result) { return std::unexpected(result.error()); }
	auto rollback = Cleanup{[this] { (void)conn_.exec("ROLLBACK"); }};

	{
		auto query = conn_.prepare(
			"SELECT COALESCE(MAX(version), -1) FROM l1_ledger WHERE persona_id=? AND record_id=?"
		);
		if(!query) { return std::unexpected(query.error()); }
		if(auto result = query->binds(version.personaId, version.recordId); !result)
		{
			return std::unexpected(result.error());
		}

		int current = -1;
		if(auto row = query->fetch(current); !row) { return std::unexpected(row.error()); }

		if(version.version != current + 1)
		{
			return std::unexpected(makeError(LedgerErrc::version_not_continuous));
		}
	}

	auto stmt = conn_.prepare(
		"INSERT INTO l1_ledger(record_id, persona_id, version, content, type, priority, family, "
		"scene_name, timestamps_json, source_message_ids_json, metadata_json, op, created_at, "
		"updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)"
	);
	if(!stmt) { return std::unexpected(stmt.error()); }

	if(auto result = stmt->binds(
		   version.recordId,
		   version.personaId,
		   version.version,
		   version.content,
		   version.type,
		   version.priority,
		   version.family,
		   version.sceneName,
		   version.timestampsJson,
		   version.sourceMessageIdsJson,
		   version.metadataJson,
		   version.op,
		   version.createdAt,
		   version.updatedAt
	   );
	   !result)
	{
		return std::unexpected(result.error());
	}

	if(auto done = stmt->step(); !done) { return std::unexpected(done.error()); }

	const std::int64_t newSeq = ::sqlite3_last_insert_rowid(conn_.db);
	if(auto result = conn_.exec("COMMIT"); !result) { return std::unexpected(result.error()); }
	resetCleans(rollback);
	return newSeq;
}

std::expected<std::vector<L1Version>, std::error_code> Store::versionsOf(
	std::string_view personaId,
	std::string_view recordId
) const
{
	const std::string sql = std::string{"SELECT "} + kL1Columns +
	                        " FROM l1_ledger WHERE persona_id=? AND record_id=? ORDER BY version ASC";

	auto stmt = conn_.prepare(sql);
	if(!stmt) { return std::unexpected(stmt.error()); }
	if(auto result = stmt->binds(personaId, recordId); !result) { return std::unexpected(result.error()); }

	std::vector<L1Version> out;
	L1Version v;
	for(;;)
	{
		auto row = fetchL1(*stmt, v);
		if(!row) { return std::unexpected(row.error()); }
		if(!*row) { break; }

		out.push_back(std::move(v));
		v = L1Version{};
	}
	return out;
}

std::expected<std::optional<L1Version>, std::error_code> Store::latestOf(
	std::string_view personaId,
	std::string_view recordId
) const
{
	const std::string sql = std::string{"SELECT "} + kL1Columns +
	                        " FROM l1_ledger WHERE persona_id=? AND record_id=? ORDER BY version DESC LIMIT 1";

	auto stmt = conn_.prepare(sql);
	if(!stmt) { return std::unexpected(stmt.error()); }
	if(auto result = stmt->binds(personaId, recordId); !result) { return std::unexpected(result.error()); }

	L1Version v;
	auto row = fetchL1(*stmt, v);
	if(!row) { return std::unexpected(row.error()); }
	if(!*row) { return std::optional<L1Version>{}; }
	return std::optional<L1Version>{std::move(v)};
}

std::expected<std::vector<L1Version>, std::error_code> Store::replayL1() const
{
	const std::string sql = std::string{"SELECT "} + kL1Columns + " FROM l1_ledger ORDER BY seq ASC";

	auto stmt = conn_.prepare(sql);
	if(!stmt) { return std::unexpected(stmt.error()); }

	std::vector<L1Version> out;
	L1Version v;
	for(;;)
	{
		auto row = fetchL1(*stmt, v);
		if(!row) { return std::unexpected(row.error()); }
		if(!*row) { break; }

		out.push_back(std::move(v));
		v = L1Version{};
	}
	return out;
}

std::expected<std::vector<L1Version>, std::error_code> Store::replayL1Since(std::int64_t afterSeq) const
{
	const std::string sql = std::string{"SELECT "} + kL1Columns + " FROM l1_ledger WHERE seq > ? ORDER BY seq ASC";

	auto stmt = conn_.prepare(sql);
	if(!stmt) { return std::unexpected(stmt.error()); }
	if(auto result = stmt->binds(afterSeq); !result) { return std::unexpected(result.error()); }

	std::vector<L1Version> out;
	L1Version v;
	for(;;)
	{
		auto row = fetchL1(*stmt, v);
		if(!row) { return std::unexpected(row.error()); }
		if(!*row) { break; }

		out.push_back(std::move(v));
		v = L1Version{};
	}
	return out;
}

std::expected<std::vector<L0Entry>, std::error_code> Store::replayL0() const
{
	const std::string sql = std::string{"SELECT "} + kL0Columns + " FROM l0_ledger ORDER BY seq ASC";

	auto stmt = conn_.prepare(sql);
	if(!stmt) { return std::unexpected(stmt.error()); }

	std::vector<L0Entry> out;
	L0Entry e;
	for(;;)
	{
		auto row = fetchL0(*stmt, e);
		if(!row) { return std::unexpected(row.error()); }
		if(!*row) { break; }

		out.push_back(std::move(e));
		e = L0Entry{};
	}
	return out;
}

VerifyReport Store::verify() const
{
	VerifyReport report;

	{
		auto stmt = conn_.prepare("PRAGMA integrity_check");
		if(!stmt)
		{
			report.error = "prepare integrity_check 失败: " + stmt.error().message();
			return report;
		}

		std::string result;
		auto row = stmt->fetch(result);
		if(!row)
		{
			report.error = "integrity_check 失败: " + row.error().message();
			return report;
		}
		if(!*row || result != "ok")
		{
			report.error = "integrity_check: " + result;
			return report;
		}
	}

	auto countRows = [&](const char* table, std::int64_t& rows, std::int64_t& maxSeq) -> std::string
	{
		const std::string sql = std::string{"SELECT COUNT(*), COALESCE(MAX(seq), 0) FROM "} + table;
		auto stmt = conn_.prepare(sql);
		if(!stmt) { return "prepare " + std::string{table} + " 统计失败: " + stmt.error().message(); }

		auto row = stmt->fetch(rows, maxSeq);
		if(!row) { return std::string{table} + " 统计失败: " + row.error().message(); }
		return {};
	};

	if(auto error = countRows("l0_ledger", report.l0Rows, report.l0MaxSeq); !error.empty())
	{
		report.error = std::move(error);
		return report;
	}
	if(auto error = countRows("l1_ledger", report.l1Rows, report.l1MaxSeq); !error.empty())
	{
		report.error = std::move(error);
		return report;
	}

	report.ok = true;
	return report;
}

const std::string& Store::path() const noexcept { return path_; }

} // namespace herodotus::clio::ledger

