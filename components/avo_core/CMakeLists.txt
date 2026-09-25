file(GLOB SRCS "src/*.c")
idf_component_register(SRCS ${SRCS} INCLUDE_DIRS "include")
