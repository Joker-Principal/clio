module;

#include <simdjson.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

module herodotus.clio.transfer;

import herodotus.clio.easy;

namespace herodotus::clio::transfer {
namespace {

using simdjson::builder::string_builder;

class TransferCategory final : public std::error_category
{
public:
	const char* name() const noexcept override { return "herodotus.transfer"; }

	std::string message(int condition) const override
	{
		switch(static_cast<TransferErrc>(condition))
		{
		case TransferErrc::target_not_empty: return "导入目标非空";
		case TransferErrc::field_missing: return "导入数据缺少必填字段";
		case TransferErrc::record_conflict: return "导入数据与账本冲突";
		case TransferErrc::format_invalid: return "导入数据格式非法";
		case TransferErrc::internal: return "导入导出内部错误";
		}
		return "未知导入导出错误";
	}
};

std::string finish(string_builder& builder)
try
{
    return static_cast<std::string>(builder);
}
catch(...)
{
    std::throw_with_nested(Exception{makeError(TransferErrc::internal)});
}

std::optional<std::string_view> stringField(simdjson::dom::element doc, std::string_view key)
{
	simdjson::dom::element value;
	if(doc.at_key(key).get(value) != simdjson::SUCCESS) { return std::nullopt; }

	std::string_view text;
	if(value.get_string().get(text) != simdjson::SUCCESS) { return std::nullopt; }
	return text;
}

std::optional<std::int64_t> intField(simdjson::dom::element doc, std::string_view key)
{
	simdjson::dom::element value;
	if(doc.at_key(key).get(value) != simdjson::SUCCESS) { return std::nullopt; }

	std::int64_t number = 0;
	if(value.get_int64().get(number) != simdjson::SUCCESS) { return std::nullopt; }
	return number;
}

/// 解析一行 `l0.jsonl`；`seq` 由目标账本重新分配，因此忽略。
std::expected<ledger::L0Entry, std::error_code> parseL0(std::string_view line)
{
	simdjson::dom::parser parser;
	simdjson::dom::element doc;
	if(parser.parse(simdjson::padded_string{line}).get(doc) != simdjson::SUCCESS)
	{
		return std::unexpected(makeError(TransferErrc::format_invalid));
	}

	const auto recordId = stringField(doc, "record_id");
	const auto personaId = stringField(doc, "persona_id");
	const auto content = stringField(doc, "content");
	if(!recordId || !personaId || !content)
	{
		return std::unexpected(makeError(TransferErrc::field_missing));
	}

	ledger::L0Entry entry;
	entry.recordId = std::string{*recordId};
	entry.personaId = std::string{*personaId};
	entry.content = std::string{*content};
	entry.sessionId = std::string{stringField(doc, "session_id").value_or("")};
	entry.role = std::string{stringField(doc, "role").value_or("")};
	entry.occurredAt = intField(doc, "occurred_at").value_or(0);
	entry.recordedAt = intField(doc, "recorded_at").value_or(0);
	entry.source = std::string{stringField(doc, "source").value_or("import")};
	return entry;
}

/// 解析一行 `l1.jsonl`；`version` 由目标版本链重新分配，`seq` 忽略。
std::expected<ledger::L1Version, std::error_code> parseL1(std::string_view line)
{
	simdjson::dom::parser parser;
	simdjson::dom::element doc;
	if(parser.parse(simdjson::padded_string{line}).get(doc) != simdjson::SUCCESS)
	{
		return std::unexpected(makeError(TransferErrc::format_invalid));
	}

	const auto recordId = stringField(doc, "record_id");
	const auto personaId = stringField(doc, "persona_id");
	const auto content = stringField(doc, "content");
	if(!recordId || !personaId || !content)
	{
		return std::unexpected(makeError(TransferErrc::field_missing));
	}

	ledger::L1Version version;
	version.recordId = std::string{*recordId};
	version.personaId = std::string{*personaId};
	version.content = std::string{*content};
	version.version = static_cast<int>(intField(doc, "version").value_or(0));
	version.type = std::string{stringField(doc, "type").value_or("episodic")};
	version.priority = static_cast<int>(intField(doc, "priority").value_or(0));
	version.family = std::string{stringField(doc, "family").value_or("")};
	version.sceneName = std::string{stringField(doc, "scene_name").value_or("")};
	version.timestampsJson = std::string{stringField(doc, "timestamps_json").value_or("{}")};
	version.sourceMessageIdsJson = std::string{stringField(doc, "source_message_ids_json").value_or("[]")};
	version.metadataJson = std::string{stringField(doc, "metadata_json").value_or("{}")};
	version.op = std::string{stringField(doc, "op").value_or("add")};
	version.createdAt = intField(doc, "created_at").value_or(0);
	version.updatedAt = intField(doc, "updated_at").value_or(0);
	return version;
}

/// 按行读 JSONL；文件不存在视为空。
std::expected<std::vector<std::string>, std::error_code> readLines(const std::filesystem::path& path)
{
	std::vector<std::string> lines;

	std::error_code ec;
	if(!std::filesystem::exists(path, ec)) { return lines; }

	std::ifstream in(path, std::ios::binary);
	if(!in) { return std::unexpected(makeError(TransferErrc::internal)); }

	std::string line;
	while(std::getline(in, line))
	{
		if(!line.empty() && line.back() == '\r') { line.pop_back(); }
		if(line.empty()) { continue; }
		lines.push_back(std::move(line));
	}
	return lines;
}

bool matches(const ExportFilter& filter, const ledger::L0Entry& entry)
{
	if(filter.afterSeq > 0 && entry.seq <= filter.afterSeq) { return false; }
	if(filter.personaId && entry.personaId != *filter.personaId) { return false; }
	return true;
}

bool matches(const ExportFilter& filter, const ledger::L1Version& version)
{
	if(filter.afterSeq > 0 && version.seq <= filter.afterSeq) { return false; }
	if(filter.personaId && version.personaId != *filter.personaId) { return false; }
	if(filter.family && version.family != *filter.family) { return false; }
	if(filter.sceneName && version.sceneName != *filter.sceneName) { return false; }
	return true;
}

} // namespace

const std::error_category& transferCategory() noexcept
{
	static const TransferCategory instance;
	return instance;
}

std::error_code makeError(TransferErrc code) noexcept
{
	return {static_cast<int>(code), transferCategory()};
}

std::string toJson(const ledger::L0Entry& entry)
{
	string_builder sb(512);

	sb.start_object();
	sb.append_key_value<"seq">(entry.seq);
	sb.append_comma();
	sb.append_key_value<"record_id">(entry.recordId);
	sb.append_comma();
	sb.append_key_value<"persona_id">(entry.personaId);
	sb.append_comma();
	sb.append_key_value<"session_id">(entry.sessionId);
	sb.append_comma();
	sb.append_key_value<"role">(entry.role);
	sb.append_comma();
	sb.append_key_value<"content">(entry.content);
	sb.append_comma();
	sb.append_key_value<"occurred_at">(entry.occurredAt);
	sb.append_comma();
	sb.append_key_value<"recorded_at">(entry.recordedAt);
	sb.append_comma();
	sb.append_key_value<"source">(entry.source);
	sb.end_object();

	return finish(sb);
}

std::string toJson(const ledger::L1Version& version)
{
	string_builder sb(1024);

	sb.start_object();
	sb.append_key_value<"seq">(version.seq);
	sb.append_comma();
	sb.append_key_value<"record_id">(version.recordId);
	sb.append_comma();
	sb.append_key_value<"persona_id">(version.personaId);
	sb.append_comma();
	sb.append_key_value<"version">(version.version);
	sb.append_comma();
	sb.append_key_value<"content">(version.content);
	sb.append_comma();
	sb.append_key_value<"type">(version.type);
	sb.append_comma();
	sb.append_key_value<"priority">(version.priority);
	sb.append_comma();
	sb.append_key_value<"family">(version.family);
	sb.append_comma();
	sb.append_key_value<"scene_name">(version.sceneName);
	sb.append_comma();
	sb.append_key_value<"timestamps_json">(version.timestampsJson);
	sb.append_comma();
	sb.append_key_value<"source_message_ids_json">(version.sourceMessageIdsJson);
	sb.append_comma();
	sb.append_key_value<"metadata_json">(version.metadataJson);
	sb.append_comma();
	sb.append_key_value<"op">(version.op);
	sb.append_comma();
	sb.append_key_value<"created_at">(version.createdAt);
	sb.append_comma();
	sb.append_key_value<"updated_at">(version.updatedAt);
	sb.end_object();

	return finish(sb);
}

std::expected<ExportStats, std::error_code> exportJsonl(
	const ledger::Store& store,
	std::string_view outDir,
	const ExportFilter& filter
)
{
	ExportStats stats;

	const std::filesystem::path dir{outDir};
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	if(ec) { return std::unexpected(makeError(TransferErrc::internal)); }

	stats.l0Path = (dir / "l0.jsonl").string();
	stats.l1Path = (dir / "l1.jsonl").string();

	{
		auto entries = store.replayL0();
		if(!entries) { return std::unexpected(entries.error()); }

		std::ofstream out(stats.l0Path, std::ios::binary | std::ios::trunc);
		if(!out) { return std::unexpected(makeError(TransferErrc::internal)); }

		for(const auto& entry : *entries)
		{
			if(!matches(filter, entry)) { continue; }
			out << toJson(entry) << '\n';
			++stats.l0Rows;
		}
		if(!out) { return std::unexpected(makeError(TransferErrc::internal)); }
	}

	{
		auto versions = store.replayL1();
		if(!versions) { return std::unexpected(versions.error()); }

		std::ofstream out(stats.l1Path, std::ios::binary | std::ios::trunc);
		if(!out) { return std::unexpected(makeError(TransferErrc::internal)); }

		for(const auto& version : *versions)
		{
			if(!matches(filter, version)) { continue; }
			out << toJson(version) << '\n';
			++stats.l1Rows;
		}
		if(!out) { return std::unexpected(makeError(TransferErrc::internal)); }
	}

	return stats;
}

std::expected<ImportStats, std::error_code> importJsonl(
	std::string_view srcDir,
	ledger::Store& store,
	ImportMode mode
)
{
	const std::filesystem::path dir{srcDir};

	if(mode == ImportMode::emptyOnly)
	{
		auto existingL0 = store.replayL0();
		if(!existingL0) { return std::unexpected(existingL0.error()); }
		auto existingL1 = store.replayL1();
		if(!existingL1) { return std::unexpected(existingL1.error()); }

		if(!existingL0->empty() || !existingL1->empty())
		{
			return std::unexpected(makeError(TransferErrc::target_not_empty));
		}
	}

	ImportStats stats;

	auto l0Lines = readLines(dir / "l0.jsonl");
	if(!l0Lines) { return std::unexpected(l0Lines.error()); }

	for(const auto& line : *l0Lines)
	{
		auto entry = parseL0(line);
		if(!entry) { return std::unexpected(entry.error()); }

		auto appended = store.appendL0(*entry);
		if(!appended) { return std::unexpected(appended.error()); }
		if(*appended) { ++stats.l0Rows; }
		else { ++stats.l0Skipped; }
	}

	auto l1Lines = readLines(dir / "l1.jsonl");
	if(!l1Lines) { return std::unexpected(l1Lines.error()); }

	std::map<std::pair<std::string, std::string>, std::vector<ledger::L1Version>> groups;
	for(const auto& line : *l1Lines)
	{
		auto version = parseL1(line);
		if(!version) { return std::unexpected(version.error()); }
		groups[{version->personaId, version->recordId}].push_back(std::move(*version));
	}

	for(auto& [key, versions] : groups)
	{
		std::sort(
			versions.begin(),
			versions.end(),
			[](const ledger::L1Version& left, const ledger::L1Version& right) { return left.version < right.version; }
		);

		auto existing = store.versionsOf(key.first, key.second);
		if(!existing) { return std::unexpected(existing.error()); }

		const std::size_t already = existing->size();
		stats.l1Skipped += static_cast<std::int64_t>(std::min(already, versions.size()));

		for(std::size_t i = already; i < versions.size(); ++i)
		{
			ledger::L1Version next = versions[i];
			next.version = static_cast<int>(i);
			next.seq = 0;

			auto appended = store.appendL1Version(next);
			if(!appended) { return std::unexpected(appended.error()); }
			++stats.l1Rows;
		}
	}

	return stats;
}

} // namespace herodotus::clio::transfer
