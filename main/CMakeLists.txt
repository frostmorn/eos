file(GLOB_RECURSE EOS_SRCS "*.c")

idf_component_register(
    SRCS ${EOS_SRCS}
    INCLUDE_DIRS "include_override" "."
    WHOLE_ARCHIVE 
    LDFRAGMENTS "eld/driver.lf" "eld/bin.lf" "eld/assets.lf"
)
idf_build_set_property(
    COMPILE_OPTIONS "-DFD_SETSIZE=128" APPEND
)

target_link_libraries(
${COMPONENT_LIB} INTERFACE
"-Wl,--wrap=_open_r"
"-Wl,--wrap=_close_r"
"-Wl,--wrap=_read_r"
"-Wl,--wrap=_write_r"
"-Wl,--wrap=_lseek_r"
"-Wl,--wrap=_fstat_r"
"-Wl,--wrap=_stat_r"
"-Wl,--wrap=_link_r"
"-Wl,--wrap=_unlink_r"
"-Wl,--wrap=_rename_r"
"-Wl,--wrap=opendir"
"-Wl,--wrap=closedir"
"-Wl,--wrap=mkdir"
"-Wl,--wrap=system"
"-Wl,--wrap=chdir"
"-Wl,--wrap=getcwd"
"-Wl,--wrap=pthread_create"
"-Wl,--wrap=xTaskCreateStaticPinnedToCore"
"-Wl,--wrap=xTaskCreatePinnedToCore"
"-Wl,--wrap=heap_caps_aligned_alloc_base"
"-Wl,--wrap=heap_caps_free"
"-Wl,--wrap=usleep"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=override-init
)

if(DEFINED WIDTH AND NOT DEFINED EOS_QEMU_DISPLAY_WIDTH)
    set(EOS_QEMU_DISPLAY_WIDTH ${WIDTH})
endif()
if(DEFINED HEIGHT AND NOT DEFINED EOS_QEMU_DISPLAY_HEIGHT)
    set(EOS_QEMU_DISPLAY_HEIGHT ${HEIGHT})
endif()
if(NOT DEFINED EOS_QEMU_DISPLAY_WIDTH AND DEFINED ENV{EOS_QEMU_DISPLAY_WIDTH})
    set(EOS_QEMU_DISPLAY_WIDTH $ENV{EOS_QEMU_DISPLAY_WIDTH})
endif()
if(NOT DEFINED EOS_QEMU_DISPLAY_HEIGHT AND DEFINED ENV{EOS_QEMU_DISPLAY_HEIGHT})
    set(EOS_QEMU_DISPLAY_HEIGHT $ENV{EOS_QEMU_DISPLAY_HEIGHT})
endif()
if(NOT DEFINED EOS_QEMU_DISPLAY_WIDTH AND DEFINED ENV{WIDTH})
    set(EOS_QEMU_DISPLAY_WIDTH $ENV{WIDTH})
endif()
if(NOT DEFINED EOS_QEMU_DISPLAY_HEIGHT AND DEFINED ENV{HEIGHT})
    set(EOS_QEMU_DISPLAY_HEIGHT $ENV{HEIGHT})
endif()

if(DEFINED EOS_QEMU_DISPLAY_WIDTH AND DEFINED EOS_QEMU_DISPLAY_HEIGHT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        EOS_QEMU_DISPLAY_WIDTH=${EOS_QEMU_DISPLAY_WIDTH}
        EOS_QEMU_DISPLAY_HEIGHT=${EOS_QEMU_DISPLAY_HEIGHT}
    )
endif()
