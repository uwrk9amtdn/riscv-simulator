cmake_minimum_required(VERSION 3.14)

project(rvsim)

if(CMAKE_C_BYTE_ORDER STREQUAL "BIG_ENDIAN")
    message(FATAL_ERROR "Only little endian architectures supported for now.")
endif()

add_executable(rvsim
    src/main.cpp
    src/mmio.cpp
    src/host_io.cpp
    src/hart/hart.cpp
    src/hart/csr.cpp
    src/hart/op_amo.cpp
    src/hart/op_auipc.cpp
    src/hart/op_branch.cpp
    src/hart/op_imm.cpp
    src/hart/op_jal.cpp
    src/hart/op_jalr.cpp
    src/hart/op_load.cpp
    src/hart/op_lui.cpp
    src/hart/op_misc_mem.cpp
    src/hart/op_op.cpp
    src/hart/op_store.cpp
    src/hart/op_system.cpp
    src/devices/clint.cpp
    src/devices/plic.cpp
    src/devices/ram.cpp
    src/devices/uart.cpp
)

target_compile_features(rvsim PRIVATE cxx_std_20)

# Devirtualized TLB: the hart uses this concrete class directly so lookups inline (+17% measured,
# see notes/performance.md). Set it empty (-DRVSIM_TLB_STATIC=) to select the TLB at runtime with --tlb
set(RVSIM_TLB_STATIC "DirectMappedTLB" CACHE STRING "TLB class compiled into the hart, empty for runtime --tlb")
if (RVSIM_TLB_STATIC)
    target_compile_definitions(rvsim PRIVATE TLB_STATIC=${RVSIM_TLB_STATIC})
endif()

target_include_directories(rvsim PRIVATE src/hart/ src/devices/ src/)

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

find_package(Threads REQUIRED)

target_link_libraries(rvsim ${FDT_LIB} Threads::Threads)
