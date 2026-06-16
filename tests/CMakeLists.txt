function(tundraux_add_test_executable target_name)
    cmake_parse_arguments(
        TUNDRAUX_TEST
        ""
        ""
        "SOURCES;COMMAND"
        ${ARGN}
    )

    add_executable(${target_name}
        ${TUNDRAUX_TEST_SOURCES}
    )

    target_compile_features(${target_name}
        PRIVATE
            cxx_std_17
    )

    if(TUNDRAUX_TEST_COMMAND)
        add_test(NAME ${target_name} COMMAND ${TUNDRAUX_TEST_COMMAND})
    else()
        add_test(NAME ${target_name} COMMAND ${target_name})
    endif()
endfunction()

tundraux_add_test_executable(backend_core_tests
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/backend_core_tests.cpp
)

target_include_directories(backend_core_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/BACKEND/core
)

target_link_libraries(backend_core_tests
    PRIVATE
        tundraux_backend_core
)

tundraux_add_test_executable(backend_file_service_tests
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/backend_file_service_tests.cpp
)

target_link_libraries(backend_file_service_tests
    PRIVATE
        tundraux_backend_adapters
)

tundraux_add_test_executable(backend_audit_service_tests
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/backend_audit_service_tests.cpp
)

target_link_libraries(backend_audit_service_tests
    PRIVATE
        tundraux_backend_core
        tundraux_protocol
)

tundraux_add_test_executable(tux_service_tests
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/tux_service_tests.cpp
)

target_include_directories(tux_service_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/BACKEND/core
        ${PROJECT_SOURCE_DIR}/BACKEND/adapters
)

target_link_libraries(tux_service_tests
    PRIVATE
        tundraux_backend_core
        tundraux_backend_adapters
)

tundraux_add_test_executable(backend_json_rpc_tests
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/backend_json_rpc_tests.cpp
)

target_link_libraries(backend_json_rpc_tests
    PRIVATE
        tundraux_backend_core
)

tundraux_add_test_executable(backend_stdio_tests
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/backend_stdio_tests.cpp
    COMMAND
        backend_stdio_tests
        $<TARGET_FILE:tundraux_backend_stdio>
)

target_include_directories(backend_stdio_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/SYSTEM/crypto
)

target_link_libraries(backend_stdio_tests
    PRIVATE
        tundraux_backend_core
        tundraux_backend_adapters
)

add_dependencies(backend_stdio_tests tundraux_backend_stdio)

tundraux_add_test_executable(command_registry_tests
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/command_registry_tests.cpp
        ${PROJECT_SOURCE_DIR}/APP/shell/commandRegistry.cpp
        ${PROJECT_SOURCE_DIR}/APP/shell/commandReg.cpp
)

target_include_directories(command_registry_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/APP
        ${PROJECT_SOURCE_DIR}/APP/backend_client
        ${PROJECT_SOURCE_DIR}/APP/shell
        ${PROJECT_SOURCE_DIR}/TundraTUI/include/TundraTUI
        ${PROJECT_SOURCE_DIR}/SYSTEM/debug
        ${PROJECT_SOURCE_DIR}/USER/udata
)

target_link_libraries(command_registry_tests
    PRIVATE
        TundraTUI::TundraTUI
)

