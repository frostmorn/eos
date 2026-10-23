file(GLOB_RECURSE EOS_SRCS "*.c")

idf_component_register(
    SRCS ${EOS_SRCS}
    INCLUDE_DIRS "include_override" "."
    WHOLE_ARCHIVE 
    LDFRAGMENTS "eld/driver.lf"
    LDFRAGMENTS "eld/bin.lf"
)
idf_build_set_property(
    COMPILE_OPTIONS "-DFD_SETSIZE=128" APPEND
)

target_link_libraries(
${COMPONENT_LIB} INTERFACE
"-Wl,--wrap=_open_r"
"-Wl,--wrap=_close_r"
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
