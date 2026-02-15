# Q-BitX (QBX) — Post-Quantum Bitcoin-style chain (Dilithium / ML-DSA)

Q-BitX (QBX) is a Bitcoin Core–derived blockchain that experiments with post-quantum signatures using Dilithium (ML-DSA) while keeping SHA256d PoW mining.

This repository contains the reference node (qbitx) and CLI (qbitx-cli).

> Status: experimental / testnet-grade. Use at your own risk.

---

## Key features

- SHA256d Proof-of-Work (ASIC/CPU/GPU compatible *for hashing*).
- Post-quantum signatures (Dilithium / ML-DSA) integrated into script validation.
- PQ output types (Dilithium pubkey / pubkeyhash variants).
- RPC helpers for PQ workflow (e.g. pqsendfrom).
- Conservative defaults focused on decentralization and DoS safety.

---

## What this is / what this is not

✅ This is a Bitcoin-style chain that:
- mines blocks with SHA256d
- validates and spends PQ scripts

❌ This is not (yet):
- SegWit / witness-enabled chain (planned/optional future upgrade)
- a production-hardened wallet ecosystem
- audited software

---

## Requirements

### Linux (recommended)
- CMake + Ninja
- C++ compiler toolchain
- Boost, OpenSSL, libevent, sqlite (and usual Bitcoin Core deps)

### Windows
Use WSL2 or build with a proper toolchain (advanced).

---

## Build (Linux)

```bash
git clone
cd <your-repo>

cmake -S . -B build -G Ninja
cmake --build build -j
