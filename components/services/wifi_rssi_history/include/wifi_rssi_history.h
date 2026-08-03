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
 * Recorded when the station is not associated. Pass it to the sparkline widget
 * as `gap_value` so outages render as breaks in the line instead of being
 * plotted as a real (very weak) reading.
 */
#define WIFI_RSSI_HISTORY_GAP_DBM INT16_MIN

esp_err_t wifi_rssi_history_start(void);

/*
 * Holds a radio lease so sampling can continue while a viewer is watching.
 *
 * Why this exists: `radio_manager` powers the radio down once no client holds a
 * lease, and the sampler then has nothing to read. Without this the graph stops
 * a few tens of seconds after boot, when time_sync releases its lease.
 *
 * English contract: non-blocking and idempotent, so it is safe to call from the
 * app_shell step path and on every redraw. The blocking radio_manager_acquire()
 * runs on this service's own task; calling it directly from the shell froze the
 * UI for the whole Wi-Fi bring-up. Enable it only while the graph is actually on
 * screen — holding the lease permanently would defeat the point of
 * radio_manager. Pair it with the app's leave() path so the lease cannot
 * outlive the view.
 */
esp_err_t wifi_rssi_history_set_monitoring(bool monitoring);
bool wifi_rssi_history_is_monitoring(void);

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

bool wifi_rssi_history_get_latest(int16_t *rssi_dbm);

#ifdef __cplusplus
}
#endif

#endif
