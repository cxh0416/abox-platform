add_library(abox_cloud_service STATIC src/abox_cloud_service.c src/abox_cloud_internal_v2.c src/abox_cloud_response.c src/abox_cloud_dispatch.c src/abox_cloud_queue.c)
add_library(abox::cloud_service ALIAS abox_cloud_service)
target_include_directories(abox_cloud_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(abox_cloud_service PUBLIC abox::cjson abox::mqtt_v4 abox::mqtt_ec800 abox::ec800_tls)

add_library(abox_maintenance_service STATIC src/abox_maintenance_service.c)
add_library(abox::maintenance_service ALIAS abox_maintenance_service)
target_include_directories(abox_maintenance_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(abox_maintenance_service PUBLIC abox::mqtt_trial abox::core)

add_library(abox_app_service STATIC src/abox_app_service.c)
add_library(abox::app_service ALIAS abox_app_service)
target_include_directories(abox_app_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(abox_app_service PUBLIC abox::ec800 abox::mqtt_ec800_rx abox::ec_power abox::boot_v2_app)

add_library(abox_cloud_v4 STATIC src/abox_cloud_v4.c src/abox_time_client.c)
add_library(abox::cloud_v4 ALIAS abox_cloud_v4)
target_include_directories(abox_cloud_v4 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(abox_cloud_v4 PUBLIC abox::cloud_service abox::mqtt_request)

add_library(abox_mqtt_idem_ufs STATIC src/abox_mqtt_idem_ufs.c)
add_library(abox::mqtt_idem_ufs ALIAS abox_mqtt_idem_ufs)
target_include_directories(abox_mqtt_idem_ufs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(abox_mqtt_idem_ufs PUBLIC abox::mqtt_idempotency abox::ufs_journal)
