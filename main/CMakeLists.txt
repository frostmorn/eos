file(GLOB_RECURSE EOS_SRCS "*.c")

idf_component_register(
    SRCS ${EOS_SRCS}
    INCLUDE_DIRS "."
    WHOLE_ARCHIVE 
    LDFRAGMENTS "eld/driver.lf"
    LDFRAGMENTS "eld/app.lf"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=override-init
)