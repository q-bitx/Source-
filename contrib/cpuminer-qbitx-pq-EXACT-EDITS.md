# Exact Code Edits for cpuminer-multi Q-BitX PQ Support

This document provides the exact code changes needed in `/home/sus/cpuminer-multi/cpu-miner.c`.

## Summary of Changes

1. Add global flag variable `opt_qbitx_pq`
2. Add option entry `{ "qbitx-pq", 0, NULL, 1027 }` after `--coinbase-addr`
3. Handle case `1027` in switch statement
4. Update help text
5. Modify script building code to use conditional opcode

## File: cpu-miner.c

### Edit 1: Add Global Flag Variable

**Location:** Near the top of the file, where other static option variables are declared (search for `static char *opt_coinbase_addr` or similar)

**Add this line:**
```c
static bool opt_qbitx_pq = false;
```

**Note:** If the file doesn't include `<stdbool.h>` and you get compilation errors, use `int` instead:
```c
static int opt_qbitx_pq = 0;
```

### Edit 2: Add Option Entry

**Location:** Around line 456, in the `static struct option options[]` array

**Find this line:**
```c
{ "coinbase-addr", 1, NULL, 1016 },
```

**Add immediately after it:**
```c
{ "qbitx-pq", 0, NULL, 1027 },
```

**Complete section should look like:**
```c
{ "coinbase-addr", 1, NULL, 1016 },
{ "qbitx-pq", 0, NULL, 1027 },
{ "config", required_argument, 0, 'c' },
```

### Edit 3: Handle Option in Switch Statement

**Location:** Around line 3264, in the switch statement that handles option codes

**Find this section:**
```c
case 1016:  // --coinbase-addr
	opt_coinbase_addr = strdup(optarg);
	break;
```

**Add immediately after it:**
```c
case 1027:  // --qbitx-pq
	opt_qbitx_pq = true;  // or opt_qbitx_pq = 1; if using int
	break;
```

**Complete section should look like:**
```c
case 1016:  // --coinbase-addr
	opt_coinbase_addr = strdup(optarg);
	break;
case 1027:  // --qbitx-pq
	opt_qbitx_pq = true;
	break;
```

### Edit 4: Update Help Text

**Location:** In the function that prints help text (search for where `--coinbase-addr` help is printed, usually in `show_usage()` or `print_usage()`)

**Find:** Where `--coinbase-addr` help text is printed (search for `"coinbase-addr"` in help strings)

**Add this line nearby (typically after `--coinbase-addr` help):**
```c
"  --qbitx-pq    build PQ coinbase script for Q-BitX (use 0xc4 instead of 0xac)\n"
```

### Edit 5: Modify Script Building Code

**Location:** Find the function that builds P2PKH script from coinbase address

**Search for one of these patterns:**
```bash
grep -n "script\[24\].*0xac" cpu-miner.c
grep -n "0x76.*0xa9.*0x14" cpu-miner.c
grep -n "OP_CHECKSIG\|0xac" cpu-miner.c | grep -i script
```

**The code should look something like:**
```c
script[0] = 0x76;  // OP_DUP
script[1] = 0xa9;  // OP_HASH160
script[2] = 0x14;  // Push 20 bytes
memcpy(&script[3], pubkeyhash, 20);
script[23] = 0x88;  // OP_EQUALVERIFY
script[24] = 0xac;  // OP_CHECKSIG
```

**Change the last two lines to:**
```c
script[23] = 0x88;  // OP_EQUALVERIFY
// Q-BitX PQ P2PKH requires OP_CHECKSIGDILITHIUM (0xC4) instead of OP_CHECKSIG (0xAC)
script[24] = opt_qbitx_pq ? 0xc4 : 0xac;  // Use 0xC4 for Q-BitX PQ mode, 0xAC for standard Bitcoin
```

**If using `int` instead of `bool`:**
```c
script[24] = opt_qbitx_pq ? 0xc4 : 0xac;
```

**Make sure `opt_qbitx_pq` is accessible:**
- If this function is in `cpu-miner.c`, the variable should be accessible since it's declared `static` at file scope
- If this function is in a different file (e.g., `util.c`), you need to either:
  - Remove `static` from the declaration and add `extern bool opt_qbitx_pq;` in the other file, OR
  - Pass the flag as a parameter to the function

## Complete Code Snippets

### Snippet 1: Variable Declaration (near top of cpu-miner.c)
```c
static bool opt_qbitx_pq = false;
```

### Snippet 2: Option Array (around line 456)
```c
{ "coinbase-addr", 1, NULL, 1016 },
{ "qbitx-pq", 0, NULL, 1027 },
```

### Snippet 3: Switch Case (around line 3264)
```c
case 1016:  // --coinbase-addr
	opt_coinbase_addr = strdup(optarg);
	break;
case 1027:  // --qbitx-pq
	opt_qbitx_pq = true;
	break;
```

### Snippet 4: Help Text
```c
"  --qbitx-pq    build PQ coinbase script for Q-BitX (use 0xc4 instead of 0xac)\n"
```

### Snippet 5: Script Building (where script[24] is set)
```c
script[23] = 0x88;  // OP_EQUALVERIFY
// Q-BitX PQ P2PKH requires OP_CHECKSIGDILITHIUM (0xC4) instead of OP_CHECKSIG (0xAC)
script[24] = opt_qbitx_pq ? 0xc4 : 0xac;  // Use 0xC4 for Q-BitX PQ mode, 0xAC for standard Bitcoin
```

## Verification Steps

1. **Build:**
   ```bash
   cd /home/sus/cpuminer-multi
   make clean
   make
   ```

2. **Check help:**
   ```bash
   ./cpuminer --help | grep -A1 -B1 qbitx-pq
   ```

3. **Test with Q-BitX:**
   ```bash
   ./cpuminer -a sha256d -o http://127.0.0.1:8332 -u user -p pass --coinbase-addr=<PQADDR> --qbitx-pq -t 4
   ```

4. **Verify script bytes:** The coinbase scriptPubKey should end with `88c4` (OP_EQUALVERIFY OP_CHECKSIGDILITHIUM) instead of `88ac`.

## Troubleshooting

### Compilation Error: `bool` undeclared
**Solution:** Add `#include <stdbool.h>` at the top of `cpu-miner.c`, or use `int` instead:
```c
static int opt_qbitx_pq = 0;
// ...
case 1027:
	opt_qbitx_pq = 1;
	break;
// ...
script[24] = opt_qbitx_pq ? 0xc4 : 0xac;
```

### Variable not accessible in script building function
**Solution:** If the script building function is in a different file:
1. Remove `static` from the declaration: `bool opt_qbitx_pq = false;`
2. Add `extern bool opt_qbitx_pq;` at the top of the file containing the script building function

### Script still ends with 0xac
**Solution:** Make sure you found the correct function. There might be multiple places where scripts are built. Search more thoroughly:
```bash
grep -rn "script\[24\]" .
grep -rn "0xac" . | grep -i script
```
