module;

#include <asio.hpp>

#include <simdjson.h>

#include <cstdint>
#include <format>
#include <string>
#include <utility>

module herodotus.clio.serve;

import pylai;
import herodotus.clio.version;

namespace herodotus::clio::serve {
namespace {

/// P0 的 /health 体：字段固定、无需转义 ⇒ 直接格式化。
/// P4 引入正式序列化后替换（账本 / 索引的写出口届时统一）。
std::string healthBody()
{
	return std::format(
		R"({{"status":"ok","protocol":{},"version":"{}","http":"pylai",)"
		R"("simdjson":"{}","simdjsonImplementation":"{}"}})",
		kProtocolVersion,
		kVersion,
		SIMDJSON_VERSION,
		simdjson::get_active_implementation()->name()
	);
}

} // namespace

asio::awaitable<void> HttpServer::Routes::health(pylai::route::Get<"/health">, pylai::route::StringResponse& res)
{
	co_await res.content_type("application/json").body(healthBody()).flush();
}

HttpServer::HttpServer(ServerOptions options) :
	options_(std::move(options)),
	server_(pylai::Server::builder()
		.bind(options_.host, static_cast<std::uint16_t>(options_.port))
		.route_on(&routes_, &Routes::health)
		.build()
	)
{}

void HttpServer::start()
{
	// pylai 的 `run()` 只能跑一次（io_context 不可重入）⇒ 重复调用直接返回。
	if(std::exchange(started_, true)) { return; }
	server_.run();
}

int HttpServer::port() const noexcept { return options_.port; }

const ServerOptions& HttpServer::options() const noexcept { return options_; }

} // namespace herodotus::clio::serve
