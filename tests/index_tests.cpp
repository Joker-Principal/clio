#include <cstdint>
#include <exception>
#include <filesystem>
#include <print>
#include <string>
#include <string_view>

import herodotus.clio.index;
import herodotus.clio.ledger;
import herodotus.clio.text;

namespace {

int failures = 0;

void check(bool ok, std::string_view what)
{
	if(ok) { return; }
	++failures;
	std::println(stderr, "FAIL: {}", what);
}

} // namespace

int run(const char* dictDir)
{
	using namespace herodotus::clio;

	text::Tokenizer tokenizer{dictDir};

	const auto root = std::filesystem::temp_directory_path() / "clio-index-tests";
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
	std::filesystem::create_directories(root, ec);

	ledger::Store store{(root / "ledger.db").string()};
	index::Index idx{(root / "index.db").string(), tokenizer};

	auto add = [&](const std::string& persona, const std::string& record, const std::string& content) {
		ledger::L1Version v;
		v.recordId = record;
		v.personaId = persona;

		const auto existing = store.versionsOf(persona, record);
		check(existing.has_value(), "versionsOf: 成功");
		v.version = existing ? static_cast<int>(existing->size()) : 0;

		v.content = content;
		v.type = "episodic";
		v.priority = 50;
		v.family = "work";
		v.op = v.version == 0 ? "add" : "edit";
		v.createdAt = 1767225600000;
		v.updatedAt = 1767225600000 + v.version;

		const auto seq = store.appendL1Version(v);
		check(seq.has_value(), "appendL1Version: 成功");
		if(seq) { v.seq = *seq; }

		const auto inserted = idx.upsert(v);
		check(inserted.has_value(), "upsert: 成功");

		return v;
	};

	add("p-a", "r-1", "温酒斩华雄");
	add("p-a", "r-2", "革故鼎新，FTS5 关键词检索");
	add("p-b", "r-3", "华雄是三国人物");

	// 中文词（jieba 词表内）
	auto hits = idx.search("p-a", "华雄");
	check(hits && !hits->empty() && hits->front().recordId == "r-1", "search: 中文词命中 r-1");

	// 拉丁词
	auto latinHits = idx.search("p-a", "FTS5");
	check(latinHits && !latinHits->empty() && latinHits->front().recordId == "r-2", "search: 拉丁词命中 r-2");

	// 二元组兜底（jieba 可能把"革故鼎新"整词切出，靠 bigram 命中"鼎新"）
	auto bigramHits = idx.search("p-a", "鼎新");
	check(bigramHits && !bigramHits->empty() && bigramHits->front().recordId == "r-2", "search: 二元组兜底命中 r-2");

	// 人格隔离
	auto bWine = idx.search("p-b", "温酒");
	check(bWine && bWine->empty(), "search: persona 隔离（p-b 搜不到 p-a 的内容）");

	auto bHua = idx.search("p-b", "华雄");
	check(bHua && !bHua->empty(), "search: p-b 能搜到自己的内容");

	// 同记录新版本折叠：只保留最新
	add("p-a", "r-1", "华雄被关羽斩杀");

	const auto countAfterFold = idx.docCount();
	check(countAfterFold && *countAfterFold == 3, "upsert: 同记录多版本折叠为一行");

	auto oldHits = idx.search("p-a", "温酒");
	check(oldHits && oldHits->empty(), "upsert: 旧版本内容不再命中");

	auto updated = idx.search("p-a", "关羽");
	check(
		updated && !updated->empty() && updated->front().recordId == "r-1" && updated->front().version == 1,
		"upsert: 新版本可命中且 version == 1"
	);

	// 全量重建后结果一致
	check(idx.rebuildAll(store).has_value(), "rebuildAll: 成功");

	const auto countAfterRebuild = idx.docCount();
	check(countAfterRebuild && *countAfterRebuild == 3, "rebuildAll: 文档数不变");

	auto rebuilt = idx.search("p-a", "关羽");
	check(
		rebuilt && !rebuilt->empty() && rebuilt->front().recordId == "r-1" && rebuilt->front().version == 1,
		"rebuildAll: 结果一致"
	);

	// 删除
	const auto removed = idx.remove("p-a", "r-1");
	check(removed && *removed, "remove: 存在时返回 true");

	auto afterRemove = idx.search("p-a", "关羽");
	check(afterRemove && afterRemove->empty(), "remove: 之后不再命中");

	const auto removedAgain = idx.remove("p-a", "r-1");
	check(removedAgain && !*removedAgain, "remove: 重复删除返回 false");

	// 空查询
	auto emptyQuery = idx.search("p-a", "   ");
	check(emptyQuery && emptyQuery->empty(), "search: 空查询返回空");

	check(!idx.path().empty(), "path: 非空");

	if(failures != 0)
	{
		std::println(stderr, "{} 项失败", failures);
		return 1;
	}
	std::println("index_tests: all passed");
	return 0;
}

int main(int argc, char** argv)
{
	if(argc < 2)
	{
		std::println(stderr, "usage: index_tests <dict-dir>");
		return 2;
	}

	try
	{
		return run(argv[1]);
	}
	catch(const std::exception& e)
	{
		std::println(stderr, "EXCEPTION: {}", e.what());
		return 3;
	}
}
