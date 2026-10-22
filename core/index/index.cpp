module;
#include <charconv>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include <sqlite3.h>

module herodotus.clio.index;

import herodotus.clio.easy;
import herodotus.clio.text;

namespace herodotus::clio::index {
namespace {

class IndexCategory final : public std::error_category
{
public:
	const char* name() const noexcept override { return "herodotus.index"; }

	std::string message(int condition) const override
	{
		switch(static_cast<IndexErrc>(condition))
		{
		case IndexErrc::open_index_db_failed: return "打开索引数据库失败";
		case IndexErrc::watermark_invalid: return "索引水位非法";
		case IndexErrc::document_not_found: return "索引中不存在该文档";
		case IndexErrc::internal: return "索引内部错误";
		case IndexErrc::busy_timeout_failed: return "设置索引 busy_timeout 失败";
		case IndexErrc::init_schema_failed: return "初始化索引 schema 失败";
		}
		return "未知索引错误";
	}
};

} // namespace

const std::error_category& indexCategory() noexcept
{
	static const IndexCategory instance;
	return instance;
}

std::error_code makeError(IndexErrc code) noexcept { return {static_cast<int>(code), indexCategory()}; }

/// 建目录后打开数据库（`Conn` 无默认构造，必须在初始化列表里创建）。
static Sqlite::Conn openDb(const std::string& path)
try {
	const auto parent = std::filesystem::path{path}.parent_path();
	if(!parent.empty())
	{
	    std::error_code ec;
		std::filesystem::create_directories(parent, ec);
		if(ec) throw Exception{ec};
	}

	auto result = Sqlite::Conn::open(path);
	if(!result) throw Exception{result.error()};
	return std::move(*result);
}
catch(...)
{
    std::throw_with_nested(Exception(makeError(IndexErrc::open_index_db_failed)));
}

Result<void> Index::upsertNoTxn(const ledger::L1Version& v, bool& created)
{
	const std::string terms = tokenizer_.indexText(v.content);

	std::int64_t docId = 0;
	bool exists = false;
	{
		auto query = conn_.prepare("SELECT doc_id FROM fts_doc WHERE persona_id=? AND record_id=?");
		if(!query) { return std::unexpected(query.error()); }
		if(auto result = query->binds(v.personaId, v.recordId); !result)
		{
			return std::unexpected(result.error());
		}

		auto row = query->fetch(docId);
		if(!row) { return std::unexpected(row.error()); }
		exists = *row;
	}

	if(exists)
	{
		{
			auto del = conn_.prepare("DELETE FROM l1_fts WHERE rowid=?");
			if(!del) { return std::unexpected(del.error()); }
			if(auto result = del->binds(docId); !result) { return std::unexpected(result.error()); }
			if(auto done = del->step(); !done) { return std::unexpected(done.error()); }
		}
		{
			auto update = conn_.prepare("UPDATE fts_doc SET version=?, updated_at=? WHERE doc_id=?");
			if(!update) { return std::unexpected(update.error()); }
			if(auto result = update->binds(v.version, v.updatedAt, docId); !result)
			{
				return std::unexpected(result.error());
			}
			if(auto done = update->step(); !done) { return std::unexpected(done.error()); }
		}
	}
	else
	{
		auto insert =
			conn_.prepare("INSERT INTO fts_doc(persona_id, record_id, version, updated_at) VALUES(?,?,?,?)");
		if(!insert) { return std::unexpected(insert.error()); }
		if(auto result = insert->binds(v.personaId, v.recordId, v.version, v.updatedAt); !result)
		{
			return std::unexpected(result.error());
		}
		if(auto done = insert->step(); !done) { return std::unexpected(done.error()); }

		docId = ::sqlite3_last_insert_rowid(conn_.db);
	}

	{
		auto fts = conn_.prepare("INSERT INTO l1_fts(rowid, terms) VALUES(?,?)");
		if(!fts) { return std::unexpected(fts.error()); }
		if(auto result = fts->binds(docId, terms); !result) { return std::unexpected(result.error()); }
		if(auto done = fts->step(); !done) { return std::unexpected(done.error()); }
	}

	created = !exists;
	return {};
}

static constexpr const char kSchema[] = R"SQL(
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;
CREATE TABLE IF NOT EXISTS fts_doc(
    doc_id     INTEGER PRIMARY KEY,
    persona_id TEXT NOT NULL,
    record_id  TEXT NOT NULL,
    version    INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    UNIQUE(persona_id, record_id)
);
CREATE VIRTUAL TABLE IF NOT EXISTS l1_fts USING fts5(
    terms,
    tokenize = "unicode61 remove_diacritics 2"
);
CREATE TABLE IF NOT EXISTS meta(
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
)SQL";

/// 执行一条 PRAGMA / schema；失败时包一层模块码（原因链固定为 2 层）。
/// ⚠️ 不要在它外面再包一层：clang-cl 下第 3 层 nested 会让 current_exception() 产出坏指针。
static void execOrThrow(Sqlite::Conn& conn, std::string_view sql, IndexErrc code)
try
{
	if(auto result = conn.exec(sql); !result) { throw Exception{result.error()}; }
}
catch(...)
{
	std::throw_with_nested(Exception{makeError(code)});
}

Index::Index(const std::string& indexPath, const text::Tokenizer& tokenizer) :
	tokenizer_(tokenizer),
	conn_(openDb(indexPath))
{
	path_ = indexPath;
	execOrThrow(conn_, "PRAGMA busy_timeout=5000", IndexErrc::busy_timeout_failed);
	execOrThrow(conn_, kSchema, IndexErrc::init_schema_failed);
}

std::expected<bool, std::error_code> Index::upsert(const ledger::L1Version& version)
{
	if(auto result = conn_.exec("BEGIN IMMEDIATE"); !result) { return std::unexpected(result.error()); }
	auto rollback = Cleanup{[this] { (void)conn_.exec("ROLLBACK"); }};

	bool created = false;
	if(auto result = upsertNoTxn(version, created); !result) { return std::unexpected(result.error()); }

	// 单条写入也推进水位，避免下次增量重建重复处理
	auto watermark = readWatermark();
	if(!watermark) { return std::unexpected(watermark.error()); }
	if(version.seq > *watermark)
	{
		if(auto result = setIndexedSeqNoTxn(version.seq); !result) { return std::unexpected(result.error()); }
	}

	if(auto result = conn_.exec("COMMIT"); !result) { return std::unexpected(result.error()); }
	resetCleans(rollback);
	return created;
}

std::expected<bool, std::error_code> Index::remove(std::string_view personaId, std::string_view recordId)
{
	if(auto result = conn_.exec("BEGIN IMMEDIATE"); !result) { return std::unexpected(result.error()); }
	auto rollback = Cleanup{[this] { (void)conn_.exec("ROLLBACK"); }};

	std::int64_t docId = 0;
	bool exists = false;
	{
		auto query = conn_.prepare("SELECT doc_id FROM fts_doc WHERE persona_id=? AND record_id=?");
		if(!query) { return std::unexpected(query.error()); }
		if(auto result = query->binds(personaId, recordId); !result)
		{
			return std::unexpected(result.error());
		}

		auto row = query->fetch(docId);
		if(!row) { return std::unexpected(row.error()); }
		exists = *row;
	}

	if(exists)
	{
		{
			auto delFts = conn_.prepare("DELETE FROM l1_fts WHERE rowid=?");
			if(!delFts) { return std::unexpected(delFts.error()); }
			if(auto result = delFts->binds(docId); !result) { return std::unexpected(result.error()); }
			if(auto done = delFts->step(); !done) { return std::unexpected(done.error()); }
		}
		{
			auto delDoc = conn_.prepare("DELETE FROM fts_doc WHERE doc_id=?");
			if(!delDoc) { return std::unexpected(delDoc.error()); }
			if(auto result = delDoc->binds(docId); !result) { return std::unexpected(result.error()); }
			if(auto done = delDoc->step(); !done) { return std::unexpected(done.error()); }
		}
	}

	if(auto result = conn_.exec("COMMIT"); !result) { return std::unexpected(result.error()); }
	resetCleans(rollback);
	return exists;
}

std::expected<std::vector<Hit>, std::error_code> Index::search(
	std::string_view personaId,
	std::string_view query,
	int limit
) const
{
	std::vector<Hit> out;
	if(limit <= 0) { return out; }

	const std::string expr = tokenizer_.queryExpr(query);
	if(expr.empty()) { return out; }

	auto stmt = conn_.prepare(
		"SELECT d.record_id, d.persona_id, d.version, bm25(l1_fts) "
		"FROM l1_fts JOIN fts_doc d ON d.doc_id = l1_fts.rowid "
		"WHERE l1_fts MATCH ? AND d.persona_id = ? "
		"ORDER BY bm25(l1_fts) LIMIT ?"
	);
	if(!stmt) { return std::unexpected(stmt.error()); }
	if(auto result = stmt->binds(expr, personaId, limit); !result)
	{
		return std::unexpected(result.error());
	}

	Hit hit;
	for(;;)
	{
		auto row = stmt->fetch(hit.recordId, hit.personaId, hit.version, hit.score);
		if(!row) { return std::unexpected(row.error()); }
		if(!*row) { break; }

		out.push_back(std::move(hit));
		hit = Hit{};
	}
	return out;
}

std::expected<void, std::error_code> Index::rebuildAll(const ledger::Store& store)
{
	auto versions = store.replayL1();
	if(!versions) { return std::unexpected(versions.error()); }

	if(auto result = conn_.exec("BEGIN IMMEDIATE"); !result) { return std::unexpected(result.error()); }
	auto rollback = Cleanup{[this] { (void)conn_.exec("ROLLBACK"); }};

	if(auto result = conn_.exec("DELETE FROM l1_fts; DELETE FROM fts_doc;"); !result)
	{
		return std::unexpected(result.error());
	}

	bool created = false;
	std::int64_t maxSeq = 0;
	for(const auto& version : *versions)
	{
		if(auto result = upsertNoTxn(version, created); !result) { return std::unexpected(result.error()); }
		if(version.seq > maxSeq) { maxSeq = version.seq; }
	}
	if(auto result = setIndexedSeqNoTxn(maxSeq); !result) { return std::unexpected(result.error()); }

	if(auto result = conn_.exec("COMMIT"); !result) { return std::unexpected(result.error()); }
	resetCleans(rollback);
	return {};
}

std::expected<std::int64_t, std::error_code> Index::rebuildIncremental(const ledger::Store& store)
{
	auto watermark = readWatermark();
	if(!watermark) { return std::unexpected(watermark.error()); }

	const std::int64_t after = *watermark;
	auto versions = store.replayL1Since(after);
	if(!versions) { return std::unexpected(versions.error()); }
	if(versions->empty()) { return 0; }

	if(auto result = conn_.exec("BEGIN IMMEDIATE"); !result) { return std::unexpected(result.error()); }
	auto rollback = Cleanup{[this] { (void)conn_.exec("ROLLBACK"); }};

	bool created = false;
	std::int64_t maxSeq = after;
	for(const auto& version : *versions)
	{
		if(auto result = upsertNoTxn(version, created); !result) { return std::unexpected(result.error()); }
		if(version.seq > maxSeq) { maxSeq = version.seq; }
	}
	if(auto result = setIndexedSeqNoTxn(maxSeq); !result) { return std::unexpected(result.error()); }

	if(auto result = conn_.exec("COMMIT"); !result) { return std::unexpected(result.error()); }
	resetCleans(rollback);
	return static_cast<std::int64_t>(versions->size());
}

std::expected<std::int64_t, std::error_code> Index::indexedSeq() const { return readWatermark(); }

Result<std::int64_t> Index::readWatermark() const
{
	auto stmt = conn_.prepare("SELECT value FROM meta WHERE key='l1_seq'");
	if(!stmt) { return std::unexpected(stmt.error()); }

	std::string value;
	auto row = stmt->fetch(value);
	if(!row) { return std::unexpected(row.error()); }
	if(!*row || value.empty()) { return 0; }

	std::int64_t seq = 0;
	const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), seq);
	if(ec != std::errc{} || ptr != value.data() + value.size())
	{
		return std::unexpected(makeError(IndexErrc::watermark_invalid));
	}
	return seq;
}

Result<void> Index::setIndexedSeqNoTxn(std::int64_t seq)
{
	auto stmt = conn_.prepare(
		"INSERT INTO meta(key, value) VALUES('l1_seq', ?) "
		"ON CONFLICT(key) DO UPDATE SET value=excluded.value"
	);
	if(!stmt) { return std::unexpected(stmt.error()); }

	const std::string text = std::to_string(seq);
	if(auto result = stmt->binds(text); !result) { return std::unexpected(result.error()); }
	if(auto done = stmt->step(); !done) { return std::unexpected(done.error()); }
	return {};
}

std::expected<std::int64_t, std::error_code> Index::docCount() const
{
	auto stmt = conn_.prepare("SELECT COUNT(*) FROM fts_doc");
	if(!stmt) { return std::unexpected(stmt.error()); }

	std::int64_t count = 0;
	auto row = stmt->fetch(count);
	if(!row) { return std::unexpected(row.error()); }
	return count;
}

const std::string& Index::path() const noexcept { return path_; }

} // namespace herodotus::clio::index
