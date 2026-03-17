#include <gtkmm/application.h>

#include <cstdlib>
#include <string_view>

#include <glib.h>

#include "hyalo-core/config_manager.hpp"
#include "hyalo-core/localization.hpp"
#include "hyalo-core/style_manager.hpp"
#include "hyalo-panel/panel_window.hpp"

namespace {

void configure_stable_gsk_renderer() {
    const auto* gsk_renderer = std::getenv("GSK_RENDERER");
    if (!gsk_renderer || gsk_renderer[0] == '\0') {
        g_setenv("GSK_RENDERER", "gl", TRUE);
    }

    const auto* gdk_disable = std::getenv("GDK_DISABLE");
    if (!gdk_disable || gdk_disable[0] == '\0') {
        g_setenv("GDK_DISABLE", "vulkan", TRUE);
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    configure_stable_gsk_renderer();

    const auto* session_type = std::getenv("XDG_SESSION_TYPE");
    const auto* wayland_display = std::getenv("WAYLAND_DISPLAY");
    const auto* gsk_renderer = std::getenv("GSK_RENDERER");

    if ((!session_type || std::string_view(session_type) != "wayland")
        && (!wayland_display || wayland_display[0] == '\0')
        && (!gsk_renderer || gsk_renderer[0] == '\0')) {
        g_setenv("GSK_RENDERER", "cairo", TRUE);
    }

    auto application = Gtk::Application::create(
        "org.hyalo.Panel",
        Gio::Application::Flags::NON_UNIQUE
    );

    auto config_manager = hyalo::core::ConfigManager(hyalo::core::detect_runtime_paths());
    config_manager.load();

    auto localization = hyalo::core::Localization(config_manager);
    localization.load();

    hyalo::core::StyleManager::apply(config_manager);

    return application->make_window_and_run<hyalo::panel::PanelWindow>(
        argc,
        argv,
        config_manager,
        localization
    );
}