# Q-BitX (QBX) — Post-Quantum chain (Dilithium / ML-DSA)

Q-BitX (QBX) is a Bitcoin Core–derived blockchain that experiments with post-quantum signatures using Dilithium (ML-DSA) while keeping SHA256d PoW mining.

This repository contains the reference node (qbitx) and CLI (qbitx-cli).

> Status: mainnet


---

## Key features

 SHA256d Proof-of-Work (ASIC/CPU/GPU compatible *for hashing*).
 Post-quantum signatures (Dilithium / ML-DSA) integrated into script validation.
 PQ output types (Dilithium pubkey / pubkeyhash variants).
 RPC helpers for PQ workflow (e.g. pqsendfrom).
 Conservative defaults focused on decentralization and DoS safety.

---

## What this is / what this is not

 This is a Bitcoin-style chain that:
 mines blocks with SHA256d
 validates and spends PQ scripts

 This is not (yet):
 SegWit / witness-enabled chain (planned/optional future upgrade)
 a production-hardened wallet ecosystem
 audited software

---

## Requirements

### Linux (recommended)
 CMake + Ninja
 C++ compiler toolchain
 Boost, OpenSSL, libevent, sqlite (and usual Bitcoin Core deps)

### Windows
Use WSL2 or build with a proper toolchain (advanced).

---

### Build (Linux)

### Open PowerShell and run:
### On first launch, Ubuntu will ask you to create a UNIX username and password.
wsl --install 
bash 

### Download binaries
 

sudo apt update 
sudo apt install -y unzip wget 
unzip qbitx-linux-x86_64.zip 
chmod +x qbitx qbitx-cli 
apt-get update 
apt-get install -y libevent-2.1-7 
apt-get update 
apt-get install -y libleveldb1d 
apt-get install -y libevent-pthreads-2.1-7t64 

### Create Configuration Directory
mkdir -p ~/.qbitx 


cat > ~/.qbitx/qbitx.conf <<'EOF' 
server=1
daemon=0
txindex=1

rpcbind=127.0.0.1
rpcallowip=127.0.0.1
port=8334
listen=1

rpcuser=qbx 
rpcpassword=qbxpass
printtoconsole=0
maxconnections=32

EOF

### run qbitx
./qbitx 

### If the node is running correctly, you will see the current block height and network status.
./qbitx-cli  getblockchaininfo 

### Create a New Wallet
./qbitx-cli  createwallet "pqwallet" 
./qbitx-cli  listwallets 

### Generate wallet
./qbitx-cli -rpcwallet=pqwallet getnewaddress "" pq 
### balances
./qbitx-cli -rpcwallet=pqwallet getaddressbalances 

### Backup Your Wallet
### The wallet stores a set of individual private keys.
### The backup is wallet.dat with these keys.
### All new addresses created after the backup will be lost

./qbitx-cli -rpcwallet=pqwallet backupwallet ~/.qbitx/backup_$(date +%Y%m%d_%H%M%S) 

### restore wallet for example
./qbitx-cli restorewallet pqwallet /home/sus/wallet/pqwallet 

### For send coins 
### there are three types of fee - low\normal\high
./qbitx-cli -rpcwallet=pqwallet pqsendtoaddress"FROMYOURADDRESS"  "TOADDRESS" 5.0 normal 
