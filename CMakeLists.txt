cmake_minimum_required(VERSION 3.28)

project(herodotus
    VERSION 0.1.0
    DESCRIPTION "Herodotus 引擎：账本 / 索引 / 记忆 / 知识 / 蒸馏 / 召回 / 人格 / 治理"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 单配置生成器兜底；多配置（Ninja Multi-Config）用 CMAKE_DEFAULT_BUILD_TYPE
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "构建类型（单配置生成器）" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(deps)

add_library(herodotus STATIC)
file(GLOB_RECURSE HERODOTUS_MODULES CONFIGURE_DEPENDS "core/*.cppm")
file(GLOB_RECURSE HERODOTUS_SOURCES CONFIGURE_DEPENDS "core/*.cpp")
target_sources(herodotus
    PUBLIC
        FILE_SET CXX_MODULES 
        FILES ${HERODOTUS_MODULES}
    PRIVATE
        ${HERODOTUS_SOURCES}
)
add_library(herodotus::herodotus ALIAS herodotus)
target_compile_definitions(herodotus PRIVATE HERODOTUS_VERSION="${PROJECT_VERSION}")
target_link_libraries(herodotus
    PUBLIC 
        SQLite::SQLite3
        simdjson::simdjson
    PRIVATE
        pylai::pylai
)
target_include_directories(herodotus SYSTEM PUBLIC "${CPPJIEBA_INCLUDE_DIR}")
target_include_directories(herodotus PUBLIC "${PROJECT_SOURCE_DIR}/core")

add_executable(clio daemon/main.cpp)
target_link_libraries(clio PRIVATE herodotus)
