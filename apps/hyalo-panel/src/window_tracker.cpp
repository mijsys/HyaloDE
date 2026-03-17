#include "hyalo-panel/window_tracker.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <unordered_map>
#include <utility>

#include <glib.h>

#if HYALO_PANEL_HAS_WLR_FOREIGN_TOPLEVEL
#include <poll.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#endif

namespace hyalo::panel {

namespace {

std::string make_valid_utf8(std::string_view text) {
    if (g_utf8_validate(text.data(), static_cast<gssize>(text.size()), nullptr)) {
        return std::string(text);
    }

    auto* valid = g_utf8_make_valid(text.data(), static_cast<gssize>(text.size()));
    if (!valid) {
        return {};
    }

    std::string normalized(valid);
    g_free(valid);
    return normalized;
}

struct WorkspaceRecord {
    std::uint64_t creation_id = 0;
    std::string app_id;
    std::string title;
    std::string workspace;
};

std::string export_file_path() {
    const auto* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir || runtime_dir[0] == '\0') {
        runtime_dir = "/tmp";
    }

    return std::string(runtime_dir) + "/hyalo/window-workspaces-v1.tsv";
}

std::string command_file_path() {
    const auto* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir || runtime_dir[0] == '\0') {
        runtime_dir = "/tmp";
    }

    return std::string(runtime_dir) + "/hyalo/window-commands-v1.tsv";
}

std::string sanitize_single_line_text(std::string_view text) {
    auto sanitized = make_valid_utf8(text);
    std::replace(sanitized.begin(), sanitized.end(), '\n', ' ');
    std::replace(sanitized.begin(), sanitized.end(), '\r', ' ');
    std::replace(sanitized.begin(), sanitized.end(), '\t', ' ');
    return sanitized;
}

