module;

#include <string_view>

export module herodotus.clio.version;

#ifndef HERODOTUS_VERSION
#	define HERODOTUS_VERSION "0.0.0"
#endif

export namespace herodotus::clio {

/// 产品名（整个系统）。
inline constexpr std::string_view kProductName{"Herodotus"};
/// 引擎库名。
inline constexpr std::string_view kEngineName{"herodotus"};
/// 守护进程名。
inline constexpr std::string_view kDaemonName{"clio"};

/// 与 docs/PROTOCOL.md §1.2 对应的协议版本号；破坏性变更必须递增。
inline constexpr int kProtocolVersion{1};

/// 引擎语义版本（由 CMake 从 project(VERSION) 注入）。
inline constexpr std::string_view kVersion{HERODOTUS_VERSION};

} // namespace herodotus::clio
