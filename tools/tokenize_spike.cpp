// P0 分词 spike：验证 cppjieba 可用，并落地「jieba 词 ∪ 拉丁词 ∪ CJK 二元组」并集算法
// （ARCHITECTURE §2.7 的关键词腿口径）。输入查询集文件：每行 `query<TAB>intent1,intent2`。
// 用法：tokenize_spike --dict-dir <cppjieba/dict> --input <queries.txt> [--json]

#include <argparse/argparse.hpp>
#include <cppjieba/Jieba.hpp>

#include <fstream>
#include <iostream>
#include <print>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {
    
bool isCjk(char32_t c)
{
	return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF);
}
bool isLatin(char32_t c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
	       c == '.';
}

std::vector<char32_t> decodeUtf8(std::string_view s)
{
	std::vector<char32_t> out;
	for(std::size_t i = 0; i < s.size();)
	{
		const unsigned char b = static_cast<unsigned char>(s[i]);
		char32_t cp = 0;
		int len = 0;
		if((b & 0x80u) == 0) { cp = b; len = 1; }
		else if((b & 0xE0u) == 0xC0) { cp = b & 0x1Fu; len = 2; }
		else if((b & 0xF0u) == 0xE0) { cp = b & 0x0Fu; len = 3; }
		else if((b & 0xF8u) == 0xF0) { cp = b & 0x07u; len = 4; }
		else { ++i; continue; } // 非法字节跳过
		for(int k = 1; k < len && i + k < s.size(); ++k)
		{
			cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
		}
		out.push_back(cp);
		i += static_cast<std::size_t>(len);
	}
	return out;
}

