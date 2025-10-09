#define _POSIX_C_SOURCE 200809L
#include "gpio_buttons.h"
#include <gpiod.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct gpio_buttons {
    struct gpiod_chip *chip;
    struct gpiod_line_request *req;
    unsigned int *offsets;
    char **names;
    int nlines;
    int debounce_ms;
    long poll_us; /* poll interval in microseconds */

    /* state per line */
    int *stable_val;
    int *last_read;
    struct timespec *last_time;
    int *debounce_active;

    /* callbacks */
    gpio_button_cb_t *cbs; /* per-index callbacks (NULL if none) */
    void **cb_users;
    gpio_button_cb_t global_cb;
    void *global_user;

    /* thread control */
    pthread_t thread;
    int running;
    pthread_mutex_t lock;
};

/* Helpers */
static long elapsed_ms(const struct timespec *a, const struct timespec *b) {
    return (a->tv_sec - b->tv_sec) * 1000 + (a->tv_nsec - b->tv_nsec) / 1000000;
}

/* Forward */
static void invoke_cb(struct gpio_buttons *h, int idx, unsigned int offset, int val);

struct gpio_buttons *gpio_buttons_create(const char *chip_path,
                                         const unsigned int *offsets,
                                         const char **names,
                                         int nlines,
                                         int debounce_ms,
                                         long poll_us)
{
    if (!chip_path || !offsets || nlines <= 0) return NULL;
    struct gpio_buttons *h = calloc(1, sizeof(*h));
    if (!h) return NULL;

    h->chip = gpiod_chip_open(chip_path);
    if (!h->chip) {
        perror("gpiod_chip_open");
        free(h);
        return NULL;
    }

    h->offsets = calloc(nlines, sizeof(unsigned int));
    h->names = calloc(nlines, sizeof(char*));
    h->nlines = nlines;
    h->debounce_ms = debounce_ms > 0 ? debounce_ms : 200;
    h->poll_us = (poll_us > 0) ? poll_us : 20000; /* default 20 ms */

    for (int i = 0; i < nlines; ++i) h->offsets[i] = offsets[i];
    if (names) {
        for (int i = 0; i < nlines; ++i) {
            if (names[i]) h->names[i] = strdup(names[i]);
        }
    }

    h->stable_val = calloc(nlines, sizeof(int));
    h->last_read = calloc(nlines, sizeof(int));
    h->last_time = calloc(nlines, sizeof(struct timespec));
    h->debounce_active = calloc(nlines, sizeof(int));
    h->cbs = calloc(nlines, sizeof(gpio_button_cb_t));
    h->cb_users = calloc(nlines, sizeof(void*));
    pthread_mutex_init(&h->lock, NULL);

    /* prepare line settings: input, no edge detection (polling) */
    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    if (!settings) { perror("line_settings_new"); goto fail; }
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);

    struct gpiod_line_config *lcfg = gpiod_line_config_new();
    if (!lcfg) { perror("line_config_new"); goto fail_settings; }
    gpiod_line_config_add_line_settings(lcfg, h->offsets, nlines, settings);

    struct gpiod_request_config *rcfg = gpiod_request_config_new();
    if (!rcfg) { perror("request_config_new"); goto fail_lcfg; }
    gpiod_request_config_set_consumer(rcfg, "gpio-buttons");

    h->req = gpiod_chip_request_lines(h->chip, rcfg, lcfg);
    if (!h->req) {
        perror("chip_request_lines");
        goto fail_rcfg;
    }

    /* init stable values */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    for (int i = 0; i < nlines; ++i) {
        int v = gpiod_line_request_get_value(h->req, h->offsets[i]);
        if (v < 0) v = 1; /* default released */
        h->stable_val[i] = v;
        h->last_read[i] = v;
        h->last_time[i] = now;
        h->debounce_active[i] = 0;
    }

    /* cleanup temp objects */
    gpiod_line_config_free(lcfg);
    gpiod_line_settings_free(settings);
    gpiod_request_config_free(rcfg);

    return h;

