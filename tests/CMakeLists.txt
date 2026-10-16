find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME abox_mqtt_psk_files_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_mqtt_psk_files.py)
add_executable(abox_monotonic_test abox_monotonic_test.c)
target_include_directories(abox_monotonic_test PRIVATE monotonic_stub
    ../targets/stm32f105_ec800_v1/include)
add_test(NAME abox_monotonic_test COMMAND abox_monotonic_test)
add_executable(abox_time_sync_test abox_time_sync_test.c)
target_link_libraries(abox_time_sync_test PRIVATE abox::core)
add_test(NAME abox_time_sync_test COMMAND abox_time_sync_test)
add_executable(abox_clock_test abox_clock_test.c)
target_link_libraries(abox_clock_test PRIVATE abox::core abox::mqtt_v4)
add_test(NAME abox_clock_test COMMAND abox_clock_test)
add_executable(abox_board_io_test abox_board_io_test.c)
target_link_libraries(abox_board_io_test PRIVATE abox::board_io)
add_test(NAME abox_board_io_test COMMAND abox_board_io_test)
add_executable(abox_board_io_stm32_test abox_board_io_stm32_test.c
    ../targets/stm32f105_ec800_v1/board_io_stm32f105.c)
target_include_directories(abox_board_io_stm32_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stm32_stub
    ${CMAKE_CURRENT_SOURCE_DIR}/../targets/stm32f105_ec800_v1/include)
target_link_libraries(abox_board_io_stm32_test PRIVATE abox::board_io)
add_test(NAME abox_board_io_stm32_test COMMAND abox_board_io_stm32_test)
add_test(NAME abox_component_contract_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cmake_component_contract_test.py)
add_test(NAME abox_release_publish_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_publish_release.py)
add_executable(abox_config_store_test abox_config_store_test.c)
target_link_libraries(abox_config_store_test PRIVATE abox::core)
add_test(NAME abox_config_store_test COMMAND abox_config_store_test)
foreach(case IN ITEMS
    enrollment:enrollment
    enrollment_ec800_http:enrollment_ec800_http
    ec800_ufs:ec800_ufs
    ec800_tls:ec800_tls
    mqtt_tls:mqtt_tls
    mqtt_trial:mqtt_trial
    ec800_command_port:ec800_command_port
    platform_config:core
    ota_flash:ota
    ota_ec_at:ota
    ec800:ec800
    mqtt_v4_contract:mqtt_v4
    mqtt_runtime:mqtt_runtime
    mqtt_ec800_rx:mqtt_ec800_rx
    mqtt_ec800:mqtt_ec800
    https_ufs_downloader:https_ufs_downloader
    ec_power:ec_power
    boot_v2:boot_v2_boot
    boot_v2_app:boot_v2_app)
    string(REPLACE ":" ";" pair "${case}")
    list(GET pair 0 test_name)
    list(GET pair 1 module)
    if(test_name STREQUAL "ota_flash" OR test_name STREQUAL "ota_ec_at"
            OR test_name STREQUAL "mqtt_v4_contract" OR test_name STREQUAL "ec_power"
            OR test_name STREQUAL "boot_v2" OR test_name STREQUAL "boot_v2_app")
        set(source "${test_name}_test.c")
    else()
        set(source "abox_${test_name}_test.c")
    endif()
    add_executable(abox_${test_name}_test ${source})
    target_link_libraries(abox_${test_name}_test PRIVATE abox::${module})
    add_test(NAME abox_${test_name}_test COMMAND abox_${test_name}_test)
endforeach()
add_executable(abox_ec800_mqtt_rx_route_test abox_ec800_mqtt_rx_route_test.c)
target_link_libraries(abox_ec800_mqtt_rx_route_test PRIVATE abox::ec800 abox::mqtt_ec800_rx)
add_test(NAME abox_ec800_mqtt_rx_route_test COMMAND abox_ec800_mqtt_rx_route_test)
add_executable(abox_mqtt_receipt_test abox_mqtt_receipt_test.c)
target_link_libraries(abox_mqtt_receipt_test PRIVATE abox::mqtt_ec800)
add_test(NAME abox_mqtt_receipt_test COMMAND abox_mqtt_receipt_test)
add_executable(abox_mqtt_ec800_bridge_test abox_mqtt_ec800_bridge_test.c)
target_link_libraries(abox_mqtt_ec800_bridge_test PRIVATE abox::mqtt_ec800)
add_test(NAME abox_mqtt_ec800_bridge_test COMMAND abox_mqtt_ec800_bridge_test)
add_executable(abox_mqtt_runtime_core_port_test abox_mqtt_runtime_core_port_test.c)
target_link_libraries(abox_mqtt_runtime_core_port_test PRIVATE abox::mqtt_runtime)
add_test(NAME abox_mqtt_runtime_core_port_test COMMAND abox_mqtt_runtime_core_port_test)
add_executable(abox_ec800_iccid_test abox_ec800_iccid_test.c)
target_link_libraries(abox_ec800_iccid_test PRIVATE abox::ec800)
add_test(NAME abox_ec800_iccid_test COMMAND abox_ec800_iccid_test)
add_executable(abox_enrollment_identity_test abox_enrollment_identity_test.c)
target_link_libraries(abox_enrollment_identity_test PRIVATE abox::enrollment)
add_test(NAME abox_enrollment_identity_test COMMAND abox_enrollment_identity_test)
add_executable(abox_stm32_enrollment_identity_test
    abox_stm32_enrollment_identity_test.c
    ../targets/stm32f105_ec800_v1/enrollment_identity_stm32f105.c)