if(TUNDRAUX_BUILD_ALL_TESTS)
    tundraux_add_test_executable(protocol_json_smoke_test
        SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/protocol_json_smoke_test.cpp
    )

    target_link_libraries(protocol_json_smoke_test
        PRIVATE
            tundraux_protocol
    )

    tundraux_add_test_executable(backend_client_protocol_boundary_test
        SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/backend_client_protocol_boundary_test.cpp
    )

    target_include_directories(backend_client_protocol_boundary_test
        PRIVATE
            ${PROJECT_SOURCE_DIR}/BACKEND/core
            ${PROJECT_SOURCE_DIR}/SHARED/protocol
            ${PROJECT_SOURCE_DIR}/APP/backend_client
    )

    target_link_libraries(backend_client_protocol_boundary_test
        PRIVATE
            tundraux_protocol
    )

    tundraux_add_test_executable(frontend_backend_separation_tests
        SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/frontend_backend_separation_tests.cpp
        COMMAND
            frontend_backend_separation_tests
            ${PROJECT_SOURCE_DIR}
    )

    tundraux_add_test_executable(audit_log_tests
        SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/audit_log_tests.cpp
            ${PROJECT_SOURCE_DIR}/SYSTEM/audit/audit_log.cpp
            ${PROJECT_SOURCE_DIR}/SYSTEM/crypto/crypto.cpp
            ${PROJECT_SOURCE_DIR}/USER/udata/udata.cpp
    )

    target_include_directories(audit_log_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/APP/editor
            ${PROJECT_SOURCE_DIR}/SYSTEM/audit
            ${PROJECT_SOURCE_DIR}/SYSTEM/console
            ${PROJECT_SOURCE_DIR}/SYSTEM/crypto
            ${PROJECT_SOURCE_DIR}/USER/udata
    )

    target_link_libraries(audit_log_tests
        PRIVATE
            TundraTUI::TundraTUI
    )

    tundraux_add_test_executable(legacy_direct_tests
        SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/legacy_direct_tests.cpp
            ${PROJECT_SOURCE_DIR}/APP/legacy_direct/legacy_direct.cpp
            ${PROJECT_SOURCE_DIR}/USER/udata/udata.cpp
            ${PROJECT_SOURCE_DIR}/SYSTEM/crypto/crypto.cpp
    )

    target_include_directories(legacy_direct_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/APP/backend_client
            ${PROJECT_SOURCE_DIR}/APP/legacy_direct
            ${PROJECT_SOURCE_DIR}/BACKEND/core
            ${PROJECT_SOURCE_DIR}/SYSTEM/console
            ${PROJECT_SOURCE_DIR}/SYSTEM/crypto
            ${PROJECT_SOURCE_DIR}/USER/udata
    )

    target_link_libraries(legacy_direct_tests
        PRIVATE
            TundraTUI::TundraTUI
            tundraux_backend_core
    )

    tundraux_add_test_executable(build_info_tests
        SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/build_info_tests.cpp
            ${PROJECT_SOURCE_DIR}/SYSTEM/build_info/build_info.cpp
    )

    target_include_directories(build_info_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/SYSTEM/build_info
            ${TUNDRAUX_GENERATED_DIR}
    )

    add_dependencies(build_info_tests tundraux_generate_build_info)

    tundraux_add_test_executable(explorer_permissions_tests
        SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/explorer_permissions_tests.cpp
            ${PROJECT_SOURCE_DIR}/APP/backend_client/backend_client.cpp
            ${PROJECT_SOURCE_DIR}/APP/explorer/explorer_backend.cpp
            ${PROJECT_SOURCE_DIR}/APP/explorer/explorer_directory.cpp
            ${PROJECT_SOURCE_DIR}/APP/explorer/explorer_permissions.cpp
            ${PROJECT_SOURCE_DIR}/APP/explorer/explorer_text.cpp
    )

    target_include_directories(explorer_permissions_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/APP/backend_client
            ${PROJECT_SOURCE_DIR}/APP/editor
            ${PROJECT_SOURCE_DIR}/APP/explorer
            ${PROJECT_SOURCE_DIR}/BACKEND/core
    )

    target_link_libraries(explorer_permissions_tests
        PRIVATE
            TundraTUI::TundraTUI
            tundraux_backend_core
    )

    tundraux_add_test_executable(explorer_clipboard_tests
        SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/explorer_clipboard_tests.cpp
            ${PROJECT_SOURCE_DIR}/APP/backend_client/backend_client.cpp
            ${PROJECT_SOURCE_DIR}/APP/explorer/explorer_backend.cpp
            ${PROJECT_SOURCE_DIR}/APP/explorer/explorer_clipboard.cpp
            ${PROJECT_SOURCE_DIR}/APP/explorer/explorer_directory.cpp
            ${PROJECT_SOURCE_DIR}/APP/explorer/explorer_navigation.cpp
            ${PROJECT_SOURCE_DIR}/APP/explorer/explorer_permissions.cpp
            ${PROJECT_SOURCE_DIR}/APP/explorer/explorer_text.cpp
            ${PROJECT_SOURCE_DIR}/SYSTEM/audit/audit_log.cpp
            ${PROJECT_SOURCE_DIR}/SYSTEM/crypto/crypto.cpp
            ${PROJECT_SOURCE_DIR}/USER/udata/udata.cpp
    )

    target_include_directories(explorer_clipboard_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/APP/backend_client
            ${PROJECT_SOURCE_DIR}/APP/editor
            ${PROJECT_SOURCE_DIR}/APP/explorer
            ${PROJECT_SOURCE_DIR}/BACKEND/core
            ${PROJECT_SOURCE_DIR}/SYSTEM/audit
            ${PROJECT_SOURCE_DIR}/SYSTEM/console
            ${PROJECT_SOURCE_DIR}/SYSTEM/crypto
            ${PROJECT_SOURCE_DIR}/USER/udata
    )

    target_link_libraries(explorer_clipboard_tests
        PRIVATE
            TundraTUI::TundraTUI
            tundraux_backend_core
    )

    tundraux_add_test_executable(explorer_open_tests
        SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/explorer_open_tests.cpp
            ${PROJECT_SOURCE_DIR}/APP/explorer/explorer_directory.cpp
            ${PROJECT_SOURCE_DIR}/APP/explorer/explorer_navigation.cpp
            ${PROJECT_SOURCE_DIR}/APP/explorer/explorer_open.cpp
            ${PROJECT_SOURCE_DIR}/APP/explorer/explorer_text.cpp
    )

    target_include_directories(explorer_open_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/APP/backend_client
            ${PROJECT_SOURCE_DIR}/APP/editor
            ${PROJECT_SOURCE_DIR}/APP/explorer
            ${PROJECT_SOURCE_DIR}/BACKEND/core
            ${PROJECT_SOURCE_DIR}/SYSTEM/audit
            ${PROJECT_SOURCE_DIR}/USER/udata
    )

    target_link_libraries(explorer_open_tests
        PRIVATE
            TundraTUI::TundraTUI
            tundraux_backend_core
    )

    tundraux_add_test_executable(debug_structfile_tests
        SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/debug_structfile_tests.cpp
            ${PROJECT_SOURCE_DIR}/APP/backend_client/backend_client.cpp
            ${PROJECT_SOURCE_DIR}/APP/backend_client/backend_facade.cpp
            ${PROJECT_SOURCE_DIR}/APP/backend_client/backend_process.cpp
            ${PROJECT_SOURCE_DIR}/APP/backend_client/backend_runtime.cpp
            ${PROJECT_SOURCE_DIR}/SYSTEM/debug/debug.cpp
            ${PROJECT_SOURCE_DIR}/SYSTEM/crypto/crypto.cpp
            ${PROJECT_SOURCE_DIR}/SYSTEM/build_info/build_info.cpp
            ${PROJECT_SOURCE_DIR}/APP/legacy_direct/legacy_direct_stub.cpp
    )

    target_include_directories(debug_structfile_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/SYSTEM/build_info
            ${PROJECT_SOURCE_DIR}/SYSTEM/console
            ${PROJECT_SOURCE_DIR}/SYSTEM/crypto
            ${PROJECT_SOURCE_DIR}/SYSTEM/debug
            ${PROJECT_SOURCE_DIR}/CORE/startup
            ${PROJECT_SOURCE_DIR}/APP/backend_client
            ${PROJECT_SOURCE_DIR}/APP/legacy_direct
            ${TUNDRAUX_GENERATED_DIR}
    )

    target_link_libraries(debug_structfile_tests
        PRIVATE
            TundraTUI::TundraTUI
            tundraux_protocol
    )

    add_dependencies(debug_structfile_tests tundraux_generate_build_info)

    tundraux_add_test_executable(tundratui_footer_hint_tests
        SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/tundratui_footer_hint_tests.cpp
    )

    target_link_libraries(tundratui_footer_hint_tests
        PRIVATE
            TundraTUI::TundraTUI
    )

    tundraux_add_test_executable(frontend_backend_client_tests
        SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/frontend_backend_client_tests.cpp
            ${PROJECT_SOURCE_DIR}/APP/backend_client/backend_client.cpp
            ${PROJECT_SOURCE_DIR}/APP/backend_client/backend_process.cpp
            ${PROJECT_SOURCE_DIR}/APP/backend_client/backend_runtime.cpp
    )

    target_include_directories(frontend_backend_client_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/APP/backend_client
            ${PROJECT_SOURCE_DIR}/SHARED/protocol
    )

    target_link_libraries(frontend_backend_client_tests
        PRIVATE
            tundraux_protocol
    )

    tundraux_add_test_executable(frontend_backend_facade_tests
        SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/frontend_backend_facade_tests.cpp
            ${PROJECT_SOURCE_DIR}/APP/backend_client/backend_facade.cpp
            ${PROJECT_SOURCE_DIR}/APP/backend_client/backend_client.cpp
            ${PROJECT_SOURCE_DIR}/APP/backend_client/backend_process.cpp
            ${PROJECT_SOURCE_DIR}/APP/backend_client/backend_runtime.cpp
    )

    target_include_directories(frontend_backend_facade_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/APP/backend_client
            ${PROJECT_SOURCE_DIR}/SHARED/protocol
    )

    target_link_libraries(frontend_backend_facade_tests
        PRIVATE
            tundraux_protocol
    )
endif()
