#include "boot_id.h"

/* FNV-1a, 64 bit. Not cryptographic and does not need to be: it only has to make
   different MACs and times give different bytes. */
static uint64_t boot_id_fnv1a(uint64_t hash, const uint8_t *data, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

void boot_id_mix(uint8_t out[16], const uint8_t random[16], const uint8_t mac[6], uint64_t time_us)
{
    uint8_t time_bytes[8];
    uint64_t halves[2] = { 0xcbf29ce484222325ULL, 0x84222325cbf29ce4ULL };

    for (size_t i = 0; i < sizeof(time_bytes); ++i) {
        time_bytes[i] = (uint8_t)(time_us >> (8U * i));
    }
    for (size_t half = 0; half < 2; ++half) {
        halves[half] = boot_id_fnv1a(halves[half], mac, 6);
        halves[half] = boot_id_fnv1a(halves[half], time_bytes, sizeof(time_bytes));
        /* A different pass over the same inputs for each half, so the two halves
           are not copies of each other. */
        halves[half] = boot_id_fnv1a(halves[half], (const uint8_t *)&half, 1);
    }
    for (size_t i = 0; i < 16; ++i) {
        out[i] = (uint8_t)(random[i] ^ (uint8_t)(halves[i / 8U] >> (8U * (i % 8U))));
    }
}

void boot_id_format_uuid_v4(const uint8_t bytes[16], char out[BOOT_ID_STRING_LEN + 1])
{
    static const char hex[] = "0123456789abcdef";
    uint8_t b[16];
    size_t pos = 0;

    for (size_t i = 0; i < 16; ++i) {
        b[i] = bytes[i];
    }
    b[6] = (uint8_t)((b[6] & 0x0fU) | 0x40U); /* version 4 */
    b[8] = (uint8_t)((b[8] & 0x3fU) | 0x80U); /* variant 10xx */

    for (size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            out[pos++] = '-';
        }
        out[pos++] = hex[b[i] >> 4];
        out[pos++] = hex[b[i] & 0x0fU];
    }
    out[pos] = '\0';
}
