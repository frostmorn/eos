file(GLOB_RECURSE EOS_SRCS "*.c")

idf_component_register(WHOLE_ARCHIVE 
    SRCS ${EOS_SRCS}
    INCLUDE_DIRS "."
)
