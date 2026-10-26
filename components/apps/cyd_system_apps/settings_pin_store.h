#ifndef SETTINGS_PIN_STORE_H
#define SETTINGS_PIN_STORE_H

/*
 * Where the settings PIN lives in NVS: namespace "sys_pin" (system scope, so
 * Clear App Data keeps it; Initialize NVS removes it).
 *
 *   key "pin_v1"  blob  version, flags (none in use), PBKDF2 iterations, salt, hash
 *   key "fails"   u32   wrong PINs in a row, kept across restarts
 *
 * A missing "pin_v1" means the PIN is off.
 *
 * English contract: neither the PIN nor its hash is ever logged. A "pin_v1"
 * blob of the wrong size or version is reported to nvs_health and loads as
 * SETTINGS_PIN_STORE_CORRUPT, which callers must treat as locked.
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "settings_pin_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * HMAC-SHA256 rounds per hash, used for PINs set from now on. Each record
 * keeps its own count, so a PIN stored with another count still checks.
 *
 * 4096 took 699 ms on the CYD (2026-10-23), a pause the user noticed on every
 * 決定. 1024 is about 175 ms. Guessing on the touch screen is held back by the
 * wait after wrong PINs, not by this; the count only slows guessing from a
 * flash dump, which 6-8 digits cannot survive anyway.
 */
#define SETTINGS_PIN_STORE_ITERATIONS 1024U

/* flags: none in use. Bit 0 once meant "system info asks for the PIN too"
   (2026-10-23 only); apps now choose that themselves, and it is ignored. */

typedef enum {
    SETTINGS_PIN_STORE_OFF = 0,
    SETTINGS_PIN_STORE_ON,
    SETTINGS_PIN_STORE_CORRUPT,
} settings_pin_store_state_t;

typedef struct {
    settings_pin_record_t record;
    uint32_t flags;
} settings_pin_store_config_t;

esp_err_t settings_pin_store_load(settings_pin_store_config_t *config, settings_pin_store_state_t *state);
esp_err_t settings_pin_store_save(const settings_pin_store_config_t *config);
/* Removes the PIN and the failure count. */
esp_err_t settings_pin_store_erase(void);

esp_err_t settings_pin_store_load_failures(uint32_t *failures);
esp_err_t settings_pin_store_save_failures(uint32_t failures);

/* A fresh random salt and the current iteration count. */
void settings_pin_store_new_record(settings_pin_record_t *record);
/* PBKDF2-HMAC-SHA256 (mbedtls); a settings_pin_hash_fn. */
bool settings_pin_store_hash(const char *pin,
                             size_t pin_len,
                             const uint8_t *salt,
                             size_t salt_len,
                             uint32_t iterations,
                             uint8_t out[SETTINGS_PIN_HASH_LEN]);

#ifdef __cplusplus
}
#endif

#endif
