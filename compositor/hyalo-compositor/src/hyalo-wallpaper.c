// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L

#include "hyalo-wallpaper.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "buffer.h"
#include "common/mem.h"
#include "common/string-helpers.h"
#include "labwc.h"
#include "output.h"
#include "scaled-buffer/scaled-buffer.h"

struct hyalo_wallpaper_buffer {
	struct scaled_buffer *scaled_buffer;
	cairo_surface_t *source_surface;
	char *path;
	int source_width;
	int source_height;
};

static cairo_surface_t *
load_wallpaper_surface(const char *path)
{
	GError *error = NULL;
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path, &error);
	if (!pixbuf) {
		if (error) {
			wlr_log(WLR_ERROR, "failed to load wallpaper '%s': %s",
				path, error->message);
			g_error_free(error);
		}
		return NULL;
	}

	const int width = gdk_pixbuf_get_width(pixbuf);
	const int height = gdk_pixbuf_get_height(pixbuf);
	const int channels = gdk_pixbuf_get_n_channels(pixbuf);
	const bool has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
	const int src_stride = gdk_pixbuf_get_rowstride(pixbuf);
	const guchar *src_data = gdk_pixbuf_read_pixels(pixbuf);

	cairo_surface_t *surface = cairo_image_surface_create(
		CAIRO_FORMAT_ARGB32, width, height);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		g_object_unref(pixbuf);
		cairo_surface_destroy(surface);
		return NULL;
	}

	unsigned char *dst_data = cairo_image_surface_get_data(surface);
	const int dst_stride = cairo_image_surface_get_stride(surface);

	for (int y = 0; y < height; ++y) {
		const guchar *src_row = src_data + (y * src_stride);
		uint32_t *dst_row = (uint32_t *)(dst_data + (y * dst_stride));
		for (int x = 0; x < width; ++x) {
			const guchar *pixel = src_row + (x * channels);
			uint8_t red = pixel[0];
			uint8_t green = pixel[1];
			uint8_t blue = pixel[2];
			uint8_t alpha = has_alpha ? pixel[3] : 255;

			red = (uint8_t)((red * alpha + 127) / 255);
			green = (uint8_t)((green * alpha + 127) / 255);
			blue = (uint8_t)((blue * alpha + 127) / 255);

			dst_row[x] = ((uint32_t)alpha << 24)
				| ((uint32_t)red << 16)
				| ((uint32_t)green << 8)
				| (uint32_t)blue;
		}
	}

	cairo_surface_mark_dirty(surface);
	g_object_unref(pixbuf);
	return surface;
}

static struct lab_data_buffer *
wallpaper_create_buffer(struct scaled_buffer *scaled_buffer, double scale)
{
	struct hyalo_wallpaper_buffer *self = scaled_buffer->data;
	if (!self || !self->source_surface || self->source_width <= 0
			|| self->source_height <= 0) {
		wlr_log(WLR_ERROR, "cannot create wallpaper buffer: invalid source surface");
		return NULL;
	}

	struct lab_data_buffer *buffer = buffer_create_cairo(
		scaled_buffer->width, scaled_buffer->height, scale);
	if (!buffer) {
		wlr_log(WLR_ERROR,
			"failed to allocate wallpaper buffer for %dx%d at scale %.3f",
			scaled_buffer->width, scaled_buffer->height, scale);
		return NULL;
	}

	cairo_t *cairo = cairo_create(buffer->surface);
	const double scale_x = (double)scaled_buffer->width / (double)self->source_width;
	const double scale_y = (double)scaled_buffer->height / (double)self->source_height;
	const double cover_scale = fmax(scale_x, scale_y);
	const double draw_width = self->source_width * cover_scale;
	const double draw_height = self->source_height * cover_scale;
	const double offset_x = ((double)scaled_buffer->width - draw_width) / 2.0;
	const double offset_y = ((double)scaled_buffer->height - draw_height) / 2.0;

	cairo_translate(cairo, offset_x, offset_y);
	cairo_scale(cairo, cover_scale, cover_scale);
	cairo_set_source_surface(cairo, self->source_surface, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cairo), CAIRO_FILTER_BEST);
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cairo);
	cairo_surface_flush(buffer->surface);
	cairo_destroy(cairo);

	return buffer;
}

