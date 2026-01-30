# Q-BitX Quickstart Guide

This guide shows you how to quickly get started with Q-BitX on regtest (local testing network) without needing to specify `-datadir` for normal usage.

## Prerequisites

- Q-BitX node and CLI tools built and in your PATH
- Default data directory: `~/.bitcoin` (Linux/macOS) or `%LOCALAPPDATA%\Bitcoin\` (Windows)

## Setup

### 1. Create Default Data Directory and Config

```bash
mkdir -p ~/.bitcoin
cat > ~/.bitcoin/bitcoin.conf <<'EOF'
regtest=1
server=1
rpcbind=127.0.0.1
rpcallowip=127.0.0.1
rpcport=18444
port=18445
listen=1
EOF
```

**Note:** The config file `~/.bitcoin/bitcoin.conf` is automatically read by Q-BitX. You don't need to specify `-datadir` unless you want to use a different location.

### 2. Start the Node

Start Q-BitX in the foreground:

```bash
./qbitx
```

The node will start and connect to the regtest network. You'll see log output in the terminal.

**Note:** In WSL (Windows Subsystem for Linux), the `-daemon` option may not work properly. For production use on Linux, daemon support is available.

To stop the node, open another terminal and run:

```bash
./qbitx-cli stop
```

## Wallet Operations

### 3. Create a Wallet

Create a descriptor wallet (recommended):

```bash
./qbitx-cli -named createwallet wallet_name="pqwallet" descriptors=true
```

List all wallets:

```bash
./qbitx-cli listwallets
```

Check wallet info:

```bash
./qbitx-cli -rpcwallet=pqwallet getwalletinfo
```

### 4. Get a Post-Quantum (PQ) Address

Generate a new Dilithium address:

```bash
./qbitx-cli -rpcwallet=pqwallet getnewaddress "" "pq"
```

**Note:** `"pq"` is an alias for `"dilithium-legacy"`. The address type `"dilithium-bech32"` (alias: `"pq-bech32"`) may not be fully supported yet.

Verify the address:

```bash
./qbitx-cli -rpcwallet=pqwallet getaddressinfo "<ADDR>"
```

Replace `<ADDR>` with the address returned from `getnewaddress`. You should see:
- `"ismine": true`
- `"solvable": true` (if wallet has the key)
- `"type": "dilithium_pubkeyhash"`

### 5. Mine Coins on Regtest

Mine 101 blocks to your PQ address to get spendable coins:

```bash
./qbitx-cli -rpcwallet=pqwallet -named generatetoaddress nblocks=101 address="<MINER_PQ_ADDR>"
```

Replace `<MINER_PQ_ADDR>` with your PQ address from step 4.

**Why 101 blocks?** The first 100 blocks are coinbase maturity blocks. After 101 blocks, the coins become spendable.

### 6. Check Balance and UTXOs

Check your wallet balance:

```bash
./qbitx-cli -rpcwallet=pqwallet getbalances
```

List unspent transaction outputs (UTXOs):

```bash
./qbitx-cli -rpcwallet=pqwallet listunspent 1
```

The `1` parameter shows UTXOs with at least 1 confirmation.

### 7. Backup Your Wallet

Always backup your wallet before sending transactions:

```bash
./qbitx-cli -rpcwallet=pqwallet backupwallet ~/pqwallet.bak
```

This creates a backup file at `~/pqwallet.bak`. Store this file securely!

## Sending PQ Coins

### 8. Send PQ Coins Using pqsendfrom

The `pqsendfrom` RPC sends PQ coins from one wallet address to another:

```bash
./qbitx-cli -rpcwallet=pqwallet pqsendfrom "<FROM_ADDR>" "<TO_ADDR>" 0.1 "normal"
```

Replace:
- `<FROM_ADDR>` with your source PQ address (must belong to the wallet)
- `<TO_ADDR>` with the destination PQ address
- `0.1` with the amount to send (in QBX)
- `"normal"` with fee policy: `"low"` (1 sat/vB), `"normal"` (5 sat/vB), or `"high"` (15 sat/vB)

**Note:** `pqsendfrom` is only available in regtest mode.

### 9. Alternative: Send Using pqsendto

The `pqsendto` RPC sends PQ coins from a specific UTXO without using wallet storage:

```bash
./qbitx-cli pqsendto "<TXID>" 0 "<SCRIPTCODE_HEX>" "<PUBKEY_HEX>" "<PRIVKEY_HEX>" "<TO_ADDR>" 1.0 0.0001
```

This requires:
- Transaction ID (txid) of the UTXO
- Output index (vout)
- Script code hex
- Public and private key hex values
- Destination address
- Amount and fee

**Note:** `pqsendto` is only available in regtest mode and requires manual key management.

### 10. Confirm Transactions

After sending, check the transaction status:

```bash
./qbitx-cli -rpcwallet=pqwallet gettransaction "<TXID>"
```

Replace `<TXID>` with the transaction ID returned from `pqsendfrom` or `pqsendto`.

To confirm the transaction in a block, mine one more block:

```bash
./qbitx-cli -rpcwallet=pqwallet -named generatetoaddress nblocks=1 address="<MINER_PQ_ADDR>"
```

## Common Commands Summary

```bash
# Start/stop node
./qbitx
./qbitx-cli stop

# Wallet management
./qbitx-cli -named createwallet wallet_name="pqwallet" descriptors=true
./qbitx-cli listwallets
./qbitx-cli -rpcwallet=pqwallet getwalletinfo

# Addresses
./qbitx-cli -rpcwallet=pqwallet getnewaddress "" "pq"
./qbitx-cli -rpcwallet=pqwallet getaddressinfo "<ADDR>"

# Mining
./qbitx-cli -rpcwallet=pqwallet -named generatetoaddress nblocks=101 address="<ADDR>"

# Balance and UTXOs
./qbitx-cli -rpcwallet=pqwallet getbalances
./qbitx-cli -rpcwallet=pqwallet listunspent 1

# Backup
./qbitx-cli -rpcwallet=pqwallet backupwallet ~/pqwallet.bak

# Send PQ coins
./qbitx-cli -rpcwallet=pqwallet pqsendfrom "<FROM>" "<TO>" 0.1 "normal"

# Transaction info
./qbitx-cli -rpcwallet=pqwallet gettransaction "<TXID>"
```

## Troubleshooting

- **"Error: Wallet file verification failed"**: Make sure the wallet exists and you're using the correct wallet name.
- **"Error: Insufficient funds"**: Mine more blocks to your address or check your balance with `getbalances`.
- **"pqsendfrom is for regression testing (-regtest mode) only"**: Make sure `regtest=1` is set in your `bitcoin.conf`.
- **"Dilithium private key not found"**: Ensure the address was generated in this wallet using `getnewaddress` with `"pq"` type.

## Next Steps

- Read the full [Bitcoin Core documentation](https://bitcoincore.org/en/doc/) for advanced features
- Explore [descriptor wallets](doc/descriptors.md) for more wallet functionality
- Check [JSON-RPC interface documentation](doc/JSON-RPC-interface.md) for all available RPC methods
