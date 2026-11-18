# Run an existing C main() on an F105 with 64 KiB SRAM. Both test projects
# use the same RAM layout, newlib-nano and result convention.
function(abox_nano_smoke target)
    set(bench "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    set(layout "${bench}/gd32_v1_uart_loopback.ld")
    target_sources(${target} PRIVATE "${bench}/nano_smoke_start.c")
    target_compile_options(${target} PRIVATE -UNDEBUG)
    target_link_options(${target} PRIVATE -nostartfiles --specs=nano.specs --specs=nosys.specs
        "-T${layout}" -Wl,--gc-sections -Wl,--defsym,end=_ebss -Wl,--wrap=__assert_func
        "-Wl,-Map=${target}.map")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${layout}")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${target}> $<TARGET_FILE:${target}>.bin)
endfunction()
