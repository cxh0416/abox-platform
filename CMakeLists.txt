cmake_minimum_required(VERSION 3.22)
project(ABoxPlatform VERSION 0.3.0 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
foreach(module core board_io cjson ec800 ec_power mqtt enrollment ota boot_v2 cloud log legacy/ota)
    add_subdirectory(components/${module})
endforeach()
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ABoxPlatform.cmake)
include(CTest)
if(BUILD_TESTING AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(tests)
endif()
# Product builds only include libraries explicitly linked by the product.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    foreach(abox_target IN ITEMS
            abox_core abox_board_io abox_cjson abox_ec800 abox_ec800_ufs abox_ec800_tls
            abox_ec800_command_port abox_ec_power abox_mqtt_v4
            abox_mqtt_ec800_rx abox_mqtt_ec800 abox_mqtt_tls
            abox_mqtt_runtime abox_mqtt_trial abox_enrollment
            abox_mqtt_digest abox_mqtt_request abox_mqtt_idempotency abox_mqtt_backoff abox_mqtt_registry
            abox_ec800_ufs_at abox_ufs_journal abox_cloud_v4 abox_mqtt_idem_ufs
            abox_enrollment_ec800_http abox_https_ufs_downloader
            abox_boot_v2_common abox_boot_v2_app abox_boot_v2_boot abox_cloud_service abox_log_ring abox_maintenance_service abox_app_service abox_ota)
        set_property(TARGET ${abox_target} PROPERTY EXCLUDE_FROM_ALL TRUE)
    endforeach()
endif()
