#include <algorithm>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

import herodotus.clio.text;

namespace {

int failures = 0;

void check(bool ok, std::string_view what)
{
	if(ok) { return; }
	++failures;
	std::println(stderr, "FAIL: {}", what);
}

bool has(const std::vector<std::string>& v, std::string_view s)
{
	return std::find(v.begin(), v.end(), s) != v.end();
}

bool uniqueTerms(const std::vector<std::string>& v)
{
	std::vector<std::string> copy = v;
	std::sort(copy.begin(), copy.end());
	return std::adjacent_find(copy.begin(), copy.end()) == copy.end();
}

} // namespace

int main(int argc, char** argv)
{
	using namespace herodotus::clio::text;

	if(argc < 2)
	{
		std::println(stderr, "usage: text_tests <cppjieba-dict-dir>");
		return 2;
	}

	Tokenizer tokenizer{argv[1]};

	// 拉丁词补丁：jieba 会把 8B / P4 / Qwen3 切碎，拉丁词必须整词捞回
	const auto r1 = tokenizer.tokenize("蒸馏模型用 8B 还是 14B，Qwen3 和 P4 都行");
	check(has(r1.latin, "8B"), "latin: 8B");
	check(has(r1.latin, "14B"), "latin: 14B");
	check(has(r1.latin, "Qwen3"), "latin: Qwen3");
	check(has(r1.latin, "P4"), "latin: P4");
	check(has(r1.terms, "8B") && has(r1.terms, "Qwen3"), "union: 拉丁词进入并集");

	// CJK 二元组是未登录词的召回底线
	const auto r2 = tokenizer.tokenize("温酒斩华雄");
	check(has(r2.bigrams, "酒斩") && has(r2.bigrams, "斩华") && has(r2.bigrams, "华雄"), "bigram: 温酒斩华雄");

	// 并集有序去重
	const auto r3 = tokenizer.tokenize("图片和视频那个事怎么定的？");
	check(uniqueTerms(r3.terms), "union: 无重复");
	check(has(r3.terms, "图片") && has(r3.terms, "视频"), "union: 常规词");

	// FTS 文本与查询表达式
	const auto idx = tokenizer.indexText("图片和视频");
	check(idx.find(' ') != std::string::npos, "indexText: 以空格连接");
	check(tokenizer.queryExpr("").empty(), "queryExpr: 空输入");
	const auto q = tokenizer.queryExpr("图片 视频");
	check(q.find(" OR ") != std::string::npos, "queryExpr: OR 连接");
	check(q.find('"') != std::string::npos, "queryExpr: 词项加引号");

	// 非法 UTF-8：坏字节替换为 U+FFFD，其后的正常内容不能被吞掉
	const std::string broken = std::string{"\xff\xfe温酒斩华雄"};
	const auto r4 = tokenizer.tokenize(broken);
	check(has(r4.terms, "华雄"), "非法字节: 后续内容仍可切词");
	check(!tokenizer.indexText(broken).empty(), "非法字节: 不抛异常且产出非空");

	if(failures == 0) { std::println("text_tests OK"); }
	return failures == 0 ? 0 : 1;
}
