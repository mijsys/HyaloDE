/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_ANIMATION_H
#define LABWC_ANIMATION_H

#include <stdbool.h>

struct view;
struct wl_event_source;

enum animation_type {
	ANIMATION_FADE_IN,
	ANIMATION_FADE_OUT,
};

struct view_animation {
	bool active;
	enum animation_type type;
	float opacity;      /* current 0.0 – 1.0 */
	float target;       /* 0.0 or 1.0 */
	int duration_ms;    /* total duration */
	int elapsed_ms;     /* elapsed time */
	struct wl_event_source *timer;
	struct view *view;
};

/**
 * Start a fade-in animation on a view that was just mapped.
 * Sets initial opacity to 0 and animates to 1.
 */
void animation_fade_in(struct view *view);

/**
 * Start a fade-out animation on a view that is about to unmap.
 * Animates from current opacity to 0, then completes the unmap.
 * Returns true if animation was started (caller should delay the unmap).
 */
bool animation_fade_out(struct view *view);

/**
 * Cancel any running animation on the view and reset opacity to 1.
 */
void animation_cancel(struct view *view);

/**
 * Clean up animation resources (called on view destroy).
 */
void animation_finish(struct view *view);

#endif /* LABWC_ANIMATION_H */
