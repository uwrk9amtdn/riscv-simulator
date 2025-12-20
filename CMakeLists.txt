cmake_minimum_required(VERSION 3.14)

project(rvsim)

if(CMAKE_C_BYTE_ORDER STREQUAL "BIG_ENDIAN")
    message(FATAL_ERROR "Only little endian architectures supported for now.")
endif()

add_executable(rvsim
    main.cpp
    clint.cpp
    hart.cpp
    mmio.cpp
    plic.cpp
    ram.cpp
    uart.cpp
)

target_compile_features(rvsim PRIVATE cxx_std_17)

add_custom_command(
    OUTPUT soc.dtb
    COMMAND dtc ARGS -o soc.dtb ${CMAKE_CURRENT_SOURCE_DIR}/soc.dts
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/soc.dts
)
add_custom_target(generate_soc_dtb DEPENDS soc.dtb)

add_dependencies(rvsim generate_soc_dtb)

find_library(FDT_LIB fdt)

if (NOT FDT_LIB)
    message(FATAL_ERROR "libfdt not found.")
endif()

target_link_libraries(rvsim ${FDT_LIB})
