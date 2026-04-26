# src/test/pq/CMakeLists.txt

enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- bundled secp256k1 (build it as part of pq_tests build) ----
# Needed by src/pubkey.cpp: ECDSA, Schnorr/ExtraKeys, ElligatorSwift, and RECOVERY (RecoverCompact).
set(SECP256K1_ENABLE_MODULE_ECDSA           ON  CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_SCHNORRSIG      ON  CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_EXTRAKEYS       ON  CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_ELLIGATORSWIFT  ON  CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_RECOVERY        ON  CACHE BOOL "" FORCE)

set(SECP256K1_BUILD_TESTS            OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXHAUSTIVE_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_BENCHMARK        OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXAMPLES         OFF CACHE BOOL "" FORCE)

# Note: secp256k1 lives in src/secp256k1 (not ./secp256k1)
add_subdirectory(
  ${CMAKE_CURRENT_SOURCE_DIR}/../../secp256k1
  ${CMAKE_BINARY_DIR}/pq_secp256k1
  EXCLUDE_FROM_ALL
)

add_executable(pq_tests
  main.cpp
  dilithium_tests.cpp
  pq_sigopcount_tests.cpp
  pq_sigop_pipeline_tests.cpp
  pq_verifyscript_tests.cpp
  pq_solver_policy_tests.cpp
  pq_sigpolicy_reject_tests.cpp
  pq_witness_activation_tests.cpp

  # Dilithium (C)
  ../../crypto/dilithium/sign.c
  ../../crypto/dilithium/packing.c
  ../../crypto/dilithium/polyvec.c
  ../../crypto/dilithium/poly.c
  ../../crypto/dilithium/symmetric-shake.c
  ../../crypto/dilithium/randombytes.c
  ../../crypto/dilithium/fips202.c
  ../../crypto/dilithium/reduce.c
  ../../crypto/dilithium/ntt.c
  ../../crypto/dilithium/rounding.c

  # Dilithium wrapper
  ../../crypto/dilithium_wrapper.cpp
  ../../crypto/dilithium.cpp

  ../../primitives/transaction.cpp
  ../../script/interpreter.cpp
  ../../script/script.cpp
  ../../script/script_error.cpp
  ../../script/solver.cpp
  ../../script/standard.cpp
  ../../uint256.cpp

  # helpers needed by standard.cpp (Encode/DecodeBase58Check)
  ../../base58.cpp

  # misc
  ../../crypto/siphash.cpp
  ../../policy/feerate.cpp

  ../../consensus/tx_verify.cpp
  ../../coins.cpp
  ../../chain.cpp
  ../../util/moneystr.cpp
  ../../logging.cpp
  ../../random.cpp
  ../../clientversion.cpp
  ../../util/time.cpp
  ../../util/threadnames.cpp
  ../../sync.cpp
  ../../util/fs.cpp
  ../../util/fs_helpers.cpp
  ../../util/syserror.cpp
  ../../randomenv.cpp
  ../../support/cleanse.cpp
)

target_sources(pq_tests PRIVATE
  # crypto
  ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto/sha256.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto/sha256_sse4.cpp   # needed by sha256.cpp on x86
  ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto/sha1.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto/ripemd160.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto/hex_base.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto/sha512.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto/hmac_sha512.cpp

  # util/core
  ${CMAKE_CURRENT_SOURCE_DIR}/../../util/strencodings.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../hash.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../pubkey.cpp
)

target_include_directories(pq_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../..                # src/
  ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto
  ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto/dilithium
  ${CMAKE_CURRENT_SOURCE_DIR}/../../secp256k1/include
  ${PROJECT_BINARY_DIR}                            # bitcoin-build-config.h (top-level CMake binary dir)
)

# link to the actual secp256k1 target (name can differ)
if(TARGET secp256k1)
  target_link_libraries(pq_tests PRIVATE secp256k1)
elseif(TARGET secp256k1_static)
  target_link_libraries(pq_tests PRIVATE secp256k1_static)
else()
  message(FATAL_ERROR "secp256k1 target not found (expected secp256k1 or secp256k1_static)")
endif()

add_test(NAME pq_tests COMMAND pq_tests)

# ---- pq_multisig_tests (standalone PQ multisig test suite) ----
add_executable(pq_multisig_tests
  ../pq_multisig/pq_multisig_tests.cpp

  # Dilithium (C) - same as pq_tests
  ../../crypto/dilithium/sign.c
  ../../crypto/dilithium/packing.c
  ../../crypto/dilithium/polyvec.c
  ../../crypto/dilithium/poly.c
  ../../crypto/dilithium/symmetric-shake.c
  ../../crypto/dilithium/randombytes.c
  ../../crypto/dilithium/fips202.c
  ../../crypto/dilithium/reduce.c
  ../../crypto/dilithium/ntt.c
  ../../crypto/dilithium/rounding.c

  # Dilithium wrapper
  ../../crypto/dilithium_wrapper.cpp
  ../../crypto/dilithium.cpp

  # CORE (same as pq_tests)
  ../../primitives/transaction.cpp
  ../../script/interpreter.cpp
  ../../script/script.cpp
  ../../script/script_error.cpp
  ../../script/solver.cpp
  ../../script/standard.cpp
  ../../uint256.cpp

  # helpers needed by standard.cpp
  ../../base58.cpp

  # misc
  ../../crypto/siphash.cpp
  ../../policy/feerate.cpp
)

target_sources(pq_multisig_tests PRIVATE
  # crypto (same as pq_tests)
  ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto/sha256.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto/sha256_sse4.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto/sha1.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto/ripemd160.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto/hex_base.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto/sha512.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto/hmac_sha512.cpp

  # util/core (same as pq_tests)
  ${CMAKE_CURRENT_SOURCE_DIR}/../../util/strencodings.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../hash.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../pubkey.cpp
)

target_include_directories(pq_multisig_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../..                # src/
  ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto
  ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto/dilithium
  ${CMAKE_CURRENT_SOURCE_DIR}/../../secp256k1/include
  ${PROJECT_BINARY_DIR}                            # bitcoin-build-config.h (top-level CMake binary dir)
)

# link to secp256k1 (reuse same target as pq_tests)
if(TARGET secp256k1)
  target_link_libraries(pq_multisig_tests PRIVATE secp256k1)
elseif(TARGET secp256k1_static)
  target_link_libraries(pq_multisig_tests PRIVATE secp256k1_static)
else()
  message(FATAL_ERROR "secp256k1 target not found (expected secp256k1 or secp256k1_static)")
endif()

add_test(NAME pq_multisig_tests COMMAND pq_multisig_tests)