std::string encodeUtf8(char32_t cp)
{
	std::string out;
	if(cp < 0x80) out.push_back(static_cast<char>(cp));
	else if(cp < 0x800)
	{
		out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
	else if(cp < 0x10000)
	{
		out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
	else
	{
		out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
	return out;
}

std::string slice(const std::vector<char32_t>& cps, std::size_t begin, std::size_t end)
{
	std::string out;
	for(std::size_t i = begin; i < end; ++i) { out += encodeUtf8(cps[i]); }
	return out;
}

bool meaningfulJiebaWord(std::string_view w)
{
	if(w.empty()) return false;
	const auto cps = decodeUtf8(w);
	if(cps.size() == 1)
	{
		const char32_t c = cps.front();
		return isCjk(c) || isLatin(c); // 单字符标点 / 空白不算词
	}
	for(const char32_t c : cps)
	{
		if(isCjk(c) || isLatin(c)) return true;
	}
	return false; // 全标点
}

struct Tokenized
{
	std::vector<std::string> jieba;
	std::vector<std::string> latin;
	std::vector<std::string> bigram;
	std::vector<std::string> uni;
};

Tokenized tokenize(cppjieba::Jieba& jieba, std::string_view text)
{
	Tokenized out;

	jieba.Cut(std::string{text}, out.jieba, false);
	for(auto it = out.jieba.begin(); it != out.jieba.end();)
	{
		if(meaningfulJiebaWord(*it)) { ++it; }
		else { it = out.jieba.erase(it); }
	}

	const auto cps = decodeUtf8(text);

	// 拉丁词：连续 latin 段，长度 ≥ 2（"P4"、"Qwen3"、"8B"；单字符无检索意义）
	for(std::size_t i = 0; i < cps.size();)
	{
		if(!isLatin(cps[i])) { ++i; continue; }
		std::size_t j = i;
		while(j < cps.size() && isLatin(cps[j])) { ++j; }
		if(j - i >= 2) { out.latin.push_back(slice(cps, i, j)); }
		i = j;
	}

	// CJK 二元组：连续 CJK 段内滑窗
	for(std::size_t i = 0; i < cps.size();)
	{
		if(!isCjk(cps[i])) { ++i; continue; }
		std::size_t j = i;
		while(j < cps.size() && isCjk(cps[j])) { ++j; }
		for(std::size_t k = i; k + 1 < j; ++k) { out.bigram.push_back(slice(cps, k, k + 2)); }
		i = j;
	}

	// 有序去重并集：jieba 词 → 拉丁词 → 二元组
	std::unordered_set<std::string> seen;
	for(const auto* list : {&out.jieba, &out.latin, &out.bigram})
	{
		for(const auto& w : *list)
		{
			if(seen.insert(w).second) { out.uni.push_back(w); }
		}
	}
	return out;
}

std::string esc(std::string_view s)
{
	std::string out;
	for(const char c : s)
	{
		if(c == '"' || c == '\\') { out.push_back('\\'); }
		out.push_back(c);
	}
	return out;
}

struct Query
{
	std::string text;
	std::vector<std::string> intents;
};

bool loadQueries(const std::string& path, std::vector<Query>& out)
{
	std::ifstream in(path);
	if(!in) return false;
	std::string line;
	while(std::getline(in, line))
	{
		if(line.empty() || line.front() == '#') continue;
		Query q;
		const auto tab = line.find('\t');
		q.text = tab == std::string::npos ? line : line.substr(0, tab);
		if(tab != std::string::npos)
		{
			std::string_view rest{line.data() + tab + 1, line.size() - tab - 1};
			while(!rest.empty())
			{
				const auto comma = rest.find(',');
				const auto word = rest.substr(0, comma);
				if(!word.empty()) { q.intents.emplace_back(word); }
				if(comma == std::string_view::npos) break;
				rest.remove_prefix(comma + 1);
			}
		}
		out.push_back(std::move(q));
	}
	return !out.empty();
}

std::string join(const std::vector<std::string>& words)
{
	std::string out;
	for(const auto& w : words)
	{
		if(!out.empty()) { out += ", "; }
		out += w;
	}
	return out;
}

void printReadable(const std::vector<Query>& queries, cppjieba::Jieba& jieba)
{
	for(const auto& q : queries)
	{
		std::print("# {}\n", q.text);
		if(!q.intents.empty()) { std::print("  意图词: {}\n", join(q.intents)); }
		const auto t = tokenize(jieba, q.text);
		std::print("  jieba : {}\n", join(t.jieba));
		std::print("  latin : {}\n", join(t.latin));
		std::print("  bigram: {}\n", join(t.bigram));
		std::print("  union : {}\n", join(t.uni));
	}
}

void printJson(const std::vector<Query>& queries, cppjieba::Jieba& jieba)
{
	std::string out = "[";
	for(const auto& q : queries)
	{
		const auto t = tokenize(jieba, q.text);
		if(out.size() > 1) { out += ","; }
		out += R"({"q":")" + esc(q.text) + R"(","intents":[)";
		for(std::size_t i = 0; i < q.intents.size(); ++i)
		{
			if(i) { out += ","; }
			out += "\"" + esc(q.intents[i]) + "\"";
		}
		out += R"(],"jieba":[)";
		for(std::size_t i = 0; i < t.jieba.size(); ++i)
		{
			if(i) { out += ","; }
			out += "\"" + esc(t.jieba[i]) + "\"";
		}
		out += R"(],"latin":[)";
		for(std::size_t i = 0; i < t.latin.size(); ++i)
		{
			if(i) { out += ","; }
			out += "\"" + esc(t.latin[i]) + "\"";
		}
		out += R"(],"bigram":[)";
		for(std::size_t i = 0; i < t.bigram.size(); ++i)
		{
			if(i) { out += ","; }
			out += "\"" + esc(t.bigram[i]) + "\"";
		}
		out += R"(],"union":[)";
		for(std::size_t i = 0; i < t.uni.size(); ++i)
		{
			if(i) { out += ","; }
			out += "\"" + esc(t.uni[i]) + "\"";
		}
		out += "]}";
	}
	out += "]\n";
	std::print("{}", out);
}

struct ConlluEval
{
	std::size_t sentences{0};
	std::size_t skipped{0};
	std::size_t goldBoundaries{0};
	std::size_t sysBoundaries{0};
	std::size_t matched{0};
};

std::size_t utf8Length(std::string_view s) { return decodeUtf8(s).size(); }

std::vector<std::string> splitTabs(std::string_view line)
{
	std::vector<std::string> out;
	std::size_t start = 0;
	while(true)
	{
		const auto tab = line.find('\t', start);
		out.emplace_back(line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
		if(tab == std::string_view::npos) break;
		start = tab + 1;
	}
	return out;
}

std::string stripSpaces(std::string_view s)
{
	std::string out;
	for(const char c : s)
	{
		if(c != ' ' && c != '\t' && c != '\r') out.push_back(c);
	}
	return out;
}

bool allDigits(std::string_view s)
{
	if(s.empty()) return false;
	for(const char c : s)
	{
		if(c < '0' || c > '9') return false;
	}
	return true;
}

ConlluEval evalConllu(cppjieba::Jieba& jieba, const std::string& path)
{
	ConlluEval r;
	std::ifstream in(path);
	std::string line;
	std::string text;
	std::vector<std::string> forms;

	const auto flush = [&] {
		if(text.empty() && forms.empty()) return;
		std::string joined;
		for(const auto& f : forms) { joined += f; }
		const std::string bare = stripSpaces(text);
		if(joined != bare || bare.empty())
		{
			++r.skipped;
			text.clear();
			forms.clear();
			return;
		}

		std::unordered_set<std::size_t> gold;
		std::size_t acc = 0;
		for(const auto& f : forms)
		{
			acc += utf8Length(f);
			gold.insert(acc);
		}
		gold.erase(acc); // 句末不算词边界

		std::vector<std::string> words;
		jieba.Cut(bare, words, false);
		std::unordered_set<std::size_t> sys;
		acc = 0;
		for(const auto& w : words)
		{
			acc += utf8Length(w);
			sys.insert(acc);
		}
		sys.erase(acc);

		++r.sentences;
		r.goldBoundaries += gold.size();
		r.sysBoundaries += sys.size();
		for(const auto p : sys)
		{
			if(gold.contains(p)) { ++r.matched; }
		}
		text.clear();
		forms.clear();
	};

	while(std::getline(in, line))
	{
		if(!line.empty() && line.back() == '\r') { line.pop_back(); }
		if(line.empty())
		{
			flush();
			continue;
		}
		if(line.rfind("# text = ", 0) == 0)
		{
			text = line.substr(9);
			continue;
		}
		if(line.front() == '#') continue;
		const auto cols = splitTabs(line);
		if(cols.size() >= 2 && allDigits(cols[0])) { forms.push_back(cols[1]); }
	}
	flush();
	return r;
}

void printEval(const ConlluEval& e, bool jsonMode)
{
	const double p = e.sysBoundaries ? static_cast<double>(e.matched) / static_cast<double>(e.sysBoundaries) : 0.0;
	const double rec =
		e.goldBoundaries ? static_cast<double>(e.matched) / static_cast<double>(e.goldBoundaries) : 0.0;
	const double f1 = (p + rec) > 0.0 ? 2.0 * p * rec / (p + rec) : 0.0;
	if(jsonMode)
	{
		std::println(
			R"({{"sentences":{},"skipped":{},"gold":{},"sys":{},"matched":{},"precision":{:.6f},"recall":{:.6f},"f1":{:.6f}}})",
			e.sentences,
			e.skipped,
			e.goldBoundaries,
			e.sysBoundaries,
			e.matched,
			p,
			rec,
			f1
		);
	}
	else
	{
		std::println("句子 {}（跳过 {}）", e.sentences, e.skipped);
		std::println("金标准边界 {} · 系统边界 {} · 命中 {}", e.goldBoundaries, e.sysBoundaries, e.matched);
		std::println("P={:.4f} R={:.4f} F1={:.4f}", p, rec, f1);
	}
}

} // namespace

int main(int argc, char** argv)
{
	argparse::ArgumentParser program("tokenize_spike", HERODOTUS_VERSION);
	program.add_argument("--dict-dir").required().help("cppjieba 词典目录（jieba.dict.utf8 等所在处）");
	program.add_argument("--input").help("查询集：每行 `query<TAB>intent1,intent2`；# 开头为注释");
	program.add_argument("--conllu").help("CoNLL-U 语料：按金标准分词边界算 P/R/F1");
	program.add_argument("--json").flag().help("输出 JSON（供对比脚本消费）；默认可读模式");

	try
	{
		program.parse_args(argc, argv);
	}
	catch(const std::exception& err)
	{
		std::println(stderr, "{}", err.what());
		std::cerr << program;
		return 2;
	}

	const bool hasInput = program.is_used("--input");
	const bool hasConllu = program.is_used("--conllu");
	if(hasInput == hasConllu)
	{
		std::println(stderr, "--input 与 --conllu 必须二选一");
		std::cerr << program;
		return 2;
	}

	const auto dictDir = program.get<std::string>("--dict-dir");
	const bool jsonMode = program.get<bool>("--json");
	cppjieba::Jieba jieba(
		dictDir + "/jieba.dict.utf8",
		dictDir + "/hmm_model.utf8",
		dictDir + "/user.dict.utf8",
		dictDir + "/idf.utf8",
		dictDir + "/stop_words.utf8"
	);

	if(hasConllu)
	{
		printEval(evalConllu(jieba, program.get<std::string>("--conllu")), jsonMode);
		return 0;
	}

	const auto inputPath = program.get<std::string>("--input");
	std::vector<Query> queries;
	if(!loadQueries(inputPath, queries))
	{
		std::println(stderr, "查询集为空或读取失败: {}", inputPath);
		return 1;
	}

	if(jsonMode) { printJson(queries, jieba); }
	else { printReadable(queries, jieba); }
	return 0;
}