static void
wallpaper_destroy(struct scaled_buffer *scaled_buffer)
{
	struct hyalo_wallpaper_buffer *self = scaled_buffer->data;
	if (!self) {
		return;
	}

	if (self->source_surface) {
		cairo_surface_destroy(self->source_surface);
	}
	free(self->path);
	free(self);
}

static bool
wallpaper_equal(struct scaled_buffer *left, struct scaled_buffer *right)
{
	struct hyalo_wallpaper_buffer *lhs = left->data;
	struct hyalo_wallpaper_buffer *rhs = right->data;
	return lhs && rhs
		&& str_equal(lhs->path, rhs->path)
		&& left->width == right->width
		&& left->height == right->height;
}

static const struct scaled_buffer_impl wallpaper_impl = {
	.create_buffer = wallpaper_create_buffer,
	.destroy = wallpaper_destroy,
	.equal = wallpaper_equal,
};

static void
destroy_output_wallpaper_buffer(struct output *output)
{
	if (output->hyalo_wallpaper_buffer) {
		wlr_scene_node_destroy(&output->hyalo_wallpaper_buffer->scene_buffer->node);
		output->hyalo_wallpaper_buffer = NULL;
	}
}

static void
destroy_output_wallpaper_source(struct output *output)
{
	destroy_output_wallpaper_buffer(output);
	if (output->hyalo_wallpaper_surface) {
		cairo_surface_destroy(output->hyalo_wallpaper_surface);
		output->hyalo_wallpaper_surface = NULL;
	}
	zfree(output->hyalo_wallpaper_path);
}

static bool
output_needs_new_wallpaper_buffer(struct output *output)
{
	if (!output->hyalo_wallpaper_buffer) {
		return true;
	}

	struct hyalo_wallpaper_buffer *wallpaper = output->hyalo_wallpaper_buffer->data;
	if (!wallpaper) {
		return true;
	}

	return !str_equal(output->hyalo_wallpaper_path, wallpaper->path);
}

static void
set_output_wallpaper_source(struct output *output, const char *path,
	cairo_surface_t *surface)
{
	if (output->hyalo_wallpaper_surface) {
		cairo_surface_destroy(output->hyalo_wallpaper_surface);
	}
	output->hyalo_wallpaper_surface = cairo_surface_reference(surface);
	xstrdup_replace(output->hyalo_wallpaper_path, path);
}

static void
schedule_output_frame(struct output *output)
{
	if (output_is_usable(output)) {
		wlr_output_schedule_frame(output->wlr_output);
	}
}

void
hyalo_wallpaper_output_init(struct output *output)
{
	if (!output || !output->server) {
		return;
	}

	output->hyalo_wallpaper_tree =
		wlr_scene_tree_create(&output->server->scene->tree);
	if (!output->hyalo_wallpaper_tree) {
		wlr_log(WLR_ERROR, "failed to create Hyalo wallpaper tree");
		return;
	}
}

void
hyalo_wallpaper_output_destroy(struct output *output)
{
	if (!output) {
		return;
	}

	destroy_output_wallpaper_source(output);
	if (output->hyalo_wallpaper_tree) {
		wlr_scene_node_destroy(&output->hyalo_wallpaper_tree->node);
		output->hyalo_wallpaper_tree = NULL;
	}
}

