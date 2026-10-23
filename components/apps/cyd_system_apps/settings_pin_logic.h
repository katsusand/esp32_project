#ifndef SETTINGS_PIN_LOGIC_H
#define SETTINGS_PIN_LOGIC_H

/*
 * The parts of the settings PIN that need no SDK: the digits being typed, the
 * wait after failed attempts, the "type it twice" check, and comparing a typed
 * PIN with the stored hash. settings_pin_app.c and settings_pin_store.c use
 * them on the device; test/host/test_settings_pin_logic.c checks them on a PC.
 *
 * English contract: nothing here logs, and nothing keeps a PIN after the
 * caller clears its entry (settings_pin_entry_clear() wipes the buffer).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 6 to 8 digits. Six is the shortest accepted: see docs/settings_pin.md. */
#define SETTINGS_PIN_MIN_DIGITS 6
#define SETTINGS_PIN_MAX_DIGITS 8

/* Failed attempts allowed before the first wait, and the wait it starts. Each
   further failure doubles the wait, up to SETTINGS_PIN_MAX_WAIT_SECONDS. */
#define SETTINGS_PIN_FREE_ATTEMPTS 4
#define SETTINGS_PIN_FIRST_WAIT_SECONDS 30U
#define SETTINGS_PIN_MAX_WAIT_SECONDS 3600U

#define SETTINGS_PIN_SALT_LEN 16
#define SETTINGS_PIN_HASH_LEN 32

/* The digits typed so far. */
typedef struct {
    char digits[SETTINGS_PIN_MAX_DIGITS + 1];
    uint8_t len;
} settings_pin_entry_t;

/* Adds one digit ('0'-'9'). False when full or not a digit. */
bool settings_pin_entry_push(settings_pin_entry_t *entry, char digit);
/* Removes the last digit. False when empty. */
bool settings_pin_entry_pop(settings_pin_entry_t *entry);
/* Wipes the digits (not optimised away). */
void settings_pin_entry_clear(settings_pin_entry_t *entry);
/* Long enough to submit. */
bool settings_pin_entry_complete(const settings_pin_entry_t *entry);

/*
 * The wait in seconds after `failures` consecutive wrong PINs: none for the
 * first SETTINGS_PIN_FREE_ATTEMPTS, then 30, 60, 120, ... up to an hour.
 */
uint32_t settings_pin_wait_seconds(uint32_t failures);

/*
 * Setting a new PIN: typed once, then again. The second must equal the first.
 */
typedef enum {
    SETTINGS_PIN_SETUP_FIRST = 0,
    SETTINGS_PIN_SETUP_SECOND,
} settings_pin_setup_stage_t;

typedef enum {
    SETTINGS_PIN_SETUP_TOO_SHORT = 0, /* nothing changes */
    SETTINGS_PIN_SETUP_NEED_SECOND,   /* first entry kept, ask again */
    SETTINGS_PIN_SETUP_MATCH,         /* `first` holds the new PIN */
    SETTINGS_PIN_SETUP_MISMATCH,      /* both forgotten, back to the first entry */
} settings_pin_setup_result_t;

typedef struct {
    settings_pin_setup_stage_t stage;
    settings_pin_entry_t first;
} settings_pin_setup_t;

void settings_pin_setup_reset(settings_pin_setup_t *setup);
/* Takes one finished entry. The caller clears `entry` afterwards. */
settings_pin_setup_result_t settings_pin_setup_submit(settings_pin_setup_t *setup, const settings_pin_entry_t *entry);

/*
 * The stored form of a PIN: a salted, slow hash, never the digits.
 */
typedef struct {
    uint8_t salt[SETTINGS_PIN_SALT_LEN];
    uint8_t hash[SETTINGS_PIN_HASH_LEN];
    uint32_t iterations;
} settings_pin_record_t;

/* PBKDF2-HMAC-SHA256 on the device; a stand-in in the host tests. */
typedef bool (*settings_pin_hash_fn)(const char *pin,
                                     size_t pin_len,
                                     const uint8_t *salt,
                                     size_t salt_len,
                                     uint32_t iterations,
                                     uint8_t out[SETTINGS_PIN_HASH_LEN]);

/* Fills record->hash from `entry` with the record's salt and iterations. */
bool settings_pin_record_fill(settings_pin_record_t *record, const settings_pin_entry_t *entry,
                              settings_pin_hash_fn hash);
/* True only when `entry` hashes to the stored hash. A failing hash is a mismatch. */
bool settings_pin_record_matches(const settings_pin_record_t *record, const settings_pin_entry_t *entry,
                                 settings_pin_hash_fn hash);
/* Compares in time that does not depend on where the bytes differ. */
bool settings_pin_equal_ct(const uint8_t *a, const uint8_t *b, size_t len);

#ifdef __cplusplus
}
#endif

#endif