target_include_directories(abox_stm32_enrollment_identity_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stm32_identity_stub
    ${CMAKE_CURRENT_SOURCE_DIR}/../targets/stm32f105_ec800_v1/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/boot_v2/include)
target_link_libraries(abox_stm32_enrollment_identity_test PRIVATE abox::enrollment)
add_test(NAME abox_stm32_enrollment_identity_test COMMAND abox_stm32_enrollment_identity_test)
add_executable(abox_enrollment_service_test abox_enrollment_service_test.c)
target_link_libraries(abox_enrollment_service_test PRIVATE abox::enrollment_ec800_http)
add_test(NAME abox_enrollment_service_test COMMAND abox_enrollment_service_test)
add_executable(abox_boot_v2_ec800_port_test abox_boot_v2_ec800_port_test.c)
target_link_libraries(abox_boot_v2_ec800_port_test PRIVATE abox::boot_v2_app)
add_test(NAME abox_boot_v2_ec800_port_test COMMAND abox_boot_v2_ec800_port_test)
add_executable(abox_boot_v2_app_init_test boot_v2_app_init_test.c)
target_link_libraries(abox_boot_v2_app_init_test PRIVATE abox::boot_v2_app)
add_test(NAME abox_boot_v2_app_init_test COMMAND abox_boot_v2_app_init_test)
add_executable(abox_ec800_recovery_test abox_ec800_recovery_test.c)
target_link_libraries(abox_ec800_recovery_test PRIVATE abox::ec_power)
add_test(NAME abox_ec800_recovery_test COMMAND abox_ec800_recovery_test)
add_executable(abox_mqtt_trial_executor_test abox_mqtt_trial_executor_test.c)
target_link_libraries(abox_mqtt_trial_executor_test PRIVATE abox::mqtt_trial)
add_test(NAME abox_mqtt_trial_executor_test COMMAND abox_mqtt_trial_executor_test)
add_executable(abox_cloud_service_test abox_cloud_service_test.c)
target_link_libraries(abox_cloud_service_test PRIVATE abox::cloud_service)
add_test(NAME abox_cloud_service_test COMMAND abox_cloud_service_test)

add_executable(abox_cloud_internal_v2_test abox_cloud_internal_v2_test.c)
target_link_libraries(abox_cloud_internal_v2_test PRIVATE abox::cloud_service)
add_test(NAME abox_cloud_internal_v2_test COMMAND abox_cloud_internal_v2_test)
add_executable(abox_log_ring_test abox_log_ring_test.c)
target_link_libraries(abox_log_ring_test PRIVATE abox::log_ring)
add_test(NAME abox_log_ring_test COMMAND abox_log_ring_test)

add_executable(abox_cloud_response_test abox_cloud_response_test.c)
target_link_libraries(abox_cloud_response_test PRIVATE abox::cloud_service)
add_test(NAME abox_cloud_response_test COMMAND abox_cloud_response_test)
add_executable(abox_maintenance_service_test abox_maintenance_service_test.c)
target_link_libraries(abox_maintenance_service_test PRIVATE abox::maintenance_service)
add_test(NAME abox_maintenance_service_test COMMAND abox_maintenance_service_test)
add_executable(abox_app_service_test abox_app_service_test.c ../components/cloud/src/abox_app_service.c
    ../components/ec_power/src/abox_ec800_recovery.c)
target_include_directories(abox_app_service_test PRIVATE
    ../components/cloud/include ../components/ec800/include ../components/boot_v2/include
    ../components/ota/include ../components/core/include ../components/ec_power/include)
add_test(NAME abox_app_service_test COMMAND abox_app_service_test)
add_executable(abox_app_service_failure_test app_service_failure_test.c
    ../components/cloud/src/abox_app_service.c ../components/ec_power/src/abox_ec800_recovery.c)
