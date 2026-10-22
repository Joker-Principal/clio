# 开发与验收用的命令行工具（不属于引擎库）

# 分词 spike：只借 cppjieba 头文件，不链引擎库
add_executable(tokenize_spike tokenize_spike.cpp)
target_link_libraries(tokenize_spike PRIVATE argparse::argparse)
target_include_directories(tokenize_spike SYSTEM PRIVATE "${CPPJIEBA_INCLUDE_DIR}")
target_compile_definitions(tokenize_spike PRIVATE HERODOTUS_VERSION="${PROJECT_VERSION}")

# 账本 / 索引维护（P1 验收用）
add_executable(store_cli store_cli.cpp)
target_link_libraries(store_cli PRIVATE herodotus argparse::argparse)
