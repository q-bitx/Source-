# ⚛️ Q-BitX (QBX)

**Q-BitX (QBX)** is a Bitcoin Core–derived blockchain focused on post-quantum transaction signatures.

It keeps classic **SHA256d Proof-of-Work** mining while adding **Dilithium / ML-DSA post-quantum signatures** into script validation and wallet/RPC workflows.

| Parameter           | Value                                       |
| ------------------- | ------------------------------------------- |
| **Network status**  | Mainnet                                     |
| **Consensus model** | SHA256d PoW + Dilithium / ML-DSA signatures |
| **Supply**          | 21,000,000 QBX                              |
| **Premine**         | No premine                                  |



---

## ⚠️ Mandatory upgrade notice

Q-BitX includes scheduled consensus activations.

All node operators, pools, exchanges and service providers should upgrade before the activation heights below:

|     Height | Activation                                                                      |
| ---------: | ------------------------------------------------------------------------------- |
| **200001** | LWMA difficulty adjustment                                                      |
| **220000** | BIP34 / BIP65 / BIP66 / CSV / SegWit baseline rules + 8M block limits           |
| **230000** | PQ sigops + native PQ witness activation                                        |
| **709632** | Taproot minimum activation height, currently inherited and inactive in practice |

Old nodes may reject, mis-handle or fail to relay blocks after these heights.

---

## ✨ Key features

* 🔐 **Post-quantum signatures** using Dilithium / ML-DSA.
* ⛏️ **SHA256d Proof-of-Work** mining.
* 🧱 **Bitcoin Core–derived validation engine**.
* 🧬 **PQ script validation** with Q-BitX-specific opcodes.
* 👛 **Wallet/RPC helpers** for PQ address and spending workflows.
* 🧾 **Native PQ witness support** scheduled at height `230000`.
* 🧩 **No premine**, fixed 21M supply model.
* 🛡️ Conservative network upgrades focused on consensus safety.

---

## What Q-BitX is

Q-BitX is a blockchain that:

* mines blocks using SHA256d;
* validates transactions using Bitcoin-style consensus logic;
* supports post-quantum Dilithium / ML-DSA signatures;
* Replacement for audited cryptographic standards;
* PQ output types and native PQ witness transactions.

---

## What Q-BitX is not

Q-BitX is not:

* a “quantum mining” chain;
* a centralized custody wallet;
* a token running on another blockchain;
* financial advice;
* a promise of absolute security without proper wallet backups and node upgrades.

Q-BitX is a standalone mainnet blockchain with post-quantum signature support built into transaction validation.

Users, miners, pools and exchanges should keep nodes updated, monitor activation heights and always back up wallets.


---

## 📦 Downloads

Latest release:

```text
https://github.com/q-bitx/Source-/releases/latest
```

Typical release files:

```text
qbitx-linux-x86_64.zip
qbitx-windows-x86_64.zip
qbitx.exe
qbitx-cli.exe
qbitx-gui.exe
```

---

## 🐧 Quick start: Linux binary

### 1. Install basic dependencies

```bash
sudo apt update
sudo apt install -y unzip wget libevent-2.1-7 libevent-pthreads-2.1-7t64 libleveldb1d
```

On some Ubuntu versions, package names may differ slightly. If a package is unavailable, install the closest available `libevent` and `leveldb` runtime packages.

### 2. Download and unpack

```bash
wget https://github.com/q-bitx/Source-/releases/latest/download/qbitx-linux-x86_64.zip
unzip qbitx-linux-x86_64.zip
chmod +x qbitx qbitx-cli
```

### 3. Create config

```bash
mkdir -p ~/.qbitx

cat > ~/.qbitx/qbitx.conf <<'EOF'
server=1
daemon=1
txindex=1

# RPC 
rpcbind=127.0.0.1
rpcallowip=127.0.0.1
rpcuser=qbx
rpcpassword=qbxpass

# P2P
listen=1
discover=1
port=8334
maxconnections=64

# Logs
printtoconsole=0

EOF
```

### 4. Start node

```bash
./qbitx
```

In another terminal:

```bash
./qbitx-cli getblockchaininfo
```

---

## 🪟 Quick start: Windows

Download the Windows release package from:

```text
https://github.com/q-bitx/Source-/releases/latest
```

Typical files:

```text
qbitx.exe
qbitx-cli.exe
qbitx-gui.exe
```

PowerShell example:

```powershell
.\qbitx.exe --version
.\qbitx-cli.exe --version
```

Default data directory:

```text
%APPDATA%\QBitX
```

Example config path:

```text
%APPDATA%\QBitX\qbitx.conf
```

---

## 👛 Wallet usage

### Create wallet

