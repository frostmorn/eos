file(GLOB_RECURSE EOS_SRCS "*.c")

idf_component_register(
    SRCS ${EOS_SRCS}
    INCLUDE_DIRS "."
    WHOLE_ARCHIVE 
    LDFRAGMENTS "driver/driver.lf"
)
