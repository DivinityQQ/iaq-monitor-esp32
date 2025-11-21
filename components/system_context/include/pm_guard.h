/* components/system_context/include/pm_guard.h */
#ifndef PM_GUARD_H
#define PM_GUARD_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize runtime power management (DFS + light sleep) and create
 * shared PM locks used to guard peripheral access.
 * Safe to call even when CONFIG_PM_ENABLE is off (no-op).
 */
esp_err_t pm_guard_init(void);

/**
 * Whether runtime PM is active (CONFIG_PM_ENABLE and init succeeded).
 */
bool pm_guard_is_enabled(void);

/**
 * Acquire/release the shared APB/CPU frequency lock to keep clocks stable
 * during I/O (e.g., I2C/UART transactions).
 */
void pm_guard_lock_bus(void);
void pm_guard_unlock_bus(void);

/**
 * Acquire/release a lock that forces CPU frequency to maximum.
 * Useful for CPU-heavy work (JSON building, TLS handshakes, etc.)
 */
void pm_guard_lock_cpu(void);
void pm_guard_unlock_cpu(void);

/**
 * Acquire/release a lock that prevents light sleep entirely.
 * Useful for peripherals that cannot tolerate clock gating.
 */
void pm_guard_lock_no_sleep(void);
void pm_guard_unlock_no_sleep(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_GUARD_H */
