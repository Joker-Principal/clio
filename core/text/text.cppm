module;

#include <cppjieba/Jieba.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module herodotus.clio.text;

export namespace herodotus::clio::text {

/// 分词并集：jieba 词 ∪ 拉丁词 ∪ CJK 二元组（有序去重）。
struct Tokenized
{
	std::vector<std::string> words;
	std::vector<std::string> latin;
	std::vector<std::string> bigrams;
	std::vector<std::string> terms; ///< 有序去重并集（写入 FTS 与构造查询的唯一口径）
};

/// 中文分词器。进程内应共用同一实例，保证读写分词口径一致（防运行中漂移）。
class Tokenizer
{
public:
	explicit Tokenizer(std::string dictDir);
	~Tokenizer() = default;

	Tokenizer(const Tokenizer&) = delete;
	Tokenizer& operator=(const Tokenizer&) = delete;

	[[nodiscard]] Tokenized tokenize(std::string_view text) const;

	/// FTS `content` 列：并集以空格连接，供 unicode61 按下标切分。
	[[nodiscard]] std::string indexText(std::string_view text) const;

	/// FTS `MATCH` 表达式：每个词项双引号包裹、`OR` 连接；空文本返回空串。
	[[nodiscard]] std::string queryExpr(std::string_view text) const;

private:
	/// 首次分词时才加载词典（jieba 词典约 5 MB，构造开销远大于分词本身）。
	/// 非线程安全：首个调用需单线程，之后只读。
	[[nodiscard]] const cppjieba::Jieba& jieba() const;

	std::string dictDir_;
	mutable std::optional<cppjieba::Jieba> jieba_;
};

} // namespace herodotus::clio::text
