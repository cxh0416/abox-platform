# RTT is opt-in, including when the platform is built standalone.
add_library(abox_rtt STATIC EXCLUDE_FROM_ALL third_party/SEGGER_RTT.c)
add_library(abox::rtt ALIAS abox_rtt)
# Only the portable C source is bundled; do not select the absent ASM helper.
target_compile_definitions(abox_rtt PUBLIC RTT_USE_ASM=0)
target_include_directories(abox_rtt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# Separate opt-in console; ordinary RTT users do not acquire the AT stack.
add_library(abox_rtt_console STATIC EXCLUDE_FROM_ALL src/abox_rtt_console.c)
add_library(abox::rtt_console ALIAS abox_rtt_console)
target_include_directories(abox_rtt_console PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(abox_rtt_console PUBLIC abox::rtt abox::ec800)