```bash
./qbitx-cli createwallet "pqwallet"
./qbitx-cli listwallets
```

### Generate a PQ address

```bash
./qbitx-cli -rpcwallet=pqwallet getnewaddress "" pq
```

Depending on wallet settings and network rules, Q-BitX may return PQ-style addresses by default.

### Check balances

```bash
./qbitx-cli -rpcwallet=pqwallet getbalances
./qbitx-cli -rpcwallet=pqwallet getaddressbalances
```

### Send QBX with PQ helper

Fee level examples:

```text
low
normal
high
```

Example:

```bash
./qbitx-cli -rpcwallet=pqwallet pqsendtoaddress "FROM_ADDRESS" "TO_ADDRESS" 5.0 normal
```

---

## 💾 Wallet backup

Always back up your wallet before receiving or mining coins.

```bash
./qbitx-cli -rpcwallet=pqwallet backupwallet ~/.qbitx/backup_$(date +%Y%m%d_%H%M%S)
```

Important:

* The wallet contains private keys.
* If you create new addresses after a backup, make a new backup.
* Losing `wallet.dat` / wallet database files may permanently lose access to funds.
* Never share your wallet backup or private keys.

### Restore wallet example

```bash
./qbitx-cli restorewallet pqwallet /path/to/your/wallet_backup
```

---

## 🛠️ Build from source: Linux

### 1. Install build dependencies

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  pkg-config \
  python3 \
  git \
  libevent-dev \
  libboost-dev \
  libboost-system-dev \
  libboost-filesystem-dev \
  libboost-thread-dev \
  libsqlite3-dev \
  libssl-dev \
  libminiupnpc-dev \
  libnatpmp-dev
```

### 2. Clone repository

```bash
git clone https://github.com/q-bitx/Source-.git
cd Source-
```

### 3. Build node and CLI

```bash
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target qbitx qbitx-cli -j$(nproc)
```

Binaries:

```bash
./build/qbitx --version
./build/qbitx-cli --version
```

---

## 🧪 Tests

PQ-focused tests:

```bash
cmake --build build --target pq_tests -j$(nproc)
./build/src/test/pq/pq_tests
```

Expected result:

```text
No errors detected
```

Some inherited Bitcoin Core tests may require additional build options and are not part of the default Q-BitX test path.

---

## ⛏️ Mining notes

Q-BitX uses SHA256d Proof-of-Work.

This means the hashing algorithm is Bitcoin-like, but Q-BitX has its own chain, consensus parameters, addresses and PQ transaction rules.

Mining software and pools must follow current Q-BitX consensus rules, especially around:

* height `220000` block limit / buried rule activation;
* height `230000` PQ sigops and PQ witness activation;
* transaction relay and block template rules.

---

## 🔐 Post-quantum design notes

Q-BitX is built around a post-quantum transaction signature layer.

Dilithium / ML-DSA signatures are integrated into script validation, enabling PQ-protected transaction outputs and spends while keeping the familiar Bitcoin-style UTXO model and SHA256d Proof-of-Work mining.

Important distinction:

```text
SHA256d PoW protects block production and chain work.
Dilithium / ML-DSA protects transaction ownership and spend authorization.
```

Q-BitX does not use “quantum mining”. The post-quantum design applies to signatures, wallet keys and transaction validation.

Consensus upgrades are activated by predefined block heights to keep network transitions predictable for nodes, miners, pools and exchanges.


---

## 🌐 Network ports

Default P2P port:

```text
8334
```

Default RPC is local-only unless explicitly configured otherwise.

Recommended RPC security:

```text
rpcbind=127.0.0.1
rpcallowip=127.0.0.1
```

Do not expose RPC publicly without proper firewalling, authentication and operational security.

---

## 🧭 Useful RPC commands

```bash
./qbitx-cli getblockchaininfo
./qbitx-cli getnetworkinfo
./qbitx-cli getpeerinfo
./qbitx-cli getwalletinfo
./qbitx-cli listwallets
./qbitx-cli -rpcwallet=pqwallet getnewaddress "" pq
./qbitx-cli -rpcwallet=pqwallet getbalances
```

---

## ⚠️ Security warning

Q-BitX is experimental blockchain software.

Before running it in production-like environments:

* back up wallets;
* test upgrades on a separate node;
* monitor consensus activation heights;
* keep RPC private;
* verify binaries and releases;
* upgrade before mandatory activation heights.

---

## 📄 License

Distributed under the MIT software license.

See `COPYING` for details.

---

## 🤝 Contributing

Issues, testing reports, build fixes and code reviews are welcome.

Focus areas:

* PQ wallet UX;
* mining and pool compatibility;
* relay / mempool testing;
* Windows wallet packaging;
* documentation;
* post-quantum transaction workflows.
