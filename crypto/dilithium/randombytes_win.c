/* Windows-specific RNG using BCryptGenRandom (CNG).
 * Used when building for WIN32 to avoid sys/syscall.h and other Linux-only headers.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "randombytes.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>

void randombytes(uint8_t *out, size_t outlen) {
    NTSTATUS status;
    size_t len;

    while (outlen > 0) {
        len = (outlen > 1048576) ? 1048576 : outlen;
        status = BCryptGenRandom(NULL, out, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (status != 0)
            abort();

        out += len;
        outlen -= len;
    }
}
#endif
