#pragma once

#include <functional>
#include <string>
#include <vector>

namespace hyalo::panel {

struct WindowSnapshot {
    std::string identifier;
    std::string title;
    std::string app_id;
    bool active = false;
    bool minimized = false;
    bool maximized = false;
    bool fullscreen = false;
    std::string workspace;
    std::vector<std::string> outputs;
};

class WindowTracker {
public:
    using Callback = std::function<void(const std::vector<WindowSnapshot>&)>;

    WindowTracker();
    ~WindowTracker();

    WindowTracker(const WindowTracker&) = delete;
    WindowTracker& operator=(const WindowTracker&) = delete;

    bool start();
    bool poll();
    [[nodiscard]] bool active() const;
    bool activate(const std::string& identifier);
    bool set_minimized(const std::string& identifier, bool minimized);
    bool close(const std::string& identifier);
    bool move_to_workspace(const std::string& identifier, const std::string& workspace_name);
    void set_callback(Callback callback);

private:
    class Impl;
    Impl* impl_;
};

}  // namespace hyalo::panel