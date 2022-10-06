set(CMAKE_C_STANDARD 99)

set(PUBLIC_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/main/c)

add_library(mirisdr STATIC src/main/c/libmirisdr.c)

target_include_directories(mirisdr PUBLIC ${PUBLIC_SOURCE_DIR})
target_include_directories(mirisdr PRIVATE ${USB_INCLUDE_DIR})

target_compile_options(mirisdr PRIVATE -Dmirisdr_STATIC)
target_compile_options(mirisdr PRIVATE -DSOVERSION="${MAJOR_VERSION}")
target_compile_options(mirisdr PRIVATE -DVERSION="${LIBVER}")

target_link_libraries(mirisdr ${USB_LIBRARY})