std::optional<std::uint64_t> parse_window_sequence(const std::string& identifier) {
    static constexpr auto prefix = std::string_view{"window-"};
    if (!identifier.starts_with(prefix)) {
        return std::nullopt;
    }

    try {
        return static_cast<std::uint64_t>(std::stoull(identifier.substr(prefix.size())));
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<WorkspaceRecord> load_workspace_records() {
    auto input = std::ifstream(export_file_path());
    if (!input) {
        return {};
    }

    auto records = std::vector<WorkspaceRecord>{};
    auto line = std::string{};
    while (std::getline(input, line)) {
        auto stream = std::stringstream(line);
        auto record = WorkspaceRecord{};
        auto creation_id = std::string{};

        if (!std::getline(stream, creation_id, '\t')
            || !std::getline(stream, record.app_id, '\t')
            || !std::getline(stream, record.title, '\t')
            || !std::getline(stream, record.workspace)) {
            continue;
        }

        try {
            record.creation_id = static_cast<std::uint64_t>(std::stoull(creation_id));
        } catch (...) {
            continue;
        }

        record.app_id = make_valid_utf8(record.app_id);
        record.title = make_valid_utf8(record.title);
        record.workspace = make_valid_utf8(record.workspace);

        records.push_back(std::move(record));
    }

    std::sort(records.begin(), records.end(), [](const auto& left, const auto& right) {
        return left.creation_id < right.creation_id;
    });
    return records;
}

}  // namespace

class WindowTracker::Impl {
public:
    Callback callback;

    bool start() {
#if HYALO_PANEL_HAS_WLR_FOREIGN_TOPLEVEL
        display = wl_display_connect(nullptr);
        if (!display) {
            return false;
        }

        registry = wl_display_get_registry(display);
        wl_registry_add_listener(registry, &registry_listener, this);

        if (wl_display_roundtrip(display) < 0) {
            shutdown();
            return false;
        }

        if (!toplevel_manager) {
            shutdown();
            return false;
        }

        if (wl_display_roundtrip(display) < 0) {
            shutdown();
            return false;
        }

        sync_workspaces();
        emit();
        return true;
#else
        return false;
#endif
    }

    bool poll() {
#if HYALO_PANEL_HAS_WLR_FOREIGN_TOPLEVEL
        if (!display) {
            return false;
        }

        pollfd descriptor{};
        descriptor.fd = wl_display_get_fd(display);
        descriptor.events = POLLIN;

        const auto ready = ::poll(&descriptor, 1, 0);
        if (ready > 0 && (descriptor.revents & POLLIN)) {
            const auto success = wl_display_dispatch(display) >= 0;
            if (success) {
                sync_workspaces();
            }
            return success;
        }

        const auto success = wl_display_dispatch_pending(display) >= 0 && wl_display_flush(display) >= 0;
        if (success) {
            sync_workspaces();
        }
        return success;
#else
        return false;
#endif
    }

    bool active() const {
#if HYALO_PANEL_HAS_WLR_FOREIGN_TOPLEVEL
        return display != nullptr && toplevel_manager != nullptr;
#else
        return false;
#endif
    }

    bool activate(const std::string& identifier) {
#if HYALO_PANEL_HAS_WLR_FOREIGN_TOPLEVEL
        auto* window = find_window(identifier);
        if (!window || !seat) {
            return false;
        }

        if (window->snapshot.minimized) {
            zwlr_foreign_toplevel_handle_v1_unset_minimized(window->handle);
        }
        zwlr_foreign_toplevel_handle_v1_activate(window->handle, seat);
        return wl_display_flush(display) >= 0;
#else
        (void)identifier;
        return false;
#endif
    }

    bool set_minimized(const std::string& identifier, bool minimized) {
#if HYALO_PANEL_HAS_WLR_FOREIGN_TOPLEVEL
        auto* window = find_window(identifier);
        if (!window) {
            return false;
        }

        if (minimized) {
            zwlr_foreign_toplevel_handle_v1_set_minimized(window->handle);
        } else {
            zwlr_foreign_toplevel_handle_v1_unset_minimized(window->handle);
        }
        return wl_display_flush(display) >= 0;
#else
        (void)identifier;
        (void)minimized;
        return false;
#endif
    }

    bool close(const std::string& identifier) {
#if HYALO_PANEL_HAS_WLR_FOREIGN_TOPLEVEL
        auto* window = find_window(identifier);
        if (!window) {
            return false;
        }

        zwlr_foreign_toplevel_handle_v1_close(window->handle);
        return wl_display_flush(display) >= 0;
#else
        (void)identifier;
        return false;
#endif
    }

    ~Impl() {
        shutdown();
    }

private:
#if HYALO_PANEL_HAS_WLR_FOREIGN_TOPLEVEL
    struct WindowState {
        zwlr_foreign_toplevel_handle_v1* handle = nullptr;
        WindowSnapshot snapshot;
        std::optional<std::uint64_t> creation_id;
    };

    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_seat* seat = nullptr;
    zwlr_foreign_toplevel_manager_v1* toplevel_manager = nullptr;
    std::unordered_map<zwlr_foreign_toplevel_handle_v1*, WindowState> windows;
    std::unordered_map<wl_output*, std::string> outputs;
    std::uint32_t next_window_id = 1;

    static void handle_registry_global(
        void* data,
        wl_registry* registry,
        uint32_t name,
        const char* interface,
        uint32_t version
    ) {
        auto* self = static_cast<Impl*>(data);

        const auto interface_name = std::string_view(interface);
        if (interface_name == zwlr_foreign_toplevel_manager_v1_interface.name) {
            self->toplevel_manager = static_cast<zwlr_foreign_toplevel_manager_v1*>(
                wl_registry_bind(registry, name, &zwlr_foreign_toplevel_manager_v1_interface, std::min(version, 3u))
            );
            zwlr_foreign_toplevel_manager_v1_add_listener(self->toplevel_manager, &manager_listener, self);
            return;
        }

        if (interface_name == wl_seat_interface.name && !self->seat) {
            self->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
            return;
        }

        if (interface_name == wl_output_interface.name) {
            auto* output = static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, 1));
            self->outputs.emplace(output, "output-" + std::to_string(name));
        }
    }

    static void handle_registry_remove(void*, wl_registry*, uint32_t) {
    }

    static void handle_toplevel_created(
        void* data,
        zwlr_foreign_toplevel_manager_v1*,
        zwlr_foreign_toplevel_handle_v1* handle
    ) {
        auto* self = static_cast<Impl*>(data);

        zwlr_foreign_toplevel_handle_v1_add_listener(handle, &handle_listener, self);

        auto state = WindowState{};
        state.handle = handle;
        state.snapshot.identifier = "window-" + std::to_string(self->next_window_id++);
        self->windows.emplace(handle, std::move(state));
    }

    static void handle_toplevel_finished(void*, zwlr_foreign_toplevel_manager_v1*) {
    }

    static void handle_title(void* data, zwlr_foreign_toplevel_handle_v1* handle, const char* title) {
        auto* self = static_cast<Impl*>(data);
        auto iterator = self->windows.find(handle);
        if (iterator == self->windows.end()) {
            return;
        }

        iterator->second.snapshot.title = make_valid_utf8(title ? title : "");
    }

    static void handle_app_id(void* data, zwlr_foreign_toplevel_handle_v1* handle, const char* app_id) {
        auto* self = static_cast<Impl*>(data);
        auto iterator = self->windows.find(handle);
        if (iterator == self->windows.end()) {
            return;
        }

        iterator->second.snapshot.app_id = make_valid_utf8(app_id ? app_id : "");
    }

    static void handle_output_enter(void* data, zwlr_foreign_toplevel_handle_v1* handle, wl_output* output) {
        auto* self = static_cast<Impl*>(data);
        auto iterator = self->windows.find(handle);
        if (iterator == self->windows.end()) {
            return;
        }

        const auto output_iterator = self->outputs.find(output);
        if (output_iterator == self->outputs.end()) {
            return;
        }

        auto& target_outputs = iterator->second.snapshot.outputs;
        if (std::find(target_outputs.begin(), target_outputs.end(), output_iterator->second) == target_outputs.end()) {
            target_outputs.push_back(output_iterator->second);
        }
    }

    static void handle_output_leave(void* data, zwlr_foreign_toplevel_handle_v1* handle, wl_output* output) {
        auto* self = static_cast<Impl*>(data);
        auto iterator = self->windows.find(handle);
        if (iterator == self->windows.end()) {
            return;
        }

        const auto output_iterator = self->outputs.find(output);
        if (output_iterator == self->outputs.end()) {
            return;
        }

        auto& target_outputs = iterator->second.snapshot.outputs;
        target_outputs.erase(
            std::remove(target_outputs.begin(), target_outputs.end(), output_iterator->second),
            target_outputs.end()
        );
    }

    static void handle_state(void* data, zwlr_foreign_toplevel_handle_v1* handle, wl_array* state) {
        auto* self = static_cast<Impl*>(data);
        auto iterator = self->windows.find(handle);
        if (iterator == self->windows.end()) {
            return;
        }

        auto& snapshot = iterator->second.snapshot;
        snapshot.active = false;
        snapshot.minimized = false;
        snapshot.maximized = false;
        snapshot.fullscreen = false;

        const auto* entries = static_cast<const std::uint32_t*>(state->data);
        const auto count = state->size / sizeof(std::uint32_t);
        for (std::size_t index = 0; index < count; ++index) {
            switch (entries[index]) {
            case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED:
                snapshot.maximized = true;
                break;
            case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED:
                snapshot.minimized = true;
                break;
            case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED:
                snapshot.active = true;
                break;
            case ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN:
                snapshot.fullscreen = true;
                break;
            default:
                break;
            }
        }
    }

    static void handle_done(void* data, zwlr_foreign_toplevel_handle_v1*) {
        auto* self = static_cast<Impl*>(data);
        self->sync_workspaces();
        self->emit();
    }

    static void handle_closed(void* data, zwlr_foreign_toplevel_handle_v1* handle) {
        auto* self = static_cast<Impl*>(data);
        self->windows.erase(handle);
        zwlr_foreign_toplevel_handle_v1_destroy(handle);
        self->emit();
    }

    static void handle_parent(void*, zwlr_foreign_toplevel_handle_v1*, zwlr_foreign_toplevel_handle_v1*) {
    }

    void emit() const {
        if (!callback) {
            return;
        }

        auto snapshot = std::vector<WindowSnapshot>{};
        snapshot.reserve(windows.size());

        for (const auto& [_, state] : windows) {
            snapshot.push_back(state.snapshot);
        }

        std::sort(snapshot.begin(), snapshot.end(), [](const WindowSnapshot& left, const WindowSnapshot& right) {
            if (left.active != right.active) {
                return left.active > right.active;
            }

            if (left.minimized != right.minimized) {
                return left.minimized < right.minimized;
            }

            return left.identifier < right.identifier;
        });

        callback(snapshot);
    }

    WindowState* find_window(const std::string& identifier) {
        const auto iterator = std::find_if(windows.begin(), windows.end(), [&](auto& entry) {
            return entry.second.snapshot.identifier == identifier;
        });
        return iterator == windows.end() ? nullptr : &iterator->second;
    }

    void sync_workspaces() {
        auto records = load_workspace_records();
        if (records.empty()) {
            for (auto& [_, state] : windows) {
                state.snapshot.workspace.clear();
            }
            return;
        }

        auto record_by_creation_id = std::unordered_map<std::uint64_t, const WorkspaceRecord*>{};
        record_by_creation_id.reserve(records.size());
        for (const auto& record : records) {
            record_by_creation_id.emplace(record.creation_id, &record);
        }

        auto claimed_creation_ids = std::unordered_set<std::uint64_t>{};
        claimed_creation_ids.reserve(windows.size());

        for (auto& [_, state] : windows) {
            if (!state.creation_id) {
                state.snapshot.workspace.clear();
                continue;
            }

            const auto iterator = record_by_creation_id.find(*state.creation_id);
            if (iterator == record_by_creation_id.end()) {
                state.creation_id.reset();
                state.snapshot.workspace.clear();
                continue;
            }

            state.snapshot.workspace = iterator->second->workspace;
            claimed_creation_ids.insert(*state.creation_id);
        }

        auto unassigned_windows = std::vector<WindowState*>{};
        unassigned_windows.reserve(windows.size());
        for (auto& [_, state] : windows) {
            if (!state.creation_id) {
                unassigned_windows.push_back(&state);
            }
        }

        std::sort(unassigned_windows.begin(), unassigned_windows.end(), [](const auto* left, const auto* right) {
            return parse_window_sequence(left->snapshot.identifier).value_or(UINT64_MAX)
                < parse_window_sequence(right->snapshot.identifier).value_or(UINT64_MAX);
        });

        for (auto* state : unassigned_windows) {
            const auto iterator = std::find_if(records.begin(), records.end(), [&](const auto& record) {
                return !claimed_creation_ids.contains(record.creation_id)
                    && record.app_id == state->snapshot.app_id
                    && record.title == state->snapshot.title;
            });

            if (iterator == records.end()) {
                continue;
            }

            state->creation_id = iterator->creation_id;
            state->snapshot.workspace = iterator->workspace;
            claimed_creation_ids.insert(iterator->creation_id);
        }

        for (auto* state : unassigned_windows) {
            if (state->creation_id) {
                continue;
            }

            const auto iterator = std::find_if(records.begin(), records.end(), [&](const auto& record) {
                return !claimed_creation_ids.contains(record.creation_id);
            });

            if (iterator == records.end()) {
                state->snapshot.workspace.clear();
                continue;
            }

            state->creation_id = iterator->creation_id;
            state->snapshot.workspace = iterator->workspace;
            claimed_creation_ids.insert(iterator->creation_id);
        }
    }

    void shutdown() {
        for (auto& [handle, _] : windows) {
            zwlr_foreign_toplevel_handle_v1_destroy(handle);
        }
        windows.clear();

        for (auto& [output, _] : outputs) {
            wl_output_destroy(output);
        }
        outputs.clear();

        if (seat) {
            wl_seat_destroy(seat);
            seat = nullptr;
        }

        if (toplevel_manager) {
            zwlr_foreign_toplevel_manager_v1_stop(toplevel_manager);
            zwlr_foreign_toplevel_manager_v1_destroy(toplevel_manager);
            toplevel_manager = nullptr;
        }

        if (registry) {
            wl_registry_destroy(registry);
            registry = nullptr;
        }

        if (display) {
            wl_display_disconnect(display);
            display = nullptr;
        }
    }

    static inline const wl_registry_listener registry_listener = {
        .global = handle_registry_global,
        .global_remove = handle_registry_remove,
    };

    static inline zwlr_foreign_toplevel_manager_v1_listener manager_listener = [] {
        auto listener = zwlr_foreign_toplevel_manager_v1_listener{};
        listener.toplevel = handle_toplevel_created;
        listener.finished = handle_toplevel_finished;
        return listener;
    }();

    static inline zwlr_foreign_toplevel_handle_v1_listener handle_listener = [] {
        auto listener = zwlr_foreign_toplevel_handle_v1_listener{};
        listener.title = handle_title;
        listener.app_id = handle_app_id;
        listener.output_enter = handle_output_enter;
        listener.output_leave = handle_output_leave;
        listener.state = handle_state;
        listener.done = handle_done;
        listener.closed = handle_closed;
        listener.parent = handle_parent;
        return listener;
    }();
