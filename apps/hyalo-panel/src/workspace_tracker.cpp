#include "hyalo-panel/workspace_tracker.hpp"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <glib.h>

#if HYALO_PANEL_HAS_EXT_WORKSPACE
#include <poll.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#include "ext-workspace-v1-client-protocol.h"
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

}  // namespace

class WorkspaceTracker::Impl {
public:
    Callback callback;

    bool start() {
#if HYALO_PANEL_HAS_EXT_WORKSPACE
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

        if (!workspace_manager) {
            shutdown();
            return false;
        }

        if (wl_display_roundtrip(display) < 0) {
            shutdown();
            return false;
        }

        emit();
        return true;
#else
        return false;
#endif
    }

    bool poll() {
#if HYALO_PANEL_HAS_EXT_WORKSPACE
        if (!display) {
            return false;
        }

        pollfd descriptor{};
        descriptor.fd = wl_display_get_fd(display);
        descriptor.events = POLLIN;

        const auto ready = ::poll(&descriptor, 1, 0);
        if (ready > 0 && (descriptor.revents & POLLIN)) {
            return wl_display_dispatch(display) >= 0;
        }

        return wl_display_dispatch_pending(display) >= 0 && wl_display_flush(display) >= 0;
#else
        return false;
#endif
    }

    bool active() const {
#if HYALO_PANEL_HAS_EXT_WORKSPACE
        return display != nullptr && workspace_manager != nullptr;
#else
        return false;
#endif
    }

        bool can_create_workspace() const {
    #if HYALO_PANEL_HAS_EXT_WORKSPACE
        return std::any_of(groups.begin(), groups.end(), [](const auto& group) {
            return group.can_create_workspace;
        });
    #else
        return false;
    #endif
        }

    bool activate(const std::string& identifier) {
#if HYALO_PANEL_HAS_EXT_WORKSPACE
        auto* workspace = find_workspace(identifier);
        if (!workspace || !workspace->snapshot.can_activate) {
            return false;
        }

        ext_workspace_handle_v1_activate(workspace->handle);
        ext_workspace_manager_v1_commit(workspace_manager);
        return wl_display_flush(display) >= 0;
#else
        (void)identifier;
        return false;
#endif
    }

    bool create_workspace(const std::string& name) {
#if HYALO_PANEL_HAS_EXT_WORKSPACE
        const auto iterator = std::find_if(groups.begin(), groups.end(), [](const auto& group) {
            return group.can_create_workspace;
        });
        if (iterator == groups.end()) {
            return false;
        }

        ext_workspace_group_handle_v1_create_workspace(iterator->handle, name.c_str());
        ext_workspace_manager_v1_commit(workspace_manager);
        return wl_display_flush(display) >= 0;
#else
        (void)name;
        return false;
#endif
    }

