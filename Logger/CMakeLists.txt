add_library(fluxdrop_logger STATIC
    src/logger.cpp
)

target_include_directories(fluxdrop_logger PUBLIC
    include
)
