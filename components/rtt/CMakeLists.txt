# RTT is opt-in, including when the platform is built standalone.
add_library(abox_rtt STATIC EXCLUDE_FROM_ALL third_party/SEGGER_RTT.c)
add_library(abox::rtt ALIAS abox_rtt)
target_include_directories(abox_rtt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