    bool remove_workspace(const std::string& identifier) {
#if HYALO_PANEL_HAS_EXT_WORKSPACE
        auto* workspace = find_workspace(identifier);
        if (!workspace || !workspace->snapshot.can_remove) {
            return false;
        }

        ext_workspace_handle_v1_remove(workspace->handle);
        ext_workspace_manager_v1_commit(workspace_manager);
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
#if HYALO_PANEL_HAS_EXT_WORKSPACE
    struct WorkspaceState {
        ext_workspace_handle_v1* handle = nullptr;
        WorkspaceSnapshot snapshot;
    };

    struct WorkspaceGroupState {
        ext_workspace_group_handle_v1* handle = nullptr;
        bool can_create_workspace = false;
    };

    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    ext_workspace_manager_v1* workspace_manager = nullptr;
    std::unordered_map<ext_workspace_handle_v1*, WorkspaceState> workspaces;
    std::vector<WorkspaceGroupState> groups;
    std::uint32_t next_workspace_id = 1;

    static void handle_registry_global(
        void* data,
        wl_registry* registry,
        uint32_t name,
        const char* interface,
        uint32_t version
    ) {
        auto* self = static_cast<Impl*>(data);
        if (std::string_view(interface) != ext_workspace_manager_v1_interface.name) {
            return;
        }

        self->workspace_manager = static_cast<ext_workspace_manager_v1*>(
            wl_registry_bind(registry, name, &ext_workspace_manager_v1_interface, std::min(version, 1u))
        );
        ext_workspace_manager_v1_add_listener(self->workspace_manager, &manager_listener, self);
    }

    static void handle_registry_remove(void*, wl_registry*, uint32_t) {
    }

    static void handle_workspace_group_created(
        void* data,
        ext_workspace_manager_v1*,
        ext_workspace_group_handle_v1* group
    ) {
        auto* self = static_cast<Impl*>(data);
        ext_workspace_group_handle_v1_add_listener(group, &group_listener, self);
        self->groups.push_back(WorkspaceGroupState{.handle = group});
    }

    static void handle_workspace_created(
        void* data,
        ext_workspace_manager_v1*,
        ext_workspace_handle_v1* workspace
    ) {
        auto* self = static_cast<Impl*>(data);
        ext_workspace_handle_v1_add_listener(workspace, &workspace_listener, self);

        auto state = WorkspaceState{};
        state.handle = workspace;
        state.snapshot.identifier = "workspace-" + std::to_string(self->next_workspace_id++);
        self->workspaces.emplace(workspace, std::move(state));
    }

    static void handle_manager_done(void* data, ext_workspace_manager_v1*) {
        static_cast<Impl*>(data)->emit();
    }

    static void handle_manager_finished(void* data, ext_workspace_manager_v1*) {
        auto* self = static_cast<Impl*>(data);
        self->workspace_manager = nullptr;
    }

    static void handle_workspace_id(void* data, ext_workspace_handle_v1* workspace, const char* id) {
        auto* self = static_cast<Impl*>(data);
        const auto iterator = self->workspaces.find(workspace);
        if (iterator == self->workspaces.end()) {
            return;
        }

        iterator->second.snapshot.identifier = id ? make_valid_utf8(id) : iterator->second.snapshot.identifier;
    }

    static void handle_workspace_name(void* data, ext_workspace_handle_v1* workspace, const char* name) {
        auto* self = static_cast<Impl*>(data);
        const auto iterator = self->workspaces.find(workspace);
        if (iterator == self->workspaces.end()) {
            return;
        }

        iterator->second.snapshot.name = make_valid_utf8(name ? name : "");
    }

    static void handle_workspace_coordinates(void*, ext_workspace_handle_v1*, wl_array*) {
    }

    static void handle_workspace_state(void* data, ext_workspace_handle_v1* workspace, std::uint32_t state) {
        auto* self = static_cast<Impl*>(data);
        const auto iterator = self->workspaces.find(workspace);
        if (iterator == self->workspaces.end()) {
            return;
        }

        auto& snapshot = iterator->second.snapshot;
        snapshot.active = (state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE) != 0;
        snapshot.urgent = (state & EXT_WORKSPACE_HANDLE_V1_STATE_URGENT) != 0;
        snapshot.hidden = (state & EXT_WORKSPACE_HANDLE_V1_STATE_HIDDEN) != 0;
    }

    static void handle_workspace_capabilities(void* data, ext_workspace_handle_v1* workspace, std::uint32_t capabilities) {
        auto* self = static_cast<Impl*>(data);
        const auto iterator = self->workspaces.find(workspace);
        if (iterator == self->workspaces.end()) {
            return;
        }

        iterator->second.snapshot.can_activate = (capabilities & EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE) != 0;
        iterator->second.snapshot.can_remove = (capabilities & EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_REMOVE) != 0;
    }

    static void handle_workspace_removed(void* data, ext_workspace_handle_v1* workspace) {
        auto* self = static_cast<Impl*>(data);
        self->workspaces.erase(workspace);
        ext_workspace_handle_v1_destroy(workspace);
        self->emit();
    }

    static void handle_group_capabilities(void* data, ext_workspace_group_handle_v1* group, std::uint32_t capabilities) {
        auto* self = static_cast<Impl*>(data);
        const auto iterator = std::find_if(self->groups.begin(), self->groups.end(), [&](const auto& current) {
            return current.handle == group;
        });
        if (iterator == self->groups.end()) {
            return;
        }

        iterator->can_create_workspace = (capabilities & EXT_WORKSPACE_GROUP_HANDLE_V1_GROUP_CAPABILITIES_CREATE_WORKSPACE) != 0;
    }

    static void handle_group_output_enter(void*, ext_workspace_group_handle_v1*, wl_output*) {
    }

    static void handle_group_output_leave(void*, ext_workspace_group_handle_v1*, wl_output*) {
    }

    static void handle_group_workspace_enter(void*, ext_workspace_group_handle_v1*, ext_workspace_handle_v1*) {
    }

    static void handle_group_workspace_leave(void*, ext_workspace_group_handle_v1*, ext_workspace_handle_v1*) {
    }

    static void handle_group_removed(void* data, ext_workspace_group_handle_v1* group) {
        auto* self = static_cast<Impl*>(data);
        self->groups.erase(std::remove_if(self->groups.begin(), self->groups.end(), [&](const auto& current) {
            return current.handle == group;
        }), self->groups.end());
        ext_workspace_group_handle_v1_destroy(group);
    }

    void emit() const {
        if (!callback) {
            return;
        }

        auto snapshot = std::vector<WorkspaceSnapshot>{};
        snapshot.reserve(workspaces.size());

        for (const auto& [_, state] : workspaces) {
            snapshot.push_back(state.snapshot);
        }

        std::sort(snapshot.begin(), snapshot.end(), [](const WorkspaceSnapshot& left, const WorkspaceSnapshot& right) {
            return left.name < right.name;
        });

        callback(snapshot);
    }

    WorkspaceState* find_workspace(const std::string& identifier) {
        const auto iterator = std::find_if(workspaces.begin(), workspaces.end(), [&](auto& entry) {
            return entry.second.snapshot.identifier == identifier;
        });
        return iterator == workspaces.end() ? nullptr : &iterator->second;
    }

    void shutdown() {
        for (auto& [handle, _] : workspaces) {
            ext_workspace_handle_v1_destroy(handle);
        }
        workspaces.clear();

        for (auto& group : groups) {
            ext_workspace_group_handle_v1_destroy(group.handle);
        }
        groups.clear();

        if (workspace_manager) {
            ext_workspace_manager_v1_stop(workspace_manager);
            ext_workspace_manager_v1_destroy(workspace_manager);
            workspace_manager = nullptr;
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

    static inline ext_workspace_manager_v1_listener manager_listener = [] {
        auto listener = ext_workspace_manager_v1_listener{};
        listener.workspace_group = handle_workspace_group_created;
        listener.workspace = handle_workspace_created;
        listener.done = handle_manager_done;
        listener.finished = handle_manager_finished;
        return listener;
    }();

    static inline ext_workspace_group_handle_v1_listener group_listener = [] {
        auto listener = ext_workspace_group_handle_v1_listener{};
        listener.capabilities = handle_group_capabilities;
        listener.output_enter = handle_group_output_enter;
        listener.output_leave = handle_group_output_leave;
        listener.workspace_enter = handle_group_workspace_enter;
        listener.workspace_leave = handle_group_workspace_leave;
        listener.removed = handle_group_removed;
        return listener;
    }();

    static inline ext_workspace_handle_v1_listener workspace_listener = [] {
        auto listener = ext_workspace_handle_v1_listener{};
        listener.id = handle_workspace_id;
        listener.name = handle_workspace_name;
        listener.coordinates = handle_workspace_coordinates;
        listener.state = handle_workspace_state;
        listener.capabilities = handle_workspace_capabilities;
        listener.removed = handle_workspace_removed;
        return listener;
    }();
#else
    void shutdown() {
    }
#endif
};

WorkspaceTracker::WorkspaceTracker()
    : impl_(new Impl()) {
}

WorkspaceTracker::~WorkspaceTracker() {
    delete impl_;
}

bool WorkspaceTracker::start() {
    return impl_->start();
}

bool WorkspaceTracker::poll() {
    return impl_->poll();
}

bool WorkspaceTracker::active() const {
    return impl_->active();
}

bool WorkspaceTracker::can_create_workspace() const {
    return impl_->can_create_workspace();
}

bool WorkspaceTracker::activate(const std::string& identifier) {
    return impl_->activate(identifier);
}

bool WorkspaceTracker::create_workspace(const std::string& name) {
    return impl_->create_workspace(name);
}

bool WorkspaceTracker::remove_workspace(const std::string& identifier) {
    return impl_->remove_workspace(identifier);
}

void WorkspaceTracker::set_callback(Callback callback) {
    impl_->callback = std::move(callback);
}

}  // namespace hyalo::panel