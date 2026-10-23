#include <string.h>
#include "settings_pin_logic.h"

/* memset that the compiler may not drop for a buffer about to go unused. */
static void settings_pin_wipe(void *buf, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)buf;
    while (len-- > 0) {
        *p++ = 0;
    }
}

bool settings_pin_entry_push(settings_pin_entry_t *entry, char digit)
{
    if (entry == NULL || digit < '0' || digit > '9' || entry->len >= SETTINGS_PIN_MAX_DIGITS) {
        return false;
    }
    entry->digits[entry->len++] = digit;
    entry->digits[entry->len] = '\0';
    return true;
}

bool settings_pin_entry_pop(settings_pin_entry_t *entry)
{
    if (entry == NULL || entry->len == 0) {
        return false;
    }
    entry->digits[--entry->len] = '\0';
    return true;
}

void settings_pin_entry_clear(settings_pin_entry_t *entry)
{
    if (entry != NULL) {
        settings_pin_wipe(entry, sizeof(*entry));
    }
}

bool settings_pin_entry_complete(const settings_pin_entry_t *entry)
{
    return entry != NULL && entry->len >= SETTINGS_PIN_MIN_DIGITS && entry->len <= SETTINGS_PIN_MAX_DIGITS;
}

uint32_t settings_pin_wait_seconds(uint32_t failures)
{
    if (failures <= SETTINGS_PIN_FREE_ATTEMPTS) {
        return 0;
    }
    uint32_t doublings = failures - SETTINGS_PIN_FREE_ATTEMPTS - 1U;
    uint32_t wait = SETTINGS_PIN_FIRST_WAIT_SECONDS;

    /* Stop doubling at the cap, so a huge count cannot overflow the shift. */
    while (doublings-- > 0 && wait < SETTINGS_PIN_MAX_WAIT_SECONDS) {
        wait *= 2U;
    }
    return wait < SETTINGS_PIN_MAX_WAIT_SECONDS ? wait : SETTINGS_PIN_MAX_WAIT_SECONDS;
}

void settings_pin_setup_reset(settings_pin_setup_t *setup)
{
    if (setup == NULL) {
        return;
    }
    settings_pin_entry_clear(&setup->first);
    setup->stage = SETTINGS_PIN_SETUP_FIRST;
}

settings_pin_setup_result_t settings_pin_setup_submit(settings_pin_setup_t *setup, const settings_pin_entry_t *entry)
{
    if (setup == NULL || !settings_pin_entry_complete(entry)) {
        return SETTINGS_PIN_SETUP_TOO_SHORT;
    }
    if (setup->stage == SETTINGS_PIN_SETUP_FIRST) {
        setup->first = *entry;
        setup->stage = SETTINGS_PIN_SETUP_SECOND;
        return SETTINGS_PIN_SETUP_NEED_SECOND;
    }
    if (entry->len == setup->first.len &&
        settings_pin_equal_ct((const uint8_t *)entry->digits, (const uint8_t *)setup->first.digits, entry->len)) {
        return SETTINGS_PIN_SETUP_MATCH;
    }
    settings_pin_setup_reset(setup);
    return SETTINGS_PIN_SETUP_MISMATCH;
}

bool settings_pin_equal_ct(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;

    if (a == NULL || b == NULL) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}

bool settings_pin_record_fill(settings_pin_record_t *record, const settings_pin_entry_t *entry,
                              settings_pin_hash_fn hash)
{
    if (record == NULL || hash == NULL || !settings_pin_entry_complete(entry) || record->iterations == 0) {
        return false;
    }
    return hash(entry->digits, entry->len, record->salt, sizeof(record->salt), record->iterations, record->hash);
}

bool settings_pin_record_matches(const settings_pin_record_t *record, const settings_pin_entry_t *entry,
                                 settings_pin_hash_fn hash)
{
    uint8_t computed[SETTINGS_PIN_HASH_LEN];
    bool ok = false;

    if (record == NULL || hash == NULL || !settings_pin_entry_complete(entry) || record->iterations == 0) {
        return false;
    }
    if (hash(entry->digits, entry->len, record->salt, sizeof(record->salt), record->iterations, computed)) {
        ok = settings_pin_equal_ct(computed, record->hash, sizeof(computed));
    }
    settings_pin_wipe(computed, sizeof(computed));
    return ok;
}
