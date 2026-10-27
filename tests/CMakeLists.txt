# 单元 / 集成测试；由父目录在 BUILD_TESTING 下 add_subdirectory

add_executable(text_tests text_tests.cpp)
target_link_libraries(text_tests PRIVATE herodotus)
add_test(NAME text_tests COMMAND text_tests "${cppjieba_SOURCE_DIR}/dict")

add_executable(ledger_tests ledger_tests.cpp)
target_link_libraries(ledger_tests PRIVATE herodotus)
add_test(NAME ledger_tests COMMAND ledger_tests)

add_executable(index_tests index_tests.cpp)
target_link_libraries(index_tests PRIVATE herodotus)
add_test(NAME index_tests COMMAND index_tests "${cppjieba_SOURCE_DIR}/dict")

add_executable(transfer_tests transfer_tests.cpp)
target_link_libraries(transfer_tests PRIVATE herodotus)
add_test(NAME transfer_tests COMMAND transfer_tests)
