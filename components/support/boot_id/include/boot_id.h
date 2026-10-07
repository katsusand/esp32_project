#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * An identifier for this boot: a random UUID (version 4 form), made once per
 * boot and the same for every caller until the next reset.
 *
 * English contract: it tells which log files and lines belong to one run of the
 * firmware, even when a late clock sync or a size limit splits them over several
 * files. It is unique, not secret, and it does not order boots: the date and the
 * file number do that. It is made on first use, not at boot, because the
 * hardware random number generator is only truly random once Wi-Fi or Bluetooth
 * is running; the MAC address and the uptime are mixed in so that two units, or
 * two boots of one unit, differ even when it is not.
 */
#define BOOT_ID_STRING_LEN 36

/* "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx", lower case, NUL terminated. Any task. */
const char *boot_id_get(void);

/*
 * Pure helpers, split out so they can be tested on the host.
 */

/*
 * Mixes `random` with the MAC address and a time into 16 bytes. The output is
 * `random` XOR a digest of the MAC and the time: it is as random as `random` is,
 * and it still differs between units and between boots when `random` is weak.
 */
void boot_id_mix(uint8_t out[16], const uint8_t random[16], const uint8_t mac[6], uint64_t time_us);

/* Writes `bytes` as a UUID string, setting the version (4) and variant (10) bits. */
void boot_id_format_uuid_v4(const uint8_t bytes[16], char out[BOOT_ID_STRING_LEN + 1]);

#ifdef __cplusplus
}
#endif
