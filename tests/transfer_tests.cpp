#include <simdjson.h>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <string_view>

import herodotus.clio.transfer;
import herodotus.clio.ledger;

namespace {

int failures = 0;

void check(bool ok, std::string_view what)
{
	if(ok) { return; }
	++failures;
	std::println(stderr, "FAIL: {}", what);
}

std::int64_t countLines(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	std::int64_t lines = 0;
	std::string line;
	while(std::getline(in, line))
	{
		++lines;
	}
	return lines;
}

int run()
{
	using namespace herodotus::clio;

	ledger::L0Entry entry;
	entry.seq = 1;
	entry.recordId = "m-1";
	entry.personaId = "default";
	entry.sessionId = "s-1";
	entry.role = "user";
	entry.content = "他说：\"温酒斩华雄\"\n换行也要转义";
	entry.occurredAt = 1767225600000;
	entry.recordedAt = 1767225601000;
	entry.source = "dsh";

	const std::string l0Json = transfer::toJson(entry);
	{
		simdjson::dom::parser parser;
		simdjson::dom::element doc;
		check(!parser.parse(simdjson::padded_string(l0Json)).get(doc), "toJson(L0): 输出是合法 JSON");

		std::string_view content;
		check(
			!doc["content"].get_string().get(content) && content == entry.content,
			"toJson(L0): content 往返一致"
		);

		std::string_view persona;
		check(
			!doc["persona_id"].get_string().get(persona) && persona == "default",
			"toJson(L0): persona_id 正确"
		);

		std::int64_t seq = 0;
		check(!doc["seq"].get_int64().get(seq) && seq == 1, "toJson(L0): seq 正确");
	}

	ledger::L1Version version;
	version.seq = 7;
	version.recordId = "r-1";
	version.personaId = "default";
	version.version = 0;
	version.content = "用户决定多媒体后置到 P4";
	version.type = "episodic";
	version.priority = 80;
	version.family = "work";
	version.sceneName = "项目规划";
	version.op = "edit";
	version.createdAt = 1767225600000;
	version.updatedAt = 1767225602000;

	const std::string l1Json = transfer::toJson(version);
	{
		simdjson::dom::parser parser;
		simdjson::dom::element l1Doc;
		check(!parser.parse(simdjson::padded_string(l1Json)).get(l1Doc), "toJson(L1): 输出是合法 JSON");

		std::string_view l1Content;
		check(
			!l1Doc["content"].get_string().get(l1Content) && l1Content == version.content,
			"toJson(L1): content 往返一致"
		);

		std::int64_t l1Version = -1;
		check(!l1Doc["version"].get_int64().get(l1Version) && l1Version == 0, "toJson(L1): version 正确");
	}

	// 真实账本导出
	const auto root = std::filesystem::temp_directory_path() / "clio-transfer-tests";
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
	std::filesystem::create_directories(root, ec);

	ledger::Store store{(root / "ledger.db").string()};
	const auto appendedL0 = store.appendL0(entry);
	check(appendedL0 && *appendedL0, "appendL0: 成功");

	const auto appendedL1 = store.appendL1Version(version);
	check(appendedL1.has_value(), "appendL1Version: 成功");

	const auto exported = transfer::exportJsonl(store, (root / "dump").string());
	check(exported && exported->l0Rows == 1, "exportJsonl: L0 行数");
	check(exported && exported->l1Rows == 1, "exportJsonl: L1 行数");
	if(!exported) { return failures == 0 ? 0 : 1; }

	check(countLines(exported->l0Path) == 1, "exportJsonl: l0.jsonl 行数一致");
	check(countLines(exported->l1Path) == 1, "exportJsonl: l1.jsonl 行数一致");

	// 每行都能单独解析
	{
		std::ifstream in(exported->l1Path, std::ios::binary);
		std::string line;
		check(static_cast<bool>(std::getline(in, line)), "exportJsonl: l1.jsonl 可读");

		simdjson::dom::parser parser;
		simdjson::dom::element row;
		check(!parser.parse(simdjson::padded_string(line)).get(row), "exportJsonl: 每行都是合法 JSON");
	}

	// 空账本
	ledger::Store emptyStore{(root / "empty.db").string()};
	const auto emptyStats = transfer::exportJsonl(emptyStore, (root / "dump_empty").string());
	check(emptyStats && emptyStats->l0Rows == 0 && emptyStats->l1Rows == 0, "exportJsonl: 空账本导出行数为 0");

	// 导入到空账本
	ledger::Store target{(root / "target.db").string()};
	const auto imported = transfer::importJsonl((root / "dump").string(), target, transfer::ImportMode::emptyOnly);
	check(imported && imported->l0Rows == 1 && imported->l1Rows == 1, "importJsonl: 行数一致");

	auto targetVersions = target.replayL1();
	check(
		targetVersions && targetVersions->size() == 1 && targetVersions->front().content == version.content &&
			targetVersions->front().personaId == version.personaId,
		"importJsonl: 内容与人格一致"
	);

	// 重复导入应幂等：L0 被唯一键跳过，L1 已被版本链覆盖
	const auto merged = transfer::importJsonl((root / "dump").string(), target, transfer::ImportMode::merge);
	check(
		merged && merged->l0Rows == 0 && merged->l1Rows == 0 && merged->l1Skipped == 1,
		"importJsonl: merge 幂等跳过"
	);

	// 非空目标拒绝 emptyOnly
	const auto rejected =
		transfer::importJsonl((root / "dump").string(), target, transfer::ImportMode::emptyOnly);
	check(
		!rejected && rejected.error() == transfer::makeError(transfer::TransferErrc::target_not_empty),
		"importJsonl: 非空目标拒绝 emptyOnly"
	);

	// persona 过滤
	transfer::ExportFilter filter;
	filter.personaId = "someone-else";
	const auto filtered = transfer::exportJsonl(store, (root / "dump_filtered").string(), filter);
	check(filtered && filtered->l0Rows == 0 && filtered->l1Rows == 0, "exportJsonl: persona 过滤生效");

	return failures == 0 ? 0 : 1;
}

} // namespace

int main()
{
	try
	{
		const int rc = run();
		if(rc == 0) { std::println("transfer_tests: all passed"); }
		return rc;
	}
	catch(const std::exception& e)
	{
		std::println(stderr, "EXCEPTION: {}", e.what());
		return 3;
	}
}
