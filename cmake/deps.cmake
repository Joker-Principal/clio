# ---------------------------------------------------------------------------
# 第三方依赖
#
#   vcpkg 供货：unofficial-sqlite3（features: fts5）/ simdjson / hical（取代 cpp-httplib）
#   仅 cppjieba 走 FetchContent —— vcpkg 官方 registry 无该端口
#
# 需通过 CMAKE_TOOLCHAIN_FILE 接入 vcpkg（机器相关，见 CMakeUserPresets.json）。
# ---------------------------------------------------------------------------
include_guard(GLOBAL)

# --- SQLite（vcpkg）----------------------------------------------------------
find_package(unofficial-sqlite3 CONFIG REQUIRED)
# 统一成供货方无关的名字，其余 CMake 代码只认 SQLite::SQLite3
if(NOT TARGET SQLite::SQLite3)
    add_library(SQLite::SQLite3 ALIAS unofficial::sqlite3::sqlite3)
endif()

# --- simdjson（vcpkg）------------------------------------------------------
find_package(simdjson CONFIG REQUIRED)

# --- hical（vcpkg）—— HTTP 服务底座，取代 cpp-httplib ----------------------
find_package(hical CONFIG REQUIRED)

# --- cppjieba（FetchContent）-----------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------------------
# cppjieba —— 中文分词（`cppjieba 词 ∪ 拉丁词 ∪ CJK 二元组` 的关键词腿）
#
# 注意：cppjieba 自带的 CMakeLists 面向独立构建（find_package(limonp)），
# 不适合 add_subdirectory。故 SOURCE_SUBDIR 指向不含 CMakeLists.txt 的目录，
# 让 FetchContent 只下载（含 limonp 子模块）、不 add_subdirectory；
# 头文件路径在 CMakeLists.txt 中手工挂载。
# ---------------------------------------------------------------------------
FetchContent_Declare(cppjieba
    GIT_REPOSITORY "https://github.com/yanyiwu/cppjieba.git"
    GIT_TAG        "v5.6.7"
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "include")
FetchContent_MakeAvailable(cppjieba)
set(CPPJIEBA_INCLUDE_DIR "${cppjieba_SOURCE_DIR}/include" "${cppjieba_SOURCE_DIR}/deps/limonp/include")