void
hyalo_wallpaper_output_reconfigure(struct output *output)
{
	if (!output || !output->hyalo_wallpaper_tree) {
		return;
	}

	/*
	 * Keep wallpaper coordinates tied to the output scene node so each output
	 * gets its own wallpaper placement even if layer arrangement is deferred.
	 */
	if (output->scene_output) {
		wlr_scene_node_set_position(&output->hyalo_wallpaper_tree->node,
			output->scene_output->x, output->scene_output->y);
	}

	if (!output->hyalo_wallpaper_surface
		|| string_null_or_empty(output->hyalo_wallpaper_path)) {
		destroy_output_wallpaper_buffer(output);
		schedule_output_frame(output);
		return;
	}

	if (output_needs_new_wallpaper_buffer(output)) {
		struct hyalo_wallpaper_buffer *wallpaper = znew(*wallpaper);
		wallpaper->source_surface = cairo_surface_reference(
			output->hyalo_wallpaper_surface);
		wallpaper->path = xstrdup(output->hyalo_wallpaper_path);
		wallpaper->source_width = cairo_image_surface_get_width(wallpaper->source_surface);
		wallpaper->source_height = cairo_image_surface_get_height(wallpaper->source_surface);

		destroy_output_wallpaper_buffer(output);

		output->hyalo_wallpaper_buffer = scaled_buffer_create(
			output->hyalo_wallpaper_tree, &wallpaper_impl, true);
		if (!output->hyalo_wallpaper_buffer) {
			cairo_surface_destroy(wallpaper->source_surface);
			free(wallpaper->path);
			free(wallpaper);
			return;
		}
		output->hyalo_wallpaper_buffer->data = wallpaper;
	}

	int width = 0;
	int height = 0;
	wlr_output_effective_resolution(output->wlr_output, &width, &height);
	if (width <= 0 || height <= 0) {
		return;
	}

	scaled_buffer_request_update(output->hyalo_wallpaper_buffer, width, height);
	schedule_output_frame(output);
}

bool
hyalo_wallpaper_set(struct server *server, const char *path)
{
	if (!server || string_null_or_empty(path)) {
		return false;
	}

	cairo_surface_t *surface = load_wallpaper_surface(path);
	if (!surface) {
		return false;
	}

	if (server->hyalo_wallpaper_surface) {
		cairo_surface_destroy(server->hyalo_wallpaper_surface);
	}
	server->hyalo_wallpaper_surface = cairo_surface_reference(surface);
	xstrdup_replace(server->hyalo_wallpaper_path, path);

	struct output *output;
	wl_list_for_each(output, &server->outputs, link) {
		set_output_wallpaper_source(output, path, surface);
		hyalo_wallpaper_output_reconfigure(output);
	}

	cairo_surface_destroy(surface);

	return true;
}

bool
hyalo_wallpaper_set_for_output(struct server *server,
	const char *output_name, const char *path)
{
	if (!server || string_null_or_empty(output_name)
			|| string_null_or_empty(path)) {
		return false;
	}

	struct output *output = output_from_name(server, output_name);
	if (!output) {
		return false;
	}

	cairo_surface_t *surface = load_wallpaper_surface(path);
	if (!surface) {
		return false;
	}

	set_output_wallpaper_source(output, path, surface);
	cairo_surface_destroy(surface);
	hyalo_wallpaper_output_reconfigure(output);

	return true;
}

void
hyalo_wallpaper_clear_for_output(struct server *server, const char *output_name)
{
	if (!server || string_null_or_empty(output_name)) {
		return;
	}

	struct output *output = output_from_name(server, output_name);
	if (!output) {
		return;
	}

	destroy_output_wallpaper_source(output);
	schedule_output_frame(output);
}

void
hyalo_wallpaper_clear(struct server *server)
{
	if (!server) {
		return;
	}

	if (server->hyalo_wallpaper_surface) {
		cairo_surface_destroy(server->hyalo_wallpaper_surface);
		server->hyalo_wallpaper_surface = NULL;
	}
	zfree(server->hyalo_wallpaper_path);

	struct output *output;
	wl_list_for_each(output, &server->outputs, link) {
		destroy_output_wallpaper_source(output);
		schedule_output_frame(output);
	}
}

void
hyalo_wallpaper_finish(struct server *server)
{
	hyalo_wallpaper_clear(server);
}