#ifndef GPIO_BUTTONS_H
#define GPIO_BUTTONS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Callback invoked on stable state change.
 * offset: GPIO line offset (e.g., 0 for PF0)
 * value:  0 = pressed (LOW for pull-up wiring), 1 = released (HIGH)
 * user:   user-supplied pointer
 */
typedef void (*gpio_button_cb_t)(unsigned int offset, int value, void *user);

/* Opaque handle */
struct gpio_buttons;

/* Create/initialize the button manager.
 * chip_path: e.g. "/dev/gpiochip5"
 * offsets:   array of line offsets (e.g. {0,1,4,6})
 * names:     optional array of names (can be NULL)
 * nlines:    number of offsets
 * debounce_ms: debounce timeout in milliseconds (e.g. 200)
 *
 * Returns pointer or NULL on error.
 */
/* New: added poll_us argument (microseconds)
 * poll_us: poll interval in microseconds (e.g. 20000 for 20 ms)
 */
struct gpio_buttons *gpio_buttons_create(const char *chip_path,
                                         const unsigned int *offsets,
                                         const char **names,
                                         int nlines,
                                         int debounce_ms,
                                         long poll_us);

/* Register callback for a line index (0..nlines-1).
 * If index is -1, register a global callback invoked for any line (offset given).
 */
int gpio_buttons_register_callback(struct gpio_buttons *h,
                                   int index,
                                   gpio_button_cb_t cb,
                                   void *user);

/* Start background thread that polls lines and dispatches callbacks.
 * Returns 0 on success.
 */
int gpio_buttons_start(struct gpio_buttons *h);

/* Stop thread and join. Returns 0 on success. */
int gpio_buttons_stop(struct gpio_buttons *h);

/* Destroy and free everything. Call after stop. */
void gpio_buttons_destroy(struct gpio_buttons *h);

#ifdef __cplusplus
}
#endif

#endif /* GPIO_BUTTONS_H */
