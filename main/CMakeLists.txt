file(GLOB_RECURSE EOS_SRCS "*.c")

idf_component_register(
    SRCS ${EOS_SRCS}
    INCLUDE_DIRS "."
    WHOLE_ARCHIVE 
    LDFRAGMENTS "eld/driver.lf"
    LDFRAGMENTS "eld/app.lf"
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
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=override-init
)
