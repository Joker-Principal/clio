#include <cstdint>
#include <filesystem>
#include <print>
#include <stdexcept>
#include <string>

import herodotus.clio.ledger;

namespace {

int failures = 0;

void check(bool ok, std::string_view what)
{
	if(ok) { return; }
	++failures;
	std::println(stderr, "FAIL: {}", what);
}

} // namespace

int main()
{
	using namespace herodotus::clio::ledger;

	const auto root = std::filesystem::temp_directory_path() / "clio-ledger-tests";
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
	std::filesystem::create_directories(root, ec);

	const auto path = (root / "ledger.db").string();
	Store store{path};

	// L0 幂等
	L0Entry m;
	m.recordId = "m-1";
	m.personaId = "default";
	m.sessionId = "s-1";
	m.role = "user";
	m.content = "多媒体后置到 P4";
	m.occurredAt = 1767225600000;
	m.recordedAt = 1767225601000;
	m.source = "dsh";
	const auto firstL0 = store.appendL0(m);
	check(firstL0 && *firstL0, "appendL0: 首次写入");
	const auto secondL0 = store.appendL0(m);
	check(secondL0 && !*secondL0, "appendL0: 同 id 幂等");

	// L1 版本连续
	L1Version v0;
	v0.recordId = "r-1";
	v0.personaId = "default";
	v0.version = 0;
	v0.content = "用户决定多媒体后置到 P4";
	v0.type = "episodic";
	v0.priority = 80;
	v0.family = "work";
	v0.op = "add";
	check(store.appendL1Version(v0).has_value(), "appendL1Version: v0 写入");

	L1Version v1 = v0;
	v1.version = 1;
	v1.content = "用户决定多媒体后置到 P4，优先完成人格系统";
	v1.op = "edit";
	check(store.appendL1Version(v1).has_value(), "appendL1Version: v1 写入");

	L1Version bad = v0;
	bad.version = 3;
	const auto badResult = store.appendL1Version(bad);
	check(!badResult.has_value(), "appendL1Version: 跳版本被拒");
	check(
		badResult.error() == makeError(LedgerErrc::version_not_continuous),
		"appendL1Version: 错误码为 version_not_continuous"
	);

	const auto versions = store.versionsOf("default", "r-1");
	check(versions && versions->size() == 2, "versionsOf: 两个版本");
	check(versions && versions->front().version == 0 && versions->back().version == 1, "versionsOf: 版本升序");

	const auto latest = store.latestOf("default", "r-1");
	check(latest && latest->has_value() && (*latest)->version == 1, "latestOf: 最新版本");

	const auto missing = store.latestOf("default", "missing");
	check(missing && !missing->has_value(), "latestOf: 不存在返回空");

	const auto replay = store.replayL1();
	check(replay && replay->size() == 2, "replayL1: 全量");
	check(replay && replay->front().version == 0, "replayL1: 按 seq 顺序");

	const auto report = store.verify();
	check(report.ok, "verify: 通过");
	check(report.l0Rows == 1 && report.l1Rows == 2, "verify: 行数");

	std::filesystem::remove_all(root, ec);
	if(failures == 0) { std::println("ledger_tests OK"); }
	return failures == 0 ? 0 : 1;
}
