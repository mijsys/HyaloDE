#pragma once

#include <functional>
#include <string>
#include <vector>

namespace hyalo::panel {

struct WorkspaceSnapshot {
    std::string identifier;
    std::string name;
    bool active = false;
    bool urgent = false;
    bool hidden = false;
    bool can_activate = false;
    bool can_remove = false;
};

class WorkspaceTracker {
public:
    using Callback = std::function<void(const std::vector<WorkspaceSnapshot>&)>;

    WorkspaceTracker();
    ~WorkspaceTracker();

    WorkspaceTracker(const WorkspaceTracker&) = delete;
    WorkspaceTracker& operator=(const WorkspaceTracker&) = delete;

    bool start();
    bool poll();
    [[nodiscard]] bool active() const;
    [[nodiscard]] bool can_create_workspace() const;
    bool activate(const std::string& identifier);
    bool create_workspace(const std::string& name);
    bool remove_workspace(const std::string& identifier);
    void set_callback(Callback callback);

private:
    class Impl;
    Impl* impl_;
};

}  // namespace hyalo::panel