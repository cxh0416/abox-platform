set(ABOX_NOR_IDEM_KIB "1536" CACHE STRING "V3 NOR idempotency partition size in KiB")
if(NOT ABOX_NOR_IDEM_KIB MATCHES "^[1-9][0-9]*$" OR
   ABOX_NOR_IDEM_KIB LESS 12 OR ABOX_NOR_IDEM_KIB GREATER 3584)
    message(FATAL_ERROR "ABOX_NOR_IDEM_KIB must be a sector-aligned size between 12 and 3584 KiB")
endif()
math(EXPR idem_sector_remainder "${ABOX_NOR_IDEM_KIB} % 4")
if(idem_sector_remainder)
    message(FATAL_ERROR "ABOX_NOR_IDEM_KIB must be a multiple of 4 KiB")
endif()

add_library(abox_nor STATIC src/abox_nor.c)
add_library(abox::nor ALIAS abox_nor)
target_include_directories(abox_nor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(abox_nor PUBLIC ABOX_NOR_IDEM_KIB=${ABOX_NOR_IDEM_KIB}U)
