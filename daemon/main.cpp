#include <cstdlib>
#include <iostream>
#include <print>
#include <string>

#include <argparse/argparse.hpp>

import herodotus.clio;

int main(int argc, char** argv)
{
	using namespace herodotus::clio;

	argparse::ArgumentParser program(std::string{kDaemonName}, std::string{kVersion});
	program.add_argument("--host")
		.default_value(std::string{"127.0.0.1"})
		.help("监听地址（只允许回环）");
	program.add_argument("--port")
		.scan<'i', int>()
		.default_value(7788)
		.help("监听端口");
	program.add_argument("--data-dir")
		.default_value(std::string{"data"})
		.help("数据目录（默认 ./data）");

	try
	{
		program.parse_args(argc, argv);
	}
	catch(const std::exception& err)
	{
		std::println(stderr, "[clio] {}", err.what());
		std::cerr << program;
		return EXIT_FAILURE;
	}

	const auto host = program.get<std::string>("--host");
	const auto port = program.get<int>("--port");
	const auto dataDir = program.get<std::string>("--data-dir");

	std::println("[clio] Herodotus 引擎 v{} (protocol {})", kVersion, kProtocolVersion);

	const auto ledger = ledger::probe(dataDir);
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
		serve::ServerOptions{.host = host, .port = port, .dataDir = dataDir}
	};

	std::println("[clio] 已监听 http://{}:{}（Ctrl+C 退出）", host, port);
	server.start();

	std::println("[clio] 已退出");
	return EXIT_SUCCESS;
}
