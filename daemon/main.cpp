#include <cstdio>
#include <cstdlib>
#include <print>
#include <string>
#include <string_view>
import herodotus.clio;

namespace {

struct Options
{
	std::string host{"127.0.0.1"};
	int port{7788};
	std::string dataDir{"data"};
};

constexpr std::string_view usage = R"(clio —— Herodotus 引擎守护进程
用法: clio [--host <addr>] [--port <n>] [--data-dir <path>]
  --host      监听地址（默认 127.0.0.1，只允许回环）
  --port      监听端口（默认 7788）
  --data-dir  数据目录（默认 ./data）
)";

bool parseArgs(int argc, char** argv, Options& out)
{
	for(int i = 1; i < argc; ++i)
	{
		const std::string arg{argv[i]};
		const auto take = [&](std::string& dst) {
			if(i + 1 >= argc)
			{
				std::println(stderr, "[clio] 参数 {} 缺少取值", arg);
				return false;
			}
			dst = argv[++i];
			return true;
		};

		if(arg == "--help" || arg == "-h")
		{
			std::println("{}", usage);
			return false;
		}
		if(arg == "--host")
		{
			if(!take(out.host)) return false;
		}
		else if(arg == "--port")
		{
			std::string value;
			if(!take(value)) return false;
			out.port = std::atoi(value.c_str());
		}
		else if(arg == "--data-dir")
		{
			if(!take(out.dataDir)) return false;
		}
		else
		{
			std::println(stderr, "[clio] 未知参数: {}", arg);
			std::println("{}", usage);
			return false;
		}
	}
	return true;
}

} // namespace

int main(int argc, char** argv)
{
	using namespace herodotus::clio;
	
	Options options;
	if(!parseArgs(argc, argv, options)) { return EXIT_FAILURE; }

	std::println(
		"[clio] Herodotus 引擎 v{} (protocol {})", kVersion, kProtocolVersion
	);

	const auto ledger = ledger::probe(options.dataDir);
	std::println("[clio] sqlite {} (FTS5 {})", ledger.sqliteVersion, ledger.fts5 ? "on" : "off");
	if(!ledger.ok)
	{
		std::println(stderr, "[clio] {}", ledger.error);
		return EXIT_FAILURE;
	}
	if(!ledger.fts5)
	{
		std::println(stderr, "[clio] 致命：SQLite 未启用 FTS5——中文检索的关键词腿无法工作");
		return EXIT_FAILURE;
	}
	std::println("[clio] 账本可用: {}", ledger.dbPath);

	serve::HttpServer server{
		serve::ServerOptions{.host = options.host, .port = options.port, .dataDir = options.dataDir}
	};

	std::println("[clio] 已监听 http://{}:{}（Ctrl+C 退出）", options.host, options.port);
	server.start();

	std::println("[clio] 已退出");
	return EXIT_SUCCESS;
}
