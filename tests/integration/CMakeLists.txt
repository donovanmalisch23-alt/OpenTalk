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

qt_add_executable(tst_chat_session tst_chat_session.cpp)
target_link_libraries(tst_chat_session PRIVATE
    Qt6::Test OpenTalk::Client OpenTalk::Server OpenTalk::Security OpenTalk::CompilerOptions)
target_compile_definitions(tst_chat_session PRIVATE
    OPENTALK_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
add_test(NAME tst_chat_session COMMAND tst_chat_session)
set_tests_properties(tst_chat_session PROPERTIES TIMEOUT 180)

qt_add_executable(tst_mainwindow_online tst_mainwindow_online.cpp)
target_link_libraries(tst_mainwindow_online PRIVATE
    Qt6::Test OpenTalk::Ui OpenTalk::Server OpenTalk::Security OpenTalk::CompilerOptions)
target_compile_definitions(tst_mainwindow_online PRIVATE
    OPENTALK_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data"
    OPENTALK_SERVER_EXECUTABLE="$<TARGET_FILE:opentalk_server_app>")
add_dependencies(tst_mainwindow_online opentalk_server_app)
add_test(NAME tst_mainwindow_online COMMAND tst_mainwindow_online)
set_tests_properties(tst_mainwindow_online PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_local_server tst_local_server.cpp)
target_link_libraries(tst_local_server PRIVATE Qt6::Test OpenTalk::Client OpenTalk::Security OpenTalk::CompilerOptions)
target_compile_definitions(tst_local_server PRIVATE
    OPENTALK_SERVER_EXECUTABLE="$<TARGET_FILE:opentalk_server_app>")
add_dependencies(tst_local_server opentalk_server_app)
add_test(NAME tst_local_server COMMAND tst_local_server)
set_tests_properties(tst_local_server PROPERTIES TIMEOUT 180)

qt_add_executable(tst_voice_session tst_voice_session.cpp)
target_link_libraries(tst_voice_session PRIVATE
    Qt6::Test OpenTalk::Client OpenTalk::Server OpenTalk::Security OpenTalk::Audio OpenTalk::CompilerOptions)
target_compile_definitions(tst_voice_session PRIVATE
    OPENTALK_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
add_test(NAME tst_voice_session COMMAND tst_voice_session)
set_tests_properties(tst_voice_session PROPERTIES TIMEOUT 180)
