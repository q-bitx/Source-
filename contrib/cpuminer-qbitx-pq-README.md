# cpuminer-multi Q-BitX PQ Address Support Patch

This patch adds support for Q-BitX Post-Quantum (PQ) addresses to cpuminer-multi (tpruvot).

## Problem

cpuminer-multi builds coinbase scriptPubKey for `--coinbase-addr` as Bitcoin P2PKH:
```
76 a9 14 <20-byte-hash> 88 ac (OP_CHECKSIG = 0xAC)
```

But Q-BitX PQ P2PKH requires:
```
76 a9 14 <20-byte-hash> 88 c4 (OP_CHECKSIGDILITHIUM = 0xC4)
```

## Solution

Add a new command-line flag `--qbitx-pq` that changes the coinbase output script to use `OP_CHECKSIGDILITHIUM` (0xC4) instead of `OP_CHECKSIG` (0xAC).

## How to Find the Code to Patch

1. **Search for coinbase script building:**
   ```bash
   grep -r "0x76.*0xa9.*0x14" cpuminer-multi/
   grep -r "OP_DUP.*OP_HASH160" cpuminer-multi/
   grep -r "0xac\|OP_CHECKSIG" cpuminer-multi/
   grep -r "coinbase-addr\|coinbase_addr\|COINBASE_ADDR" cpuminer-multi/
   ```

2. **Look for functions like:**
   - `address_to_script()`
   - `build_coinbase()`
   - `create_coinbase_tx()`
   - Functions that handle `--coinbase-addr` option

3. **Find where command-line options are parsed:**
   - Usually in `cpu-miner.c` or `main.c`
   - Look for `getopt_long()` or similar parsing code
   - Find the `options[]` array with `{ "coinbase-addr", ... }`

## Changes Required

### 1. Add flag variable and option

**Location:** `cpu-miner.c` (or main file with option parsing)

**Add global variable:**
```c
static bool opt_qbitx_pq = false;
```

**Add to options array (find where `{ "coinbase-addr", ... }` is):**
```c
static struct option options[] = {
    // ... existing options ...
    { "coinbase-addr", required_argument, 0, 'C' },
    { "qbitx-pq", no_argument, 0, 1000 },  // Add this line
    // ... rest of options ...
};
```

**Handle in option parsing switch statement:**
```c
case 1000:  // Add this case
    opt_qbitx_pq = true;
    break;
```

### 2. Export flag in header (if needed)

**Location:** `miner.h` or appropriate header file

**Add:**
```c
extern bool opt_qbitx_pq;
```

**And in the implementation file (cpu-miner.c or util.c):**
```c
bool opt_qbitx_pq = false;  // Remove 'static' if declared extern
```

### 3. Modify script building function

**Location:** `util.c` or wherever coinbase script is built

**Find the function that builds P2PKH script from address. It typically looks like:**
```c
static void address_to_script(unsigned char *script, int *script_len, const char *addr)
{
    unsigned char pubkeyhash[20];
    // ... decode base58 address and extract pubkeyhash ...
    
    // Build P2PKH script: OP_DUP OP_HASH160 <20-byte> OP_EQUALVERIFY OP_CHECKSIG
    script[0] = 0x76;  // OP_DUP
    script[1] = 0xa9;  // OP_HASH160
    script[2] = 0x14;  // Push 20 bytes
    memcpy(&script[3], pubkeyhash, 20);
    script[23] = 0x88;  // OP_EQUALVERIFY
    script[24] = 0xac;  // OP_CHECKSIG - CHANGE THIS LINE
    *script_len = 25;
}
```

**Change the last line to:**
```c
    script[24] = opt_qbitx_pq ? 0xc4 : 0xac;  // OP_CHECKSIGDILITHIUM (0xC4) or OP_CHECKSIG (0xAC)
```

**Make sure `opt_qbitx_pq` is accessible in this file** (either declare `extern bool opt_qbitx_pq;` or remove `static` from the variable declaration).

### 4. Update help text

**Location:** `help.h` or wherever help text is printed

**Find where `--coinbase-addr` help text is and add nearby:**
```c
"  --qbitx-pq              Use Q-BitX PQ address format (OP_CHECKSIGDILITHIUM) for coinbase\n"
"                          Requires --coinbase-addr to be set\n"
```

## Usage

```bash
./cpuminer --qbitx-pq --coinbase-addr <PQ_ADDRESS> --algo sha256d --url http://localhost:8332 --user <user> --pass <pass>
```

## Testing

After applying the patch:
1. Build cpuminer-multi
2. Run with `--qbitx-pq --coinbase-addr <PQ_ADDRESS>`
3. Verify the coinbase scriptPubKey ends with `0xc4` (OP_CHECKSIGDILITHIUM)
4. Submit a block and verify it's accepted (no 'bad-cb-pq' reject)

## Notes

- The flag only affects the coinbase scriptPubKey when `--coinbase-addr` is used
- Address parsing remains unchanged - still extracts 20-byte pubkeyhash from base58
- When flag is not set, behavior is unchanged (uses OP_CHECKSIG = 0xAC)
- The script format is: `OP_DUP OP_HASH160 <20-byte-hash> OP_EQUALVERIFY OP_CHECKSIGDILITHIUM`
