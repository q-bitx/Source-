# Applying cpuminer-multi Q-BitX PQ Patch

This guide explains how to apply the patch to cpuminer-multi source code.

## Prerequisites

- cpuminer-multi source code (tpruvot/cpuminer)
- Basic knowledge of C programming
- Build tools (gcc, make, autotools)

## Step-by-Step Application

### Step 1: Locate the Files to Modify

Search for the following patterns in cpuminer-multi source:

```bash
# Find where coinbase script is built
grep -r "0x76.*0xa9.*0x14" .
grep -r "0xac\|OP_CHECKSIG" .
grep -r "coinbase-addr\|coinbase_addr" .

# Find option parsing
grep -r "getopt_long\|struct option" .
grep -r "coinbase-addr.*required_argument" .
```

### Step 2: Add the Flag Variable

**File:** `cpu-miner.c` (or main file)

**Find:** Where other option variables are declared (e.g., `static char *opt_coinbase_addr = NULL;`)

**Add:**
```c
static bool opt_qbitx_pq = false;
```

### Step 3: Add Option to Options Array

**File:** `cpu-miner.c`

**Find:** The `static struct option options[]` array

**Find:** The line with `{ "coinbase-addr", required_argument, 0, 'C' },`

**Add after it:**
```c
{ "qbitx-pq", no_argument, 0, 1000 },
```

### Step 4: Handle the Option in Switch Statement

**File:** `cpu-miner.c`

**Find:** The switch statement that handles option parsing (usually `case 'C':` for coinbase-addr)

**Add:**
```c
case 1000:
	opt_qbitx_pq = true;
	break;
```

### Step 5: Export Flag (if needed)

**File:** `miner.h` or appropriate header

**If the script building function is in a different file, add:**
```c
extern bool opt_qbitx_pq;
```

**And in the implementation file (util.c or wherever script is built), change:**
```c
// If you declared it as static, remove static:
bool opt_qbitx_pq = false;  // Remove 'static' keyword
```

### Step 6: Modify Script Building Function

**File:** `util.c` or wherever coinbase script is built

**Search for:** Function that builds P2PKH script. Look for:
- `0x76, 0xa9, 0x14` (OP_DUP OP_HASH160 push20)
- `0x88, 0xac` (OP_EQUALVERIFY OP_CHECKSIG)
- Pattern: `script[24] = 0xac;` or similar

**Example function might look like:**
```c
static void address_to_script(unsigned char *script, int *script_len, const char *addr)
{
	unsigned char pubkeyhash[20];
	// ... decode base58 and extract pubkeyhash ...
	
	script[0] = 0x76;  // OP_DUP
	script[1] = 0xa9;  // OP_HASH160
	script[2] = 0x14;  // Push 20 bytes
	memcpy(&script[3], pubkeyhash, 20);
	script[23] = 0x88;  // OP_EQUALVERIFY
	script[24] = 0xac;  // OP_CHECKSIG  <-- CHANGE THIS LINE
	*script_len = 25;
}
```

**Change the last assignment to:**
```c
	script[24] = opt_qbitx_pq ? 0xc4 : 0xac;  // OP_CHECKSIGDILITHIUM or OP_CHECKSIG
```

**Make sure `opt_qbitx_pq` is accessible:**
- If function is in same file: it should work
- If function is in different file: declare `extern bool opt_qbitx_pq;` at top of that file

### Step 7: Update Help Text

**File:** `help.h` or wherever help text is defined

**Find:** Where `--coinbase-addr` help text is

**Add nearby:**
```c
"  --qbitx-pq              Use Q-BitX PQ address format (OP_CHECKSIGDILITHIUM) for coinbase\n"
"                          Requires --coinbase-addr to be set\n"
```

### Step 8: Build and Test

```bash
./autogen.sh
./configure
make

# Test
./cpuminer --qbitx-pq --coinbase-addr <PQ_ADDRESS> --algo sha256d --url http://localhost:8332 --user test --pass test --debug
```

### Step 9: Verify the Script

To verify the coinbase script is correct, you can:

1. **Check the script bytes:**
   - Look for coinbase transaction in debug output
   - Verify scriptPubKey ends with `88c4` (OP_EQUALVERIFY OP_CHECKSIGDILITHIUM)

2. **Test with Q-BitX node:**
   - Submit a block using cpuminer
   - Verify it's accepted (no 'bad-cb-pq' error)

## Common Issues

### Issue: `opt_qbitx_pq` undeclared
**Solution:** Make sure you declared it in the file where it's used, or add `extern bool opt_qbitx_pq;` declaration.

### Issue: Script still ends with 0xac
**Solution:** Make sure you found the correct function that builds the coinbase script. There might be multiple places.

### Issue: Build fails
**Solution:** Make sure you included `<stdbool.h>` if using `bool` type, or use `int` instead:
```c
static int opt_qbitx_pq = 0;
// ...
case 1000:
	opt_qbitx_pq = 1;
	break;
// ...
script[24] = opt_qbitx_pq ? 0xc4 : 0xac;
```

## Verification Checklist

- [ ] `--qbitx-pq` flag appears in `--help` output
- [ ] Flag can be set without errors
- [ ] Coinbase scriptPubKey ends with `0xc4` when flag is set
- [ ] Coinbase scriptPubKey ends with `0xac` when flag is NOT set (backward compatibility)
- [ ] Blocks are accepted by Q-BitX node (no 'bad-cb-pq' reject)
