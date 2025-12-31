#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the clock time ticker.
 * This starts a periodic timer that posts CLOCK_EVENT_MINUTE_TICK and
 * CLOCK_EVENT_HOUR_TICK events when the time changes.
 *
 * Should be called after the default event loop is created.
 */
void clock_time_ticker_init(void);

/**
 * Start the time ticker.
 * Called automatically when NTP sync completes.
 * Can also be called manually if needed.
 */
void clock_time_ticker_start(void);

/**
 * Stop the time ticker.
 * Called automatically when NTP sync is lost.
 */
void clock_time_ticker_stop(void);

/**
 * Check if the time ticker is running.
 */
bool clock_time_ticker_is_running(void);

#ifdef __cplusplus
}
#endif
