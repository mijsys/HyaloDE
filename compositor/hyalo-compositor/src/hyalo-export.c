// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L

#include "hyalo-export.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "labwc.h"
#include "view.h"
#include "workspaces.h"

struct export_view_record {
	struct view *view;
	uint64_t creation_id;
};

static int
compare_export_view_record(const void *left, const void *right)
{
	const struct export_view_record *left_record = left;
	const struct export_view_record *right_record = right;

	if (left_record->creation_id < right_record->creation_id) {
		return -1;
	}
	if (left_record->creation_id > right_record->creation_id) {
		return 1;
	}
	return 0;
}

static void
write_sanitized(FILE *stream, const char *value)
{
	if (!value) {
		return;
	}

	for (const unsigned char *cursor = (const unsigned char *)value;
			*cursor; ++cursor) {
		switch (*cursor) {
		case '\t':
		case '\n':
		case '\r':
			fputc(' ', stream);
			break;
		default:
			fputc(*cursor, stream);
			break;
		}
	}
}

void
hyalo_export_window_workspaces(struct server *server)
{
	if (!server) {
		return;
	}

	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!runtime_dir || !runtime_dir[0]) {
		runtime_dir = "/tmp";
	}

	char export_dir[PATH_MAX];
	if (snprintf(export_dir, sizeof(export_dir), "%s/hyalo", runtime_dir)
			>= (int)sizeof(export_dir)) {
		return;
	}

	if (mkdir(export_dir, 0700) < 0 && errno != EEXIST) {
		return;
	}

	char export_path[PATH_MAX];
	char temporary_path[PATH_MAX];
	if (snprintf(export_path, sizeof(export_path),
			"%s/window-workspaces-v1.tsv", export_dir) >= (int)sizeof(export_path)
			|| snprintf(temporary_path, sizeof(temporary_path),
			"%s/window-workspaces-v1.tsv.tmp", export_dir) >= (int)sizeof(temporary_path)) {
		return;
	}

	size_t mapped_count = 0;
	struct view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->mapped && view->workspace) {
			++mapped_count;
		}
	}

	struct export_view_record *records = NULL;
	if (mapped_count > 0) {
		records = calloc(mapped_count, sizeof(*records));
		if (!records) {
			return;
		}

		size_t index = 0;
		wl_list_for_each(view, &server->views, link) {
			if (!view->mapped || !view->workspace) {
				continue;
			}

			records[index++] = (struct export_view_record){
				.view = view,
				.creation_id = view->creation_id,
			};
		}

		qsort(records, mapped_count, sizeof(*records), compare_export_view_record);
	}

	FILE *stream = fopen(temporary_path, "w");
	if (!stream) {
		free(records);
		return;
	}

	for (size_t index = 0; index < mapped_count; ++index) {
		view = records[index].view;
		const char *workspace_name = view->visible_on_all_workspaces
			? ""
			: (view->workspace && view->workspace->name ? view->workspace->name : "");

		fprintf(stream, "%llu\t",
			(unsigned long long)records[index].creation_id);
		write_sanitized(stream, view->app_id);
		fputc('\t', stream);
		write_sanitized(stream, view->title);
		fputc('\t', stream);
		write_sanitized(stream, workspace_name);
		fputc('\n', stream);
	}

	const bool write_failed = fflush(stream) != 0 || fsync(fileno(stream)) != 0;
	if (fclose(stream) != 0 || write_failed) {
		unlink(temporary_path);
		free(records);
		return;
	}

	if (rename(temporary_path, export_path) < 0) {
		unlink(temporary_path);
	}

	free(records);
}