module;

#include <asio.hpp>

#include <simdjson.h>

#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <utility>

module herodotus.clio.serve;

import pylai;
import herodotus.clio.version;

namespace herodotus::clio::serve {
namespace {

using namespace pylai::route;

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

/// 路由表：成员函数指针就是声明，动词与 pattern 由第一个形参给出（编译期校验）。
struct Routes
{
	asio::awaitable<void> health(Get<"/health">, StringResponse& res)
	{
		co_await res.content_type("application/json").body(healthBody()).flush();
	}
};

} // namespace

struct HttpServer::Impl
{
	explicit Impl(ServerOptions opts) :
		options(std::move(opts)),
		server(pylai::Server::builder()
			.bind(options.host, static_cast<std::uint16_t>(options.port))
			.route_on(&routes, &Routes::health)
			.build()
		)
	{}

	ServerOptions options;
	Routes routes; // 必须声明在 server 之前：builder 存的是 &routes
	pylai::Server server;
	bool started{false};
};

HttpServer::HttpServer(ServerOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}

HttpServer::~HttpServer() = default;

void HttpServer::start()
{
	// pylai 的 `run()` 只能跑一次（io_context 不可重入）⇒ 重复调用直接返回。
	if(std::exchange(impl_->started, true)) { return; }
	impl_->server.run();
}

int HttpServer::port() const noexcept { return impl_->options.port; }

const ServerOptions& HttpServer::options() const noexcept { return impl_->options; }

} // namespace herodotus::clio::serve
