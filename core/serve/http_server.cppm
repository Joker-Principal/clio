module;

#include <asio.hpp>

#include <string>

export module herodotus.clio.serve;

import pylai;

export namespace herodotus::clio::serve {

struct ServerOptions
{
	/// 期望的绑定地址（pylai 的 `bind()` 直接接受它，不变量 2 由此成立）。
	std::string host{"127.0.0.1"};
	int port{7788};
	std::string dataDir{"data"};
};

/// 最小 HTTP 服务面。P0 只提供 `GET /health` 以验证工具链；
/// 完整端点（/ingest、/recall …）在 P4 落地。
class HttpServer
{
public:
	explicit HttpServer(ServerOptions options);
	~HttpServer() = default;

	HttpServer(const HttpServer&) = delete;
	HttpServer& operator=(const HttpServer&) = delete;
	HttpServer(HttpServer&&) = delete;
	HttpServer& operator=(HttpServer&&) = delete;

	/// 启动监听并**阻塞**当前线程，直到进程收到 SIGINT（Windows 只有它）
	/// 或 SIGTERM（仅 POSIX）。信号处理在 pylai 的 `Server::run()` 内部完成。
	/// NOTE：pylai 的 `quit()` 只能在 Server 所属线程调 ⇒ 不对外暴露。
	void start();

	/// 请求的端口（**不是**实际绑定端口：pylai 的 `Server` 暂不对外暴露后者）。
	[[nodiscard]] int port() const noexcept;
	[[nodiscard]] const ServerOptions& options() const noexcept;

private:
	struct Routes
	{
		asio::awaitable<void> health(pylai::route::Get<"/health">, pylai::route::StringResponse& res);
	};

	ServerOptions options_;
	Routes routes_; ///< 必须声明在 server_ 之前：builder 存的是 &routes_
	pylai::Server server_;
	bool started_{false};
};

} // namespace herodotus::clio::serve
