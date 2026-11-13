add_library(abox_boot_v2_common STATIC
    src/abox_boot_v2_state.c src/abox_boot_v2_descriptor.c
    src/abox_boot_v2_image.c src/abox_boot_v2_fault.c)
add_library(abox::boot_v2_common ALIAS abox_boot_v2_common)
target_include_directories(abox_boot_v2_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(abox_boot_v2_common PUBLIC abox::core)
add_library(abox_boot_v2_app STATIC src/abox_boot_v2_app.c
    src/abox_boot_v2_ec800_port.c src/abox_boot_v2_services.c)
add_library(abox::boot_v2_app ALIAS abox_boot_v2_app)
target_include_directories(abox_boot_v2_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(abox_boot_v2_app PUBLIC abox::boot_v2_common
    abox::https_ufs_downloader abox::ec800 abox::mqtt_ec800)
add_library(abox_boot_v2_boot STATIC src/abox_boot_v2_boot.c src/abox_boot_v2_installer.c)
add_library(abox::boot_v2_boot ALIAS abox_boot_v2_boot)
target_include_directories(abox_boot_v2_boot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(abox_boot_v2_boot PUBLIC abox::boot_v2_common abox::core)
