add_library(pqcrystals STATIC
    poly.c
    sign.c
    randombytes.c
    polyvec.c
    reduce.c
    rounding.c
    fips202.c
    symmetric-shake.c
    packing.c
    ntt.c
)
target_include_directories(pqcrystals PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(pqcrystals PRIVATE DILITHIUM_MODE=3)
