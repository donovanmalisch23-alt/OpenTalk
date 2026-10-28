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
    Qt6::Test OpenTalk::Ui OpenTalk::Server OpenTalk::Security OpenTalk::Audio OpenTalk::CompilerOptions)
target_compile_definitions(tst_mainwindow_online PRIVATE
    OPENTALK_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data"
    OPENTALK_SERVER_EXECUTABLE="$<TARGET_FILE:opentalk_server_app>")
add_dependencies(tst_mainwindow_online opentalk_server_app)
add_test(NAME tst_mainwindow_online COMMAND tst_mainwindow_online)
set_tests_properties(tst_mainwindow_online PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_moderation_ui tst_moderation_ui.cpp)
target_link_libraries(tst_moderation_ui PRIVATE
    Qt6::Test OpenTalk::Ui OpenTalk::Server OpenTalk::Security OpenTalk::Audio OpenTalk::CompilerOptions)
target_compile_definitions(tst_moderation_ui PRIVATE
    OPENTALK_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
add_test(NAME tst_moderation_ui COMMAND tst_moderation_ui)
set_tests_properties(tst_moderation_ui PROPERTIES TIMEOUT 300 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_plugins tst_plugins.cpp)
target_link_libraries(tst_plugins PRIVATE
    Qt6::Test OpenTalk::Plugins OpenTalk::Server OpenTalk::Security OpenTalk::CompilerOptions)
target_compile_definitions(tst_plugins PRIVATE
    OPENTALK_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data"
    OPENTALK_TEST_PLUGIN="$<TARGET_FILE:opentalk_test_plugin>"
    OPENTALK_PYTHON_EXAMPLE="${PROJECT_SOURCE_DIR}/examples/plugins/hello-python"
    OPENTALK_DICE_EXAMPLE="$<TARGET_FILE_DIR:opentalk_dice_plugin>")
add_dependencies(tst_plugins opentalk_test_plugin opentalk_dice_plugin)
add_test(NAME tst_plugins COMMAND tst_plugins)
set_tests_properties(tst_plugins PROPERTIES TIMEOUT 180)

qt_add_executable(tst_plugin_ui tst_plugin_ui.cpp)
target_link_libraries(tst_plugin_ui PRIVATE Qt6::Test OpenTalk::Ui OpenTalk::Audio OpenTalk::CompilerOptions)
target_compile_definitions(tst_plugin_ui PRIVATE OPENTALK_TEST_PLUGIN="$<TARGET_FILE:opentalk_test_plugin>")
add_dependencies(tst_plugin_ui opentalk_test_plugin)
add_test(NAME tst_plugin_ui COMMAND tst_plugin_ui)
set_tests_properties(tst_plugin_ui PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_examples tst_examples.cpp)
target_link_libraries(tst_examples PRIVATE
    Qt6::Test OpenTalk::Client OpenTalk::Server OpenTalk::Security OpenTalk::CompilerOptions)
target_compile_definitions(tst_examples PRIVATE
    OPENTALK_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data"
    OPENTALK_GREETER_BOT="$<TARGET_FILE:opentalk_greeter_bot>")
add_dependencies(tst_examples opentalk_greeter_bot)
add_test(NAME tst_examples COMMAND tst_examples)
set_tests_properties(tst_examples PROPERTIES TIMEOUT 120)

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