fail_rcfg:
    gpiod_request_config_free(rcfg);
fail_lcfg:
    gpiod_line_config_free(lcfg);
fail_settings:
    gpiod_line_settings_free(settings);
fail:
    if (h->req) gpiod_line_request_release(h->req);
    if (h->chip) gpiod_chip_close(h->chip);
    if (h) {
        free(h->offsets);
        for (int i = 0; i < nlines; ++i) free(h->names[i]);
        free(h->names);
        free(h->stable_val);
        free(h->last_read);
        free(h->last_time);
        free(h->debounce_active);
        free(h->cbs);
        free(h->cb_users);
        pthread_mutex_destroy(&h->lock);
        free(h);
    }
    return NULL;
}

int gpio_buttons_register_callback(struct gpio_buttons *h,
                                   int index,
                                   gpio_button_cb_t cb,
                                   void *user)
{
    if (!h) return -1;
    pthread_mutex_lock(&h->lock);
    if (index < 0) {
        h->global_cb = cb;
        h->global_user = user;
    } else if (index < h->nlines) {
        h->cbs[index] = cb;
        h->cb_users[index] = user;
    } else {
        pthread_mutex_unlock(&h->lock);
        return -1;
    }
    pthread_mutex_unlock(&h->lock);
    return 0;
}

static void invoke_cb(struct gpio_buttons *h, int idx, unsigned int offset, int val) {
    pthread_mutex_lock(&h->lock);
    gpio_button_cb_t cb = h->cbs[idx];
    void *user = h->cb_users[idx];
    gpio_button_cb_t gcb = h->global_cb;
    void *guser = h->global_user;
    pthread_mutex_unlock(&h->lock);

    if (cb) cb(offset, val, user);
    if (gcb) gcb(offset, val, guser);
}

static void *thread_fn(void *arg) {
    struct gpio_buttons *h = arg;
    struct timespec ts;

    while (h->running) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        for (int i = 0; i < h->nlines; ++i) {
            int v = gpiod_line_request_get_value(h->req, h->offsets[i]);
            if (v < 0) continue;

            if (v != h->last_read[i]) {
                h->last_read[i] = v;
                h->last_time[i] = now;
                h->debounce_active[i] = 1;
            }

            if (h->debounce_active[i]) {
                long diff = elapsed_ms(&now, &h->last_time[i]);
                if (diff >= h->debounce_ms && v != h->stable_val[i]) {
                    h->stable_val[i] = v;
                    h->debounce_active[i] = 0;
                    /* v == 0 => pressed (LOW), v == 1 => released */
                    invoke_cb(h, i, h->offsets[i], v);
                }
            }
        }

        /* sleep using nanosleep according to configured poll_us */
        ts.tv_sec = h->poll_us / 1000000;
        ts.tv_nsec = (h->poll_us % 1000000) * 1000;
        nanosleep(&ts, NULL);
    }
    return NULL;
}

int gpio_buttons_start(struct gpio_buttons *h) {
    if (!h) return -1;
    if (h->running) return 0;
    h->running = 1;
    if (pthread_create(&h->thread, NULL, thread_fn, h) != 0) {
        h->running = 0;
        return -1;
    }
    return 0;
}

int gpio_buttons_stop(struct gpio_buttons *h) {
    if (!h) return -1;
    if (!h->running) return 0;
    h->running = 0;
    pthread_join(h->thread, NULL);
    return 0;
}

void gpio_buttons_destroy(struct gpio_buttons *h) {
    if (!h) return;
    gpio_buttons_stop(h);
    if (h->req) gpiod_line_request_release(h->req);
    if (h->chip) gpiod_chip_close(h->chip);
    for (int i = 0; i < h->nlines; ++i) free(h->names[i]);
    free(h->names);
    free(h->offsets);
    free(h->stable_val);
    free(h->last_read);
    free(h->last_time);
    free(h->debounce_active);
    free(h->cbs);
    free(h->cb_users);
    pthread_mutex_destroy(&h->lock);
    free(h);
}
