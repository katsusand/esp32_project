#ifndef WIFI_RSSI_HISTORY_H
#define WIFI_RSSI_HISTORY_H

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Recorded once when the station stops being associated. Pass it to the
 * sparkline widget as `gap_value` so a break renders as a break instead of
 * being plotted as a real (very weak) reading.
 *
 * English contract: this service never powers the radio up. It samples what is
 * already there, so the graph reflects Wi-Fi that other features turned on.
 */
#define WIFI_RSSI_HISTORY_GAP_DBM INT16_MIN

esp_err_t wifi_rssi_history_start(void);


/*
 * Returns the collected RSSI samples in dBm, oldest first.
 *
 * English contract: `*samples` points at service-owned static storage that
 * lives for the whole process, which is what makes it safe to hand straight to
 * a cyd_display_sparkline_t (the display driver does not copy sample arrays).
 * `*revision` changes whenever the contents change; pass it through to the
 * sparkline or the dirty-rect diff will never notice the update.
 *
 * Any output pointer may be NULL. Returns false while no sample has been
 * collected, which is the normal state before the first successful Wi-Fi
 * association.
 *
 * The sampler writes from this service's own task while a reader may be rendering.
 * A concurrent write only produces one partially updated frame, which is
 * acceptable for a trend graph, so no lock is held across rendering.
 */
bool wifi_rssi_history_get(const int16_t **samples, uint16_t *count, uint16_t *revision);

/*
 * Current reading, or false when the station is not associated right now.
 *
 * English contract: a trailing break marker means "not connected", so this
 * returns false rather than reporting the last real reading as if it were live.
 */
bool wifi_rssi_history_get_latest(int16_t *rssi_dbm);

#ifdef __cplusplus
}
#endif

#endif
