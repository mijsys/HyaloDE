// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L

#include "hyalo-command.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/string-helpers.h"
#include "labwc.h"
#include "hyalo-export.h"
#include "hyalo-wallpaper.h"
#include "view.h"
#include "workspaces.h"

enum {
	HYALO_COMMAND_POLL_MS = 180,
};

static const char *
hyalo_runtime_dir(void)
{
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!runtime_dir || !runtime_dir[0]) {
		runtime_dir = "/tmp";
	}
	return runtime_dir;
}

static bool
hyalo_command_path(char *buffer, size_t buffer_size, const char *suffix)
{
	return snprintf(buffer, buffer_size, "%s/hyalo/%s",
		hyalo_runtime_dir(), suffix) < (int)buffer_size;
}

static void
hyalo_write_capabilities_file(void)
{
	char runtime_path[PATH_MAX];
	char capability_path[PATH_MAX];

	if (!hyalo_command_path(runtime_path, sizeof(runtime_path), "")
		|| !hyalo_command_path(capability_path, sizeof(capability_path),
			"capabilities-v1.tsv")) {
		return;
	}

	if (mkdir(runtime_path, 0700) < 0 && errno != EEXIST) {
		return;
	}

	FILE *stream = fopen(capability_path, "w");
	if (!stream) {
		return;
	}

	fputs("wallpaper\t1\n", stream);
	fclose(stream);
}

static void
hyalo_remove_capabilities_file(void)
{
	char capability_path[PATH_MAX];
	if (!hyalo_command_path(capability_path, sizeof(capability_path),
			"capabilities-v1.tsv")) {
		return;
	}

	unlink(capability_path);
}

static struct view *
find_view_by_identifier(struct server *server, const char *identifier)
{
	static const char prefix[] = "window-";
	if (!identifier || strncmp(identifier, prefix, strlen(prefix)) != 0) {
		return NULL;
	}

	char *end = NULL;
	const uint64_t creation_id = strtoull(identifier + strlen(prefix), &end, 10);
	if (!end || *end != '\0') {
		return NULL;
	}

	struct view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->creation_id == creation_id) {
			return view;
		}
	}

	return NULL;
}

static bool
handle_move_window_command(struct server *server, const char *identifier, const char *workspace_name)
{
	struct view *view = find_view_by_identifier(server, identifier);
	if (!view || !workspace_name || !workspace_name[0]) {
		return false;
	}

	struct workspace *target = workspaces_find(server->workspaces.current,
		workspace_name, false);
	if (!target) {
		return false;
	}

	const bool moving_active_away = view == server->active_view
		&& view->workspace == server->workspaces.current
		&& target != server->workspaces.current
		&& !view->visible_on_all_workspaces;

	view_move_to_workspace(view, target);
	if (moving_active_away) {
		desktop_focus_topmost_view(server);
	}

	desktop_update_top_layer_visibility(server);
	cursor_update_focus(server);
	return true;
}

static bool
handle_wallpaper_command(struct server *server,
	const char *output_name, const char *path)
{
	if (!string_null_or_empty(output_name)) {
		if (string_null_or_empty(path)) {
			hyalo_wallpaper_clear_for_output(server, output_name);
			return true;
		}
		return hyalo_wallpaper_set_for_output(server, output_name, path);
	}

	if (string_null_or_empty(path)) {
		hyalo_wallpaper_clear(server);
		return true;
	}

	return hyalo_wallpaper_set(server, path);
}

static void
process_command_file(struct server *server, FILE *stream)
{
	char *line = NULL;
	size_t capacity = 0;
	bool workspace_changed = false;

	while (getline(&line, &capacity, stream) >= 0) {
		char *saveptr = NULL;
		char *command = strtok_r(line, "\t\n", &saveptr);
		if (!command) {
			continue;
		}

		if (!strcmp(command, "move-window")) {
			char *identifier = strtok_r(NULL, "\t\n", &saveptr);
			char *workspace_name = strtok_r(NULL, "\t\n", &saveptr);
			workspace_changed = handle_move_window_command(server, identifier, workspace_name)
				|| workspace_changed;
		} else if (!strcmp(command, "set-wallpaper")) {
			char *first = strtok_r(NULL, "\t\n", &saveptr);
			char *second = strtok_r(NULL, "\n", &saveptr);
			if (second) {
				handle_wallpaper_command(server, first, second);
			} else {
				handle_wallpaper_command(server, NULL, first);
			}
		} else if (!strcmp(command, "clear-wallpaper")) {
			char *output_name = strtok_r(NULL, "\t\n", &saveptr);
			handle_wallpaper_command(server, output_name, NULL);
		}
	}

	free(line);
	if (workspace_changed) {
		hyalo_export_window_workspaces(server);
	}
}

static int
handle_hyalo_command_timer(void *data)
{
	struct server *server = data;
	char command_path[PATH_MAX];
	char processing_path[PATH_MAX];
	char runtime_path[PATH_MAX];

	if (!hyalo_command_path(runtime_path, sizeof(runtime_path), "")
		|| !hyalo_command_path(command_path, sizeof(command_path), "window-commands-v1.tsv")
		|| !hyalo_command_path(processing_path, sizeof(processing_path), "window-commands-v1.processing.tsv")) {
		goto rearm;
	}

	if (mkdir(runtime_path, 0700) < 0 && errno != EEXIST) {
		goto rearm;
	}

	if (rename(command_path, processing_path) < 0) {
		if (errno != ENOENT) {
			wlr_log(WLR_DEBUG, "Hyalo command rename failed: %s", strerror(errno));
		}
		goto rearm;
	}

	FILE *stream = fopen(processing_path, "r");
	if (stream) {
		process_command_file(server, stream);
		fclose(stream);
	}
	unlink(processing_path);

rearm:
	wl_event_source_timer_update(server->hyalo_command_timer, HYALO_COMMAND_POLL_MS);
	return 0;
}

void
hyalo_command_bridge_init(struct server *server)
{
	hyalo_write_capabilities_file();

	server->hyalo_command_timer = wl_event_loop_add_timer(server->wl_event_loop,
		handle_hyalo_command_timer, server);
	if (!server->hyalo_command_timer) {
		wlr_log(WLR_ERROR, "unable to create Hyalo command bridge timer");
		return;
	}

	wl_event_source_timer_update(server->hyalo_command_timer, HYALO_COMMAND_POLL_MS);
}

void
hyalo_command_bridge_finish(struct server *server)
{
	hyalo_remove_capabilities_file();

	if (!server->hyalo_command_timer) {
		return;
	}

	wl_event_source_remove(server->hyalo_command_timer);
	server->hyalo_command_timer = NULL;
}