file(GLOB SRCS "src/*.c" "assets/fonts/*.c" "assets/img/*.c")
idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES avo_core avo_hal)
