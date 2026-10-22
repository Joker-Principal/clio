#include <argparse/argparse.hpp>
#include <simdjson.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <print>
#include <string>
#include <system_error>

import herodotus.clio.error;
import herodotus.clio.transfer;
import herodotus.clio.index;
import herodotus.clio.ledger;
import herodotus.clio.text;

namespace {

/// 逐层展开 `std::throw_with_nested` 产生的原因链，并标出抛出位置。
void printCause(const std::exception& error, int depth = 0)
{
	const std::string indent(static_cast<std::size_t>(depth) * 2, ' ');

	std::string line = depth > 0 ? indent + "caused by: " : "";
	line += error.what();

	if(const auto* exception = dynamic_cast<const herodotus::clio::Exception*>(&error))
	{
		line += " [";
		line += exception->location().file_name();
		line += ':';
		line += std::to_string(exception->location().line());
		line += ']';
	}
	std::println(stderr, "{}", line);

	if(depth >= 16)
	{
		std::println(stderr, "{}...（原因链超过 16 层，疑似自引用）", indent);
		return;
	}

	try
	{
		std::rethrow_if_nested(error);
	}
	catch(const std::exception& inner)
	{
		printCause(inner, depth + 1);
	}
	catch(...)
	{
		std::println(stderr, "{}caused by: <非 std::exception>", indent);
	}
}


std::int64_t nowMs()
{
	using namespace std::chrono;
	return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string dumpJson(simdjson::builder::string_builder& builder)
{
	try
	{
		return static_cast<std::string>(builder);
	}
	catch(...)
	{
		std::throw_with_nested(herodotus::clio::Exception{std::make_error_code(std::errc::io_error)});
	}
}

template<typename T>
T must(std::expected<T, std::error_code>&& result)
{
	if(!result) { throw herodotus::clio::Exception{result.error()}; }
	return std::move(*result);
}

void mustOk(std::expected<void, std::error_code>&& result)
{
	if(!result) { throw herodotus::clio::Exception{result.error()}; }
}

int run(int argc, char** argv)
{
	argparse::ArgumentParser program{"store_cli", "0.1.0"};
	program.add_description("clio 账本 / 索引维护工具");

	program.add_argument("--data-dir")
		.default_value(std::string{"data"})
		.help("数据目录（包含 ledger.db 与 index.db）");
	program.add_argument("--dict-dir").required().help("cppjieba 词典目录");
	program.add_argument("--persona").default_value(std::string{"default"}).help("目标人格 id");

	argparse::ArgumentParser appendL1{"append-l1"};
	appendL1.add_description("追加一条 L1 版本并写入索引");
	appendL1.add_argument("--record").required().help("记录 id（同 id 多版本构成版本链）");
	appendL1.add_argument("--content").required().help("事实内容（原文）");
	appendL1.add_argument("--type")
		.default_value(std::string{"episodic"})
		.help("记忆类型：persona | episodic | instruction | work_fact | work_task | work_method | work_artifact");
	appendL1.add_argument("--family").default_value(std::string{"chat"}).help("大类：chat | work");
	appendL1.add_argument("--scene").default_value(std::string{""}).help("场景名（可选）");
	appendL1.add_argument("--op").default_value(std::string{"add"}).help("操作：add | merge | edit | forget");
	appendL1.add_argument("--priority").scan<'i', int>().default_value(50).help("优先级 0-100");
	appendL1.add_argument("--skip-index")
		.default_value(false)
		.implicit_value(true)
		.help("只写账本、不更新索引（模拟索引落后）");
	program.add_subparser(appendL1);

	argparse::ArgumentParser search{"search"};
	search.add_description("关键词检索（可一次传入多个查询词）");
	search.add_argument("query")
		.nargs(argparse::nargs_pattern::at_least_one)
		.help("一个或多个查询词（各自独立返回一组命中）");
	search.add_argument("--limit").scan<'i', int>().default_value(10).help("每个查询的返回条数上限");
	program.add_subparser(search);

	argparse::ArgumentParser rebuild{"rebuild"};
	rebuild.add_description("从账本重建索引");
	rebuild.add_argument("--incremental")
		.default_value(false)
		.implicit_value(true)
		.help("只处理账本中尚未索引的版本（默认全量重建）");
	program.add_subparser(rebuild);

	argparse::ArgumentParser dump{"dump"};
	dump.add_description("导出账本为 JSONL");
	dump.add_argument("--out").required().help("输出目录（写入 l0.jsonl / l1.jsonl）");
	dump.add_argument("--persona").help("只导出该人格");
	dump.add_argument("--family").help("只导出该大类（chat | work）");
	dump.add_argument("--scene").help("只导出该场景");
	dump.add_argument("--after-seq").scan<'i', int>().default_value(0).help("只导出 seq 大于该值的记录");
	program.add_subparser(dump);

	argparse::ArgumentParser load{"load"};
	load.add_description("从 JSONL 目录导入账本（导入后需 rebuild 索引）");
	load.add_argument("--in").required().help("输入目录（读取 l0.jsonl / l1.jsonl）");
	load.add_argument("--mode")
		.default_value(std::string{"empty"})
		.help("empty（目标账本必须为空）| merge（按版本位置续接）");
	program.add_subparser(load);

	argparse::ArgumentParser verify{"verify"};
	verify.add_description("账本完整性自检");
	program.add_subparser(verify);

	argparse::ArgumentParser stats{"stats"};
	stats.add_description("输出账本与索引的规模");
	program.add_subparser(stats);

	program.add_epilog(
		"示例：\n"
		"  store_cli --dict-dir D append-l1 --record r-1 --content \"温酒斩华雄\"\n"
		"  store_cli --dict-dir D --persona p-a search \"华雄\" \"温酒\" --limit 5\n"
		"  store_cli --dict-dir D rebuild --incremental\n"
		"  store_cli --dict-dir D dump --out ./dump --persona p-a\n"
		"  store_cli --dict-dir D load --in ./dump --mode merge"
	);

	if(argc <= 1)
	{
		std::println("{}", program.help().str());
		return 0;
	}

	try
	{
		program.parse_args(argc, argv);
	}
	catch(const std::exception& err)
	{
		std::println(stderr, "{}", err.what());
		std::println(stderr, "{}", program.help().str());
		return 2;
	}

	const auto dictDir = program.get<std::string>("--dict-dir");
	const auto dataDir = program.get<std::string>("--data-dir");
	const auto persona = program.get<std::string>("--persona");

	herodotus::clio::text::Tokenizer tokenizer{dictDir};

	namespace fs = std::filesystem;
	herodotus::clio::ledger::Store store{(fs::path{dataDir} / "ledger.db").string()};
	herodotus::clio::index::Index idx{(fs::path{dataDir} / "index.db").string(), tokenizer};

	if(program.is_subcommand_used("append-l1"))
	{
		auto& cmd = program.at<argparse::ArgumentParser>("append-l1");

		herodotus::clio::ledger::L1Version version;
		version.recordId = cmd.get<std::string>("--record");
		version.personaId = persona;
		version.version =
			static_cast<int>(must(store.versionsOf(persona, version.recordId)).size());
		version.content = cmd.get<std::string>("--content");
		version.type = cmd.get<std::string>("--type");
		version.family = cmd.get<std::string>("--family");
		version.sceneName = cmd.get<std::string>("--scene");
		version.op = cmd.get<std::string>("--op");
		version.priority = cmd.get<int>("--priority");
		version.createdAt = nowMs();
		version.updatedAt = version.createdAt;

		version.seq = must(store.appendL1Version(version));
		if(!cmd.get<bool>("--skip-index")) { (void)must(idx.upsert(version)); }

		simdjson::builder::string_builder sb;
		sb.start_object();
		sb.append_key_value<"ok">(true);
		sb.append_comma();
		sb.append_key_value<"record_id">(version.recordId);
		sb.append_comma();
		sb.append_key_value<"version">(version.version);
		sb.end_object();
		std::println("{}", dumpJson(sb));
		return 0;
	}

	if(program.is_subcommand_used("search"))
	{
		auto& cmd = program.at<argparse::ArgumentParser>("search");
		const auto queries = cmd.get<std::vector<std::string>>("query");
		const int limit = cmd.get<int>("--limit");

		simdjson::builder::string_builder sb;
		sb.start_object();
		sb.append_key_value<"ok">(true);
		sb.append_comma();
		sb.escape_and_append_with_quotes("results");
		sb.append_colon();
		sb.start_array();

		for(std::size_t i = 0; i < queries.size(); ++i)
		{
			if(i != 0) { sb.append_comma(); }

			const auto found = must(idx.search(persona, queries[i], limit));

			sb.start_object();
			sb.append_key_value<"query">(queries[i]);
			sb.append_comma();
			sb.append_key_value<"count">(static_cast<std::int64_t>(found.size()));
			sb.append_comma();
			sb.escape_and_append_with_quotes("hits");
			sb.append_colon();
			sb.start_array();
			for(std::size_t k = 0; k < found.size(); ++k)
			{
				if(k != 0) { sb.append_comma(); }
				sb.start_object();
				sb.append_key_value<"record_id">(found[k].recordId);
				sb.append_comma();
				sb.append_key_value<"version">(found[k].version);
				sb.append_comma();
				sb.append_key_value<"score">(found[k].score);
				sb.end_object();
			}
			sb.end_array();
			sb.end_object();
		}

		sb.end_array();
		sb.end_object();
		std::println("{}", dumpJson(sb));
		return 0;
	}

	if(program.is_subcommand_used("rebuild"))
	{
		auto& cmd = program.at<argparse::ArgumentParser>("rebuild");
		const bool incremental = cmd.get<bool>("--incremental");

		std::int64_t processed = 0;
		if(incremental) { processed = must(idx.rebuildIncremental(store)); }
		else { mustOk(idx.rebuildAll(store)); }

		simdjson::builder::string_builder sb;
		sb.start_object();
		sb.append_key_value<"ok">(true);
		sb.append_comma();
		sb.append_key_value<"mode">(incremental ? "incremental" : "full");
		sb.append_comma();
		sb.append_key_value<"processed">(processed);
		sb.append_comma();
		sb.append_key_value<"docs">(must(idx.docCount()));
		sb.append_comma();
		sb.append_key_value<"indexed_seq">(must(idx.indexedSeq()));
		sb.end_object();
		std::println("{}", dumpJson(sb));
		return 0;
	}

	if(program.is_subcommand_used("dump"))
	{
		auto& cmd = program.at<argparse::ArgumentParser>("dump");

		// 筛选里的 string_view 指向下面这几个局部 string，必须活到 exportJsonl 返回
		const std::string personaFilter =
			cmd.is_used("--persona") ? cmd.get<std::string>("--persona") : std::string{};
		const std::string familyFilter =
			cmd.is_used("--family") ? cmd.get<std::string>("--family") : std::string{};
		const std::string sceneFilter =
			cmd.is_used("--scene") ? cmd.get<std::string>("--scene") : std::string{};

		herodotus::clio::transfer::ExportFilter filter;
		if(cmd.is_used("--persona")) { filter.personaId = personaFilter; }
		if(cmd.is_used("--family")) { filter.family = familyFilter; }
		if(cmd.is_used("--scene")) { filter.sceneName = sceneFilter; }
		filter.afterSeq = static_cast<std::int64_t>(cmd.get<int>("--after-seq"));

		const auto result = must(
			herodotus::clio::transfer::exportJsonl(store, cmd.get<std::string>("--out"), filter)
		);

		simdjson::builder::string_builder sb;
		sb.start_object();
		sb.append_key_value<"ok">(true);
		sb.append_comma();
		sb.append_key_value<"l0_rows">(result.l0Rows);
		sb.append_comma();
		sb.append_key_value<"l1_rows">(result.l1Rows);
		sb.append_comma();
		sb.append_key_value<"l0_path">(result.l0Path);
		sb.append_comma();
		sb.append_key_value<"l1_path">(result.l1Path);
		sb.end_object();
		std::println("{}", dumpJson(sb));
		return 0;
	}

	if(program.is_subcommand_used("load"))
	{
		auto& cmd = program.at<argparse::ArgumentParser>("load");

		const std::string modeText = cmd.get<std::string>("--mode");
		herodotus::clio::transfer::ImportMode mode = herodotus::clio::transfer::ImportMode::emptyOnly;
		if(modeText == "merge") { mode = herodotus::clio::transfer::ImportMode::merge; }
		else if(modeText != "empty")
		{
			std::println(stderr, "--mode 只支持 empty 或 merge，收到: {}", modeText);
			return 2;
		}

		const auto result = must(
			herodotus::clio::transfer::importJsonl(cmd.get<std::string>("--in"), store, mode)
		);

		simdjson::builder::string_builder sb;
		sb.start_object();
		sb.append_key_value<"ok">(true);
		sb.append_comma();
		sb.append_key_value<"mode">(modeText);
		sb.append_comma();
		sb.append_key_value<"l0_rows">(result.l0Rows);
		sb.append_comma();
		sb.append_key_value<"l1_rows">(result.l1Rows);
		sb.append_comma();
		sb.append_key_value<"l0_skipped">(result.l0Skipped);
		sb.append_comma();
		sb.append_key_value<"l1_skipped">(result.l1Skipped);
		sb.end_object();
		std::println("{}", dumpJson(sb));
		return 0;
	}

	if(program.is_subcommand_used("verify"))
	{
		const auto report = store.verify();

		simdjson::builder::string_builder sb;
		sb.start_object();
		sb.append_key_value<"ok">(report.ok);
		sb.append_comma();
		sb.append_key_value<"l0_rows">(report.l0Rows);
		sb.append_comma();
		sb.append_key_value<"l1_rows">(report.l1Rows);
		sb.append_comma();
		sb.append_key_value<"l0_max_seq">(report.l0MaxSeq);
		sb.append_comma();
		sb.append_key_value<"l1_max_seq">(report.l1MaxSeq);
		sb.append_comma();
		sb.append_key_value<"error">(report.error);
		sb.end_object();
		std::println("{}", dumpJson(sb));
		return report.ok ? 0 : 1;
	}

	if(program.is_subcommand_used("stats"))
	{
		const auto report = store.verify();

		simdjson::builder::string_builder sb;
		sb.start_object();
		sb.append_key_value<"ok">(report.ok);
		sb.append_comma();
		sb.append_key_value<"l0_rows">(report.l0Rows);
		sb.append_comma();
		sb.append_key_value<"l1_rows">(report.l1Rows);
		sb.append_comma();
		sb.append_key_value<"docs">(must(idx.docCount()));
		sb.append_comma();
		sb.append_key_value<"indexed_seq">(must(idx.indexedSeq()));
		sb.end_object();
		std::println("{}", dumpJson(sb));
		return 0;
	}

	std::println(stderr, "{}", program.help().str());
	return 2;
}

} // namespace

int main(int argc, char** argv)
{
	try
	{
		return run(argc, argv);
	}
	catch(const std::exception& err)
	{
		printCause(err);
		return 3;
	}
}
