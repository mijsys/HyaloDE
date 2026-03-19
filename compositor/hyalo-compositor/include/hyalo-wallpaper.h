/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_HYALO_WALLPAPER_H
#define LABWC_HYALO_WALLPAPER_H

#include <stdbool.h>

struct server;
struct output;

bool hyalo_wallpaper_set(struct server *server, const char *path);
bool hyalo_wallpaper_set_for_output(struct server *server,
	const char *output_name, const char *path);
void hyalo_wallpaper_clear(struct server *server);
void hyalo_wallpaper_clear_for_output(struct server *server,
	const char *output_name);
void hyalo_wallpaper_finish(struct server *server);
void hyalo_wallpaper_output_init(struct output *output);
void hyalo_wallpaper_output_destroy(struct output *output);
void hyalo_wallpaper_output_reconfigure(struct output *output);

#endif /* LABWC_HYALO_WALLPAPER_H */