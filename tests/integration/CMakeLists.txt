# SPDX-License-Identifier: GPL-3.0-only
# Tests that run real components together over real sockets on localhost.
qt_add_executable(tst_tls_session tst_tls_session.cpp)
target_link_libraries(tst_tls_session PRIVATE
    Qt6::Test OpenTalk::Client OpenTalk::Server OpenTalk::Security OpenTalk::CompilerOptions)
target_compile_definitions(tst_tls_session PRIVATE
    OPENTALK_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
add_test(NAME tst_tls_session COMMAND tst_tls_session)
set_tests_properties(tst_tls_session PROPERTIES TIMEOUT 120)

qt_add_executable(tst_server_process tst_server_process.cpp)
target_link_libraries(tst_server_process PRIVATE Qt6::Test OpenTalk::CompilerOptions)
target_compile_definitions(tst_server_process PRIVATE
    OPENTALK_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data"
    OPENTALK_SERVER_EXECUTABLE="$<TARGET_FILE:opentalk_server_app>"
    OPENTALK_PING_EXECUTABLE="$<TARGET_FILE:opentalk_ping>")
add_dependencies(tst_server_process opentalk_server_app opentalk_ping)
add_test(NAME tst_server_process COMMAND tst_server_process)
set_tests_properties(tst_server_process PROPERTIES TIMEOUT 120)