#else
    void shutdown() {
    }
#endif
};

WindowTracker::WindowTracker()
    : impl_(new Impl()) {
}

WindowTracker::~WindowTracker() {
    delete impl_;
}

bool WindowTracker::start() {
    return impl_->start();
}

bool WindowTracker::poll() {
    return impl_->poll();
}

bool WindowTracker::active() const {
    return impl_->active();
}

bool WindowTracker::activate(const std::string& identifier) {
    return impl_->activate(identifier);
}

bool WindowTracker::set_minimized(const std::string& identifier, bool minimized) {
    return impl_->set_minimized(identifier, minimized);
}

bool WindowTracker::close(const std::string& identifier) {
    return impl_->close(identifier);
}

bool WindowTracker::move_to_workspace(const std::string& identifier, const std::string& workspace_name) {
    if (!parse_window_sequence(identifier).has_value()) {
        return false;
    }

    const auto command_path = command_file_path();
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(command_path).parent_path(), error);

    auto output = std::ofstream(command_path, std::ios::app);
    if (!output.is_open()) {
        return false;
    }

    output << "move-window\t"
           << sanitize_single_line_text(identifier)
           << '\t'
           << sanitize_single_line_text(workspace_name)
           << '\n';
    return output.good();
}

void WindowTracker::set_callback(Callback callback) {
    impl_->callback = std::move(callback);
}

}  // namespace hyalo::panel