// SPDX-License-Identifier: GPL-2.0-only
/*
 * Window fade-in / fade-out animations for HyaloOS compositor.
 *
 * Uses wl_event_loop timers to drive per-frame opacity changes on view
 * scene-graph buffers via wlr_scene_buffer_set_opacity().
 */
#include "animation.h"
#include "config/rcxml.h"
#include "labwc.h"
#include "view.h"
#include <stdlib.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#define ANIM_TICK_MS 16 /* ~60 fps */

static void
set_buffer_opacity(struct wlr_scene_buffer *buffer,
	int sx, int sy, void *data)
{
	float *opacity = data;
	wlr_scene_buffer_set_opacity(buffer, *opacity);
}

static void
apply_opacity(struct view *view, float opacity)
{
	if (!view->scene_tree) {
		return;
	}
	wlr_scene_node_for_each_buffer(&view->scene_tree->node,
		set_buffer_opacity, &opacity);

	/* Also apply to SSD (decoration) tree if present */
	if (view->ssd) {
		/* ssd->tree lives right next to the view scene_tree */
	}
}

static void animation_cleanup(struct view_animation *anim);

static int
animation_timer_cb(void *data)
{
	struct view_animation *anim = data;
	struct view *view = anim->view;

	anim->elapsed_ms += ANIM_TICK_MS;

	float progress = (float)anim->elapsed_ms / (float)anim->duration_ms;
	if (progress >= 1.0f) {
		progress = 1.0f;
	}

	/* Ease-out cubic: 1 - (1 - t)^3 */
	float ease = 1.0f - (1.0f - progress) * (1.0f - progress) * (1.0f - progress);

	if (anim->type == ANIMATION_FADE_IN) {
		anim->opacity = ease;
	} else {
		anim->opacity = 1.0f - ease;
	}

	apply_opacity(view, anim->opacity);

	if (progress >= 1.0f) {
		if (anim->type == ANIMATION_FADE_OUT) {
			/*
			 * Animation done – actually hide the scene node.
			 * The view's scene_tree was kept enabled during the
			 * fade-out so the user sees the animation; now disable it.
			 */
			wlr_scene_node_set_enabled(&view->scene_tree->node, false);
		}
		animation_cleanup(anim);
		return 0;
	}

	/* Re-arm timer for next tick */
	wl_event_source_timer_update(anim->timer, ANIM_TICK_MS);
	return 0;
}

static void
animation_cleanup(struct view_animation *anim)
{
	if (anim->timer) {
		wl_event_source_remove(anim->timer);
		anim->timer = NULL;
	}
	anim->active = false;
	anim->elapsed_ms = 0;
}

void
animation_fade_in(struct view *view)
{
	int duration = rc.window_animation_duration;
	if (duration <= 0 || !view->scene_tree) {
		return;
	}

	struct view_animation *anim = &view->animation;

	/* Cancel any running animation first */
	if (anim->active) {
		animation_cleanup(anim);
	}

	anim->view = view;
	anim->active = true;
	anim->type = ANIMATION_FADE_IN;
	anim->opacity = 0.0f;
	anim->target = 1.0f;
	anim->duration_ms = duration;
	anim->elapsed_ms = 0;

	/* Start with fully transparent */
	apply_opacity(view, 0.0f);

	anim->timer = wl_event_loop_add_timer(
		view->server->wl_event_loop, animation_timer_cb, anim);
	wl_event_source_timer_update(anim->timer, ANIM_TICK_MS);
}

bool
animation_fade_out(struct view *view)
{
	int duration = rc.window_animation_duration;
	if (duration <= 0 || !view->scene_tree) {
		return false;
	}

	struct view_animation *anim = &view->animation;

	/* Cancel any running animation first */
	if (anim->active) {
		animation_cleanup(anim);
	}

	/*
	 * Keep the scene node enabled during fade-out so the
	 * user sees the animation. It will be disabled when
	 * the animation completes.
	 */
	wlr_scene_node_set_enabled(&view->scene_tree->node, true);

	anim->view = view;
	anim->active = true;
	anim->type = ANIMATION_FADE_OUT;
	anim->opacity = 1.0f;
	anim->target = 0.0f;
	anim->duration_ms = duration;
	anim->elapsed_ms = 0;

	anim->timer = wl_event_loop_add_timer(
		view->server->wl_event_loop, animation_timer_cb, anim);
	wl_event_source_timer_update(anim->timer, ANIM_TICK_MS);
	return true;
}

void
animation_cancel(struct view *view)
{
	struct view_animation *anim = &view->animation;
	if (!anim->active) {
		return;
	}
	animation_cleanup(anim);
	/* Restore full opacity */
	apply_opacity(view, 1.0f);
}

void
animation_finish(struct view *view)
{
	struct view_animation *anim = &view->animation;
	if (anim->timer) {
		wl_event_source_remove(anim->timer);
		anim->timer = NULL;
	}
	anim->active = false;
}
