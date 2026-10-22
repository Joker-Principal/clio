module;

#include <cppjieba/Jieba.hpp>
#include <simdutf.h>

#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

module herodotus.clio.text;

namespace herodotus::clio::text {
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

/// 把一段已知合法的 UTF-8 追加为码点（整段交给 simdutf）。
void appendUtf8(std::vector<char32_t>& out, const char* data, std::size_t bytes)
{
	const std::size_t base = out.size();
	out.resize(base + bytes);
	const std::size_t written = simdutf::convert_utf8_to_utf32(data, bytes, out.data() + base);
	out.resize(base + written);
}

/// `pos` 处一个 ill-formed 子序列应有的字节数（Unicode「最大子部分」规则）。
std::size_t illFormedLength(std::string_view s, std::size_t pos) noexcept
{
	const auto lead = static_cast<unsigned char>(s[pos]);
	std::size_t expected = 1;
	if(lead >= 0xC2 && lead <= 0xDF) { expected = 2; }
	else if(lead >= 0xE0 && lead <= 0xEF) { expected = 3; }
	else if(lead >= 0xF0 && lead <= 0xF4) { expected = 4; }

	std::size_t length = 1;
	while(length < expected && pos + length < s.size())
	{
		const auto next = static_cast<unsigned char>(s[pos + length]);
		if(next < 0x80 || next > 0xBF) { break; }
		++length;
	}
	return length;
}

/// UTF-8 → 码点。合法前缀整段交给 simdutf；每个 ill-formed 子序列替换为一个 U+FFFD。
std::vector<char32_t> decodeUtf8(std::string_view s)
{
	std::vector<char32_t> out;
	out.reserve(s.size());

	std::size_t pos = 0;
	while(pos < s.size())
	{
		const auto result = simdutf::validate_utf8_with_errors(s.data() + pos, s.size() - pos);
		if(result.count > 0) { appendUtf8(out, s.data() + pos, result.count); }
		pos += result.count;

		if(result.error == simdutf::error_code::SUCCESS) { break; }

		// 非法区只做一次标量判断，不必逐字节重跑 SIMD 校验
		const std::size_t badBytes = illFormedLength(s, pos);
		out.push_back(0xFFFD);
		pos += badBytes;
	}

	return out;
}

std::string slice(const std::vector<char32_t>& cps, std::size_t begin, std::size_t end)
{
	if(end <= begin) { return {}; }

	std::string out;
	out.resize((end - begin) * 4);
	const std::size_t written =
		simdutf::convert_utf32_to_utf8(cps.data() + begin, end - begin, out.data());
	out.resize(written);
	return out;
}

bool meaningfulWord(std::string_view w)
{
	if(w.empty()) return false;
	const auto cps = decodeUtf8(w);
	if(cps.size() == 1)
	{
		const char32_t c = cps.front();
		return isCjk(c) || isLatin(c); // 单字符标点与空白不算词
	}
	for(const char32_t c : cps)
	{
		if(isCjk(c) || isLatin(c)) return true;
	}
	return false;
}

std::string quoteFtsTerm(std::string_view term)
{
	std::string out{"\""};
	for(const char c : term)
	{
		if(c == '"') { out += R"("")"; }
		else { out += c; }
	}
	out += '"';
	return out;
}

} // namespace

Tokenizer::Tokenizer(std::string dictDir) : dictDir_(std::move(dictDir)) {}

const cppjieba::Jieba& Tokenizer::jieba() const
{
	if(!jieba_)
	{
		jieba_.emplace(
			dictDir_ + "/jieba.dict.utf8",
			dictDir_ + "/hmm_model.utf8",
			dictDir_ + "/user.dict.utf8",
			dictDir_ + "/idf.utf8",
			dictDir_ + "/stop_words.utf8"
		);
	}
	return *jieba_;
}

Tokenized Tokenizer::tokenize(std::string_view text) const
{
	Tokenized out;

	std::vector<std::string> cut;
	jieba().Cut(std::string{text}, cut, false);
	for(auto& w : cut)
	{
		if(meaningfulWord(w)) { out.words.push_back(std::move(w)); }
	}

	const auto cps = decodeUtf8(text);

	for(std::size_t i = 0; i < cps.size();)
	{
		if(!isLatin(cps[i]))
		{
			++i;
			continue;
		}
		std::size_t j = i;
		while(j < cps.size() && isLatin(cps[j])) { ++j; }
		if(j - i >= 2) { out.latin.push_back(slice(cps, i, j)); }
		i = j;
	}

	for(std::size_t i = 0; i < cps.size();)
	{
		if(!isCjk(cps[i]))
		{
			++i;
			continue;
		}
		std::size_t j = i;
		while(j < cps.size() && isCjk(cps[j])) { ++j; }
		for(std::size_t k = i; k + 1 < j; ++k) { out.bigrams.push_back(slice(cps, k, k + 2)); }
		i = j;
	}

	std::unordered_set<std::string> seen;
	for(const auto* list : {&out.words, &out.latin, &out.bigrams})
	{
		for(const auto& term : *list)
		{
			if(seen.insert(term).second) { out.terms.push_back(term); }
		}
	}
	return out;
}

std::string Tokenizer::indexText(std::string_view text) const
{
	std::string out;
	for(const auto& term : tokenize(text).terms)
	{
		if(!out.empty()) { out += ' '; }
		out += term;
	}
	return out;
}

std::string Tokenizer::queryExpr(std::string_view text) const
{
	std::string out;
	for(const auto& term : tokenize(text).terms)
	{
		if(!out.empty()) { out += " OR "; }
		out += quoteFtsTerm(term);
	}
	return out;
}

} // namespace herodotus::clio::text
