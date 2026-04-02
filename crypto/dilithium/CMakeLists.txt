add_library(pqcrystals STATIC
    poly.c
    sign.c
    $<$<NOT:$<BOOL:${WIN32}>>:randombytes.c>
    $<$<BOOL:${WIN32}>:randombytes_win.c>
    polyvec.c
    reduce.c
    rounding.c
    fips202.c
    symmetric-shake.c
    packing.c
    ntt.c
)
target_include_directories(pqcrystals PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
  target_link_libraries(pqcrystals PRIVATE bcrypt)
endif()

target_compile_definitions(pqcrystals PRIVATE DILITHIUM_MODE=3)
