get_filename_component(FM6_BENCH_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

idf_component_register(
    SRCS "${FM6_BENCH_ROOT}/bench/target_runtime_esp32s3.c"
    INCLUDE_DIRS
        "${FM6_BENCH_ROOT}/bench"
        "${FM6_BENCH_ROOT}/generated"
        "${FM6_BENCH_ROOT}/upstream/Felucca/firmware/src"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -fno-builtin)