target_include_directories(abox_app_service_failure_test PRIVATE
    ../components/cloud/include ../components/boot_v2/include
    ../components/ota/include ../components/ec_power/include)
target_link_libraries(abox_app_service_failure_test PRIVATE abox::ec800 abox::mqtt_ec800_rx)
add_test(NAME abox_app_service_failure_test COMMAND abox_app_service_failure_test)
add_executable(abox_app_service_uart_test app_service_uart_test.c
    ../components/cloud/src/abox_app_service.c ../components/ec_power/src/abox_ec800_recovery.c)
target_include_directories(abox_app_service_uart_test PRIVATE
    ../components/cloud/include ../components/boot_v2/include ../components/ota/include
    ../components/ec_power/include)
target_link_libraries(abox_app_service_uart_test PRIVATE abox::ec800)
add_test(NAME abox_app_service_uart_test COMMAND abox_app_service_uart_test)
add_executable(abox_registration_failure_test registration_failure_test.c)
target_link_libraries(abox_registration_failure_test PRIVATE abox::mqtt_ec800 abox::enrollment_ec800_http)
add_test(NAME abox_registration_failure_test COMMAND abox_registration_failure_test)
add_executable(abox_cloud_dispatch_queue_test abox_cloud_dispatch_queue_test.c)
target_link_libraries(abox_cloud_dispatch_queue_test PRIVATE abox::cloud_service)
add_test(NAME abox_cloud_dispatch_queue_test COMMAND abox_cloud_dispatch_queue_test)

add_executable(abox_psk_station_test abox_psk_station_test.c
    ../components/ec800/src/abox_psk_station.c)
target_link_libraries(abox_psk_station_test PRIVATE abox::ec800_tls)
add_test(NAME abox_psk_station_test COMMAND abox_psk_station_test)

add_executable(abox_ec800_diagnostics_test abox_ec800_diagnostics_test.c)
target_link_libraries(abox_ec800_diagnostics_test PRIVATE abox::ec800)
add_test(NAME abox_ec800_diagnostics_test COMMAND abox_ec800_diagnostics_test)

add_executable(abox_mqtt_digest_test abox_mqtt_digest_test.c)
target_link_libraries(abox_mqtt_digest_test PRIVATE abox::mqtt_digest)
add_test(NAME abox_mqtt_digest_test COMMAND abox_mqtt_digest_test)
add_test(NAME abox_mqtt_digest_vectors
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_mqtt_digest_vectors.py
        $<TARGET_FILE:abox_mqtt_digest_test>)
add_test(NAME abox_mqtt_numbers
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_mqtt_numbers.py
        $<TARGET_FILE:abox_mqtt_digest_test>)

# Optional adapters from a product checkout; no target/firmware execution.
if(ABOX_STAGE2_PRODUCT_ROOT)
    add_test(NAME product_io_init_test COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_product_io_init.py
        --product-root ${ABOX_STAGE2_PRODUCT_ROOT})
    file(GLOB product_includes "${ABOX_STAGE2_PRODUCT_ROOT}/app/Core/User/*/include")
    add_executable(product_runtime_recovery_test product_runtime_recovery_test.c)
    target_compile_definitions(product_runtime_recovery_test PRIVATE
        PRODUCT_RUNTIME_SOURCE="${ABOX_STAGE2_PRODUCT_ROOT}/app/Core/User/mqtt/src/mqtt_runtime.c")
    if(EXISTS "${ABOX_STAGE2_PRODUCT_ROOT}/app/Core/User/system/include/vcu_config.h")
        target_compile_definitions(product_runtime_recovery_test PRIVATE PRODUCT_SWEEPER=1)
    endif()
    target_include_directories(product_runtime_recovery_test PRIVATE recovery_stubs ${product_includes})
    target_link_libraries(product_runtime_recovery_test PRIVATE abox::mqtt_runtime abox::mqtt_trial abox::core)
    add_test(NAME product_runtime_recovery_test COMMAND product_runtime_recovery_test)
    if(EXISTS "${ABOX_STAGE2_PRODUCT_ROOT}/app/tests/open_idem_recovery_test.c")
        add_executable(product_open_idem_recovery_test
            "${ABOX_STAGE2_PRODUCT_ROOT}/app/tests/open_idem_recovery_test.c")
        target_include_directories(product_open_idem_recovery_test PRIVATE recovery_stubs ${product_includes})
        target_link_libraries(product_open_idem_recovery_test PRIVATE
            abox::ec800 abox::boot_v2_common abox::mqtt_v4)
        add_test(NAME product_open_idem_recovery_test COMMAND product_open_idem_recovery_test)
    endif()
endif()
