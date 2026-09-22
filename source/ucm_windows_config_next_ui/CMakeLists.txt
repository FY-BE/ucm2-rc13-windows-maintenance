cmake_minimum_required(VERSION 3.24)

project(UcmConfigStudioNext VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Gui Network Qml Quick QuickControls2 QuickDialogs2)

set(UCM_NEXT_BACKEND_CANONICAL_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/backend")
set(UCM_NEXT_BACKEND_SOURCE_DIR
    "${CMAKE_BINARY_DIR}/backend_src")
set(UCM_NEXT_BACKEND_FILES
    compound_runtime_status_model.cpp compound_runtime_status_model.h
    config_model.cpp config_model.h config_transport.h
    config_session.cpp config_session.h
    diagnostic_package_writer.cpp diagnostic_package_writer.h
    event_timeline.cpp event_timeline.h
    libusb_backend.cpp libusb_backend.h
    mock_transport.cpp mock_transport.h
    reason_dictionary.cpp reason_dictionary.h
    runtime_status_model.cpp runtime_status_model.h
    usb_composite_runtime_status_v1.cpp usb_composite_runtime_status_v1.h
    usb_composite_runtime_trust_v1.cpp usb_composite_runtime_trust_v1.h
    usb_transfer_engine.cpp usb_transfer_engine.h
    usb_extended_wire_v2.cpp usb_extended_wire_v2.h
    usb_parameter_config_v9.cpp usb_parameter_config_v9.h
    usb_runtime_parameter_edit_v9.cpp usb_runtime_parameter_edit_v9.h
    usb_runtime_progress_v9.cpp usb_runtime_progress_v9.h
    usb_burst_hil_workflow.cpp usb_burst_hil_workflow.h
    usb_wire_v1.cpp usb_wire_v1.h
    usb_product_wire_v9.cpp usb_product_wire_v9.h
    product_log_download.cpp product_log_download.h
    usb_product_operations_v9.cpp usb_product_operations_v9.h
    usb_functionfs_product_operations.cpp
    usb_functionfs_transport.cpp usb_functionfs_transport.h
    usb_functionfs_diagnostics.cpp)
file(MAKE_DIRECTORY "${UCM_NEXT_BACKEND_SOURCE_DIR}")
foreach(source_file IN LISTS UCM_NEXT_BACKEND_FILES)
    configure_file(
        "${UCM_NEXT_BACKEND_CANONICAL_DIR}/${source_file}"
        "${UCM_NEXT_BACKEND_SOURCE_DIR}/${source_file}"
        COPYONLY)
endforeach()
configure_file(
    "${UCM_NEXT_BACKEND_CANONICAL_DIR}/../tests/usb_extended_wire_v2_tests.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_extended_wire_v2_tests.cpp"
    COPYONLY)
find_path(LIBUSB_INCLUDE_DIR NAMES libusb.h PATH_SUFFIXES libusb-1.0)
find_library(LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0)
if(NOT LIBUSB_INCLUDE_DIR OR NOT LIBUSB_LIBRARY)
    message(FATAL_ERROR "libusb-1.0 is required for the real USB bridge")
endif()

qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

add_executable(UcmReadOnlyProbe readonly_probe_main.cpp readonly_probe.cpp readonly_probe.h arm_force_csv.h)
add_executable(UcmEthercatProbe
    ethercat_probe_main.cpp ethercat_port_probe.cpp ethercat_port_probe.h
    ethercat_protocol.cpp ethercat_protocol.h)
target_link_libraries(UcmEthercatProbe PRIVATE Qt6::Core)
if(WIN32)
    target_link_libraries(UcmEthercatProbe PRIVATE iphlpapi)
endif()
add_executable(UcmEthercatMasterHil
    ethercat_master_hil_main.cpp
    ethercat_master_session.cpp ethercat_master_session.h
    ethercat_protocol.cpp ethercat_protocol.h fqx_machine_models.h)
target_link_libraries(UcmEthercatMasterHil PRIVATE Qt6::Core)

qt_add_executable(UcmConfigStudioNext
    main.cpp
    dashboard_bridge.cpp
    dashboard_bridge.h
    dashboard_poll_worker.cpp
    dashboard_poll_worker.h
    upgrade_package_check.cpp upgrade_package_check.h
    engineer_access.cpp
    engineer_access.h
    poll_schedule.h
    trend_buffer.cpp
    trend_buffer.h
    trend_plot_item.cpp
    trend_plot_item.h
    trend_curve.cpp
    trend_curve.h
    reference_force_source.cpp
    reference_force_source.h
    ethercat_port_probe.cpp
    ethercat_port_probe.h
    ethercat_protocol.cpp
    ethercat_protocol.h
    ethercat_master_session.cpp
    ethercat_master_session.h
    ethercat_master_controller.cpp
    ethercat_master_controller.h
    fqx_machine_models.h
    calibration_controller.cpp
    calibration_controller.h)

if(WIN32)
    enable_language(RC)
    target_sources(UcmConfigStudioNext PRIVATE app_icon.rc)
endif()

add_library(ucm_next_usb_backend STATIC
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/compound_runtime_status_model.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/config_model.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/config_session.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/diagnostic_package_writer.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/event_timeline.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/libusb_backend.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/mock_transport.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/reason_dictionary.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/runtime_status_model.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_composite_runtime_status_v1.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_composite_runtime_trust_v1.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_transfer_engine.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_extended_wire_v2.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_parameter_config_v9.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_runtime_parameter_edit_v9.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_runtime_progress_v9.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_burst_hil_workflow.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_wire_v1.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_product_wire_v9.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/product_log_download.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_product_operations_v9.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_functionfs_product_operations.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_functionfs_transport.cpp"
    "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_functionfs_diagnostics.cpp")
target_include_directories(ucm_next_usb_backend PUBLIC
    "${UCM_NEXT_BACKEND_SOURCE_DIR}"
    "${LIBUSB_INCLUDE_DIR}")
target_link_libraries(ucm_next_usb_backend PUBLIC
    Qt6::Core "${LIBUSB_LIBRARY}" setupapi)
target_compile_definitions(ucm_next_usb_backend PUBLIC
    UCM_CONFIG_SCHEMA_VERSION="2.0")

qt_add_qml_module(UcmConfigStudioNext
    URI UcmConfigStudio.Next
    VERSION 1.0
    QML_FILES
        Main.qml
        components/GlassPanel.qml
        components/AppPalette.js
        components/AppButton.qml
        components/AppTextField.qml
        components/AppComboBox.qml
        components/AppDialog.qml
        components/DiagnosticPage.qml
        components/ConfigurationPage.qml
        components/ProductOperationsPanel.qml
        components/EngineerConfig.js
        components/EngineerField.qml
        components/EngineerSection.qml
        components/EngineerDiagnosticsPanel.qml
        components/EvidencePage.qml
        components/CalibrationPage.qml
        components/ConnectionPage.qml
        components/EthercatMasterPage.qml
        components/FitProcessPanel.qml
        components/FitLivePlot.qml
        components/LogPage.qml
        components/TimelinePage.qml
        components/MetricCard.qml
        components/ForceComparisonCard.qml
        components/NavButton.qml
        components/PagePlaceholder.qml
        components/TrendCard.qml
        components/WaveformCard.qml
        components/CustomerTopNavButton.qml
        components/CustomerTopBar.qml
        components/CustomerActionButton.qml
        components/CustomerUnits.js
        components/CustomerRodMetric.qml
        components/CustomerTrendPanel.qml
        components/CustomerHomePage.qml
        components/CustomerConfigField.qml
        components/CustomerSettings.js
        components/CustomerSettingsPage.qml
        components/CustomerInfoPage.qml
        components/CustomerModeShell.qml
    RESOURCES
        assets/qizhen-sensing-logo.jpg
        assets/qizhen-ucm-icon.png
        assets/customer-home.svg
        assets/customer-settings.svg
        assets/customer-info.svg
        assets/customer-engineer.svg
        assets/customer-clock.svg
        assets/customer-cycle.svg
        assets/customer-download.svg
        assets/customer-machine-front.png
        assets/customer-machine-side.png)

add_custom_command(TARGET UcmConfigStudioNext POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/reference_force_camera"
        "$<TARGET_FILE_DIR:UcmConfigStudioNext>/reference_force_camera"
    COMMENT "Copying frozen Hikrobot reference-force bridge")

target_link_libraries(UcmConfigStudioNext PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickDialogs2
    ucm_next_usb_backend)

if(WIN32)
    target_link_libraries(UcmConfigStudioNext PRIVATE dwmapi iphlpapi)
endif()

if(WIN32)
    set_target_properties(UcmConfigStudioNext PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

include(CTest)
target_link_libraries(UcmReadOnlyProbe PRIVATE ucm_next_usb_backend Qt6::Core)

if(BUILD_TESTING)
    add_executable(ucm_next_ethercat_protocol_tests
        tests/ethercat_protocol_tests.cpp
        ethercat_protocol.cpp ethercat_protocol.h fqx_machine_models.h)
    target_include_directories(ucm_next_ethercat_protocol_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(ucm_next_ethercat_protocol_tests PRIVATE Qt6::Core)
    add_test(NAME ucm_next_ethercat_protocol
        COMMAND ucm_next_ethercat_protocol_tests)
    add_executable(ucm_next_ethercat_port_probe_tests
        tests/ethercat_port_probe_tests.cpp
        ethercat_port_probe.cpp ethercat_port_probe.h)
    target_include_directories(ucm_next_ethercat_port_probe_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(ucm_next_ethercat_port_probe_tests PRIVATE
        Qt6::Core)
    if(WIN32)
        target_link_libraries(ucm_next_ethercat_port_probe_tests PRIVATE
            iphlpapi)
    endif()
    add_test(NAME ucm_next_ethercat_port_probe
        COMMAND ucm_next_ethercat_port_probe_tests)
    add_executable(ucm_next_readonly_probe_tests tests/readonly_probe_tests.cpp readonly_probe.cpp readonly_probe.h)
    target_include_directories(ucm_next_readonly_probe_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(ucm_next_readonly_probe_tests PRIVATE ucm_next_usb_backend Qt6::Core)
    add_test(NAME ucm_next_readonly_probe COMMAND ucm_next_readonly_probe_tests)
    add_test(NAME ucm_next_readonly_probe_simulation COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:UcmReadOnlyProbe>" "-DOUTPUT_ROOT=${CMAKE_BINARY_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/probe_simulation.cmake")
    file(COPY "${UCM_NEXT_BACKEND_CANONICAL_DIR}/../tests/fixtures/arm_product_v9"
        DESTINATION "${UCM_NEXT_BACKEND_SOURCE_DIR}/fixtures")
    file(COPY "${UCM_NEXT_BACKEND_CANONICAL_DIR}/../tests/fixtures/arm_product_v9_parameter_config"
        DESTINATION "${UCM_NEXT_BACKEND_SOURCE_DIR}/fixtures")
    foreach(product_test IN ITEMS usb_product_wire_v9 product_log_download usb_product_operations_v9 usb_product_transport)
        configure_file("${UCM_NEXT_BACKEND_CANONICAL_DIR}/../tests/${product_test}_tests.cpp"
            "${UCM_NEXT_BACKEND_SOURCE_DIR}/${product_test}_tests.cpp" COPYONLY)
        add_executable(ucm_next_${product_test}_tests
            "${UCM_NEXT_BACKEND_SOURCE_DIR}/${product_test}_tests.cpp")
        target_link_libraries(ucm_next_${product_test}_tests PRIVATE ucm_next_usb_backend Qt6::Core)
        add_test(NAME ucm_next_${product_test} COMMAND ucm_next_${product_test}_tests)
    endforeach()
    configure_file("${UCM_NEXT_BACKEND_CANONICAL_DIR}/../tests/usb_product_golden_tests.cpp"
        "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_product_golden_tests.cpp" COPYONLY)
    add_executable(ucm_next_product_golden_tests
        "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_product_golden_tests.cpp")
    target_compile_definitions(ucm_next_product_golden_tests PRIVATE
        "UCM_PRODUCT_GOLDEN_PATH=\"${UCM_NEXT_BACKEND_CANONICAL_DIR}/../tests/fixtures/arm_product_v9/UCM_WINDOWS_USB_GOLDEN_V1.json\"")
    target_link_libraries(ucm_next_product_golden_tests PRIVATE ucm_next_usb_backend Qt6::Core)
    add_test(NAME ucm_next_product_golden COMMAND ucm_next_product_golden_tests)
    configure_file(
        "${UCM_NEXT_BACKEND_CANONICAL_DIR}/../tests/compiler_suite_c_smoke.c"
        "${UCM_NEXT_BACKEND_SOURCE_DIR}/compiler_suite_c_smoke.c"
        COPYONLY)
    add_executable(ucm_next_c_compiler_smoke
        "${UCM_NEXT_BACKEND_SOURCE_DIR}/compiler_suite_c_smoke.c")
    set_target_properties(ucm_next_c_compiler_smoke PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF LINKER_LANGUAGE C)
    add_test(NAME ucm_next_c_compiler_smoke
        COMMAND ucm_next_c_compiler_smoke)

    add_test(NAME ucm_next_ui_fallback_smoke
        COMMAND UcmConfigStudioNext
            --no-system-backdrop
            --screenshot
            "${CMAKE_BINARY_DIR}/ucm-next-ui-fallback-smoke.png")
    add_test(NAME ucm_next_ui_customer_home_1280_smoke
        COMMAND UcmConfigStudioNext
            --no-system-backdrop --window-size 1280x800 --page 0
            --screenshot
            "${CMAKE_BINARY_DIR}/ucm-next-ui-customer-home-1280.png")
    add_test(NAME ucm_next_ui_customer_settings_1366_smoke
        COMMAND UcmConfigStudioNext
            --no-system-backdrop --window-size 1366x768 --page 1
            --screenshot
            "${CMAKE_BINARY_DIR}/ucm-next-ui-customer-settings-1366.png")
    add_test(NAME ucm_next_ui_customer_info_1440_smoke
        COMMAND UcmConfigStudioNext
            --no-system-backdrop --window-size 1440x810 --page 5
            --screenshot
            "${CMAKE_BINARY_DIR}/ucm-next-ui-customer-info-1440.png")
    add_test(NAME ucm_next_ui_customer_home_1920_smoke
        COMMAND UcmConfigStudioNext
            --no-system-backdrop --window-size 1920x1080 --page 0
            --screenshot
            "${CMAKE_BINARY_DIR}/ucm-next-ui-customer-home-1920.png")
    add_executable(ucm_next_customer_product_draft_tests tests/customer_product_draft_tests.cpp)
    target_include_directories(ucm_next_customer_product_draft_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(ucm_next_customer_product_draft_tests PRIVATE Qt6::Core)
    add_test(NAME ucm_next_customer_product_draft COMMAND ucm_next_customer_product_draft_tests)
    find_program(QML_TEST_RUNNER qmltestrunner REQUIRED)
    add_test(NAME ucm_next_ui_linked_cursor
        COMMAND "${QML_TEST_RUNNER}"
            -input "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_executable(ucm_next_ui_trend_plot_tests
        tests/trend_plot_tests.cpp
        trend_buffer.cpp trend_buffer.h
        trend_plot_item.cpp trend_plot_item.h
        trend_curve.cpp trend_curve.h)
    target_include_directories(ucm_next_ui_trend_plot_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(ucm_next_ui_trend_plot_tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
    add_test(NAME ucm_next_ui_trend_plot
        COMMAND ucm_next_ui_trend_plot_tests)
    add_executable(ucm_next_upgrade_package_check_tests tests/upgrade_package_check_tests.cpp upgrade_package_check.cpp upgrade_package_check.h)
    target_include_directories(ucm_next_upgrade_package_check_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(ucm_next_upgrade_package_check_tests PRIVATE Qt6::Core)
    add_test(NAME ucm_next_upgrade_package_check COMMAND ucm_next_upgrade_package_check_tests)
    add_executable(ucm_next_ui_poll_schedule_tests
        tests/poll_schedule_tests.cpp poll_schedule.h)
    target_include_directories(ucm_next_ui_poll_schedule_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(ucm_next_ui_poll_schedule_tests PRIVATE Qt6::Core)
    add_test(NAME ucm_next_ui_poll_schedule
        COMMAND ucm_next_ui_poll_schedule_tests)
    add_executable(ucm_next_ui_arm_force_csv_tests tests/arm_force_csv_tests.cpp arm_force_csv.h)
    target_include_directories(ucm_next_ui_arm_force_csv_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(ucm_next_ui_arm_force_csv_tests PRIVATE ucm_next_usb_backend Qt6::Core)
    add_test(NAME ucm_next_ui_arm_force_csv COMMAND ucm_next_ui_arm_force_csv_tests)
    add_executable(ucm_next_ui_async_configuration_tests
        tests/async_configuration_tests.cpp
        dashboard_poll_worker.cpp dashboard_poll_worker.h upgrade_package_check.cpp upgrade_package_check.h)
    target_include_directories(ucm_next_ui_async_configuration_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(ucm_next_ui_async_configuration_tests PRIVATE
        ucm_next_usb_backend Qt6::Core)
    add_test(NAME ucm_next_ui_async_configuration
        COMMAND ucm_next_ui_async_configuration_tests)
    add_executable(ucm_next_usb_acoustic_config_hil
        tests/acoustic_config_hil_main.cpp)
    target_include_directories(ucm_next_usb_acoustic_config_hil PRIVATE
        "${UCM_NEXT_BACKEND_SOURCE_DIR}")
    target_link_libraries(ucm_next_usb_acoustic_config_hil PRIVATE
        ucm_next_usb_backend Qt6::Core)
    add_executable(ucm_next_usb_burst_config_hil
        tests/burst_config_hil_main.cpp)
    target_include_directories(ucm_next_usb_burst_config_hil PRIVATE
        "${UCM_NEXT_BACKEND_SOURCE_DIR}")
    target_link_libraries(ucm_next_usb_burst_config_hil PRIVATE
        ucm_next_usb_backend Qt6::Core)
    add_executable(ucm_next_calibration_readback_hil tests/calibration_readback_hil_main.cpp)
    target_link_libraries(ucm_next_calibration_readback_hil PRIVATE ucm_next_usb_backend Qt6::Core)
    add_executable(ucm_next_usb_upgrade_hil
        tests/usb_upgrade_hil_main.cpp
        upgrade_package_check.cpp upgrade_package_check.h)
    target_include_directories(ucm_next_usb_upgrade_hil PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${UCM_NEXT_BACKEND_SOURCE_DIR}")
    target_link_libraries(ucm_next_usb_upgrade_hil PRIVATE
        ucm_next_usb_backend Qt6::Core)
    add_executable(ucm_next_usb_burst_hil_workflow_tests
        tests/usb_burst_hil_workflow_tests.cpp)
    target_include_directories(ucm_next_usb_burst_hil_workflow_tests PRIVATE
        "${UCM_NEXT_BACKEND_SOURCE_DIR}")
    target_link_libraries(ucm_next_usb_burst_hil_workflow_tests PRIVATE
        ucm_next_usb_backend Qt6::Core)
    add_test(NAME ucm_next_usb_burst_hil_workflow
        COMMAND ucm_next_usb_burst_hil_workflow_tests)
    add_executable(ucm_next_ui_engineer_access_tests
        tests/engineer_access_tests.cpp
        engineer_access.cpp engineer_access.h)
    target_include_directories(ucm_next_ui_engineer_access_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(ucm_next_ui_engineer_access_tests PRIVATE
        Qt6::Core Qt6::Network)
    add_test(NAME ucm_next_ui_engineer_access
        COMMAND ucm_next_ui_engineer_access_tests)
    add_executable(ucm_next_usb_extended_wire_v2_tests
        "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_extended_wire_v2_tests.cpp")
    target_link_libraries(ucm_next_usb_extended_wire_v2_tests PRIVATE
        ucm_next_usb_backend Qt6::Core)
    add_test(NAME ucm_next_usb_extended_wire_v2
        COMMAND ucm_next_usb_extended_wire_v2_tests)
    add_executable(ucm_next_usb_parameter_config_v9_tests
        tests/usb_parameter_config_v9_tests.cpp)
    target_include_directories(ucm_next_usb_parameter_config_v9_tests PRIVATE
        "${UCM_NEXT_BACKEND_SOURCE_DIR}")
    target_link_libraries(ucm_next_usb_parameter_config_v9_tests PRIVATE
        ucm_next_usb_backend Qt6::Core)
    add_test(NAME ucm_next_usb_parameter_config_v9
        COMMAND ucm_next_usb_parameter_config_v9_tests)
    add_executable(ucm_next_usb_runtime_parameter_edit_v9_tests
        tests/usb_runtime_parameter_edit_v9_tests.cpp)
    target_include_directories(ucm_next_usb_runtime_parameter_edit_v9_tests PRIVATE
        "${UCM_NEXT_BACKEND_SOURCE_DIR}")
    target_link_libraries(ucm_next_usb_runtime_parameter_edit_v9_tests PRIVATE
        ucm_next_usb_backend Qt6::Core)
    add_test(NAME ucm_next_usb_runtime_parameter_edit_v9
        COMMAND ucm_next_usb_runtime_parameter_edit_v9_tests)
    add_executable(ucm_next_usb_runtime_progress_v9_tests
        tests/usb_runtime_progress_v9_tests.cpp)
    target_include_directories(ucm_next_usb_runtime_progress_v9_tests PRIVATE
        "${UCM_NEXT_BACKEND_SOURCE_DIR}")
    target_link_libraries(ucm_next_usb_runtime_progress_v9_tests PRIVATE
        ucm_next_usb_backend Qt6::Core)
    add_test(NAME ucm_next_usb_runtime_progress_v9
        COMMAND ucm_next_usb_runtime_progress_v9_tests)
    configure_file(
        "${UCM_NEXT_BACKEND_CANONICAL_DIR}/../tests/runtime_status_model_tests.cpp"
        "${UCM_NEXT_BACKEND_SOURCE_DIR}/runtime_status_model_tests.cpp"
        COPYONLY)
    add_executable(ucm_next_runtime_status_model_tests
        "${UCM_NEXT_BACKEND_SOURCE_DIR}/runtime_status_model_tests.cpp")
    target_link_libraries(ucm_next_runtime_status_model_tests PRIVATE
        ucm_next_usb_backend Qt6::Core)
    add_test(NAME ucm_next_runtime_status_model
        COMMAND ucm_next_runtime_status_model_tests)
    configure_file(
        "${UCM_NEXT_BACKEND_CANONICAL_DIR}/../tests/compound_runtime_status_model_tests.cpp"
        "${UCM_NEXT_BACKEND_SOURCE_DIR}/compound_runtime_status_model_tests.cpp"
        COPYONLY)
    add_executable(ucm_next_compound_runtime_status_model_tests
        "${UCM_NEXT_BACKEND_SOURCE_DIR}/compound_runtime_status_model_tests.cpp")
    target_link_libraries(
        ucm_next_compound_runtime_status_model_tests PRIVATE
        ucm_next_usb_backend Qt6::Core)
    add_test(NAME ucm_next_compound_runtime_status_model
        COMMAND ucm_next_compound_runtime_status_model_tests)
    configure_file(
        "${UCM_NEXT_BACKEND_CANONICAL_DIR}/../tests/usb_composite_runtime_status_v1_tests.cpp"
        "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_composite_runtime_status_v1_tests.cpp"
        COPYONLY)
    add_executable(ucm_next_usb_composite_runtime_status_v1_tests
        "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_composite_runtime_status_v1_tests.cpp")
    target_compile_definitions(
        ucm_next_usb_composite_runtime_status_v1_tests PRIVATE
        "UCM_D021_FIXTURE_DIR=\"${UCM_NEXT_BACKEND_CANONICAL_DIR}/../tests/fixtures/arm_compound_runtime_status\"")
    target_link_libraries(
        ucm_next_usb_composite_runtime_status_v1_tests PRIVATE
        ucm_next_usb_backend Qt6::Core)
    add_test(NAME ucm_next_usb_composite_runtime_status_v1
        COMMAND ucm_next_usb_composite_runtime_status_v1_tests)
    configure_file(
        "${UCM_NEXT_BACKEND_CANONICAL_DIR}/../tests/usb_composite_runtime_trust_v1_tests.cpp"
        "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_composite_runtime_trust_v1_tests.cpp"
        COPYONLY)
    add_executable(ucm_next_usb_composite_runtime_trust_v1_tests
        "${UCM_NEXT_BACKEND_SOURCE_DIR}/usb_composite_runtime_trust_v1_tests.cpp")
    target_compile_definitions(
        ucm_next_usb_composite_runtime_trust_v1_tests PRIVATE
        "UCM_D021_FIXTURE_DIR=\"${UCM_NEXT_BACKEND_CANONICAL_DIR}/../tests/fixtures/arm_compound_runtime_status\"")
    target_link_libraries(
        ucm_next_usb_composite_runtime_trust_v1_tests PRIVATE
        ucm_next_usb_backend Qt6::Core)
    add_test(NAME ucm_next_usb_composite_runtime_trust_v1
        COMMAND ucm_next_usb_composite_runtime_trust_v1_tests)
    add_test(NAME ucm_next_ui_diagnostic_smoke
        COMMAND UcmConfigStudioNext
            --no-system-backdrop --engineer-preview
            --page 2
            --screenshot
            "${CMAKE_BINARY_DIR}/ucm-next-ui-diagnostic-smoke.png")
    add_test(NAME ucm_next_ui_configuration_smoke
        COMMAND UcmConfigStudioNext
            --no-system-backdrop --engineer-preview
            --page 3
            --screenshot
            "${CMAKE_BINARY_DIR}/ucm-next-ui-configuration-smoke.png")
    add_test(NAME ucm_next_ui_configuration_local_prepare
        COMMAND UcmConfigStudioNext
            --configuration-local-self-test)
    add_test(NAME ucm_next_ui_configuration_apply_mock
        COMMAND UcmConfigStudioNext
            --configuration-apply-mock-self-test)
    add_test(NAME ucm_next_ui_reference_force_parser
        COMMAND UcmConfigStudioNext
            --reference-force-parser-self-test)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME ucm_next_ui_reference_force_vendor
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/reference_force_camera/reference_force_bridge.py"
                --verify-vendor)
        set_tests_properties(ucm_next_ui_reference_force_vendor PROPERTIES
            ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
        execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import numpy"
            RESULT_VARIABLE calibration_numpy_available
            OUTPUT_QUIET ERROR_QUIET)
        if(calibration_numpy_available EQUAL 0)
            add_test(NAME ucm_next_ui_calibration_candidate
                COMMAND "${Python3_EXECUTABLE}" -m unittest -v test_calibration_candidate.py)
            set_tests_properties(ucm_next_ui_calibration_candidate PROPERTIES
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/reference_force_camera"
                ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
        endif()
    endif()
    add_test(NAME ucm_next_ui_logs_smoke
        COMMAND UcmConfigStudioNext
            --no-system-backdrop --engineer-preview
            --page 4
            --screenshot
            "${CMAKE_BINARY_DIR}/ucm-next-ui-logs-smoke.png")
    add_test(NAME ucm_next_ui_timeline_smoke
        COMMAND UcmConfigStudioNext
            --no-system-backdrop --engineer-preview
            --page 5
            --screenshot
            "${CMAKE_BINARY_DIR}/ucm-next-ui-timeline-smoke.png")
    add_test(NAME ucm_next_ui_evidence_smoke
        COMMAND UcmConfigStudioNext
            --no-system-backdrop --engineer-preview
            --page 6
            --screenshot
            "${CMAKE_BINARY_DIR}/ucm-next-ui-evidence-smoke.png")
    add_test(NAME ucm_next_ui_calibration_smoke
        COMMAND UcmConfigStudioNext
            --no-system-backdrop --engineer-preview
            --page 7
            --screenshot
            "${CMAKE_BINARY_DIR}/ucm-next-ui-calibration-smoke.png")
    add_test(NAME ucm_next_ui_connection_smoke
        COMMAND UcmConfigStudioNext
            --no-system-backdrop --engineer-preview
            --page 8
            --screenshot
            "${CMAKE_BINARY_DIR}/ucm-next-ui-connection-smoke.png")
    add_test(NAME ucm_next_ui_ethercat_master_smoke
        COMMAND UcmConfigStudioNext
            --no-system-backdrop --engineer-preview
            --page 9
            --screenshot
            "${CMAKE_BINARY_DIR}/ucm-next-ui-ethercat-master-smoke.png")
endif()


add_executable(ucm_next_calibration_switch tests/calibration_readback_hil_main.cpp)
target_link_libraries(ucm_next_calibration_switch PRIVATE ucm_next_usb_backend Qt6::Core)

