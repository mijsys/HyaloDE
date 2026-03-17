#include <gtkmm.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <gdk/gdk.h>

#if HYALO_HAS_GTK4_LAYER_SHELL
#include <gtk4-layer-shell.h>
#endif

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

class WallpaperWindow : public Gtk::Window {
public:
    WallpaperWindow(GdkMonitor* monitor, const Glib::RefPtr<Gdk::Paintable>& paintable) {
        set_decorated(false);
        set_resizable(false);
        set_can_focus(false);
        set_focusable(false);
        set_name("hyalo-wallpaper-window");

        picture_.set_can_shrink(false);
        picture_.set_halign(Gtk::Align::FILL);
        picture_.set_valign(Gtk::Align::FILL);
        picture_.set_content_fit(Gtk::ContentFit::COVER);
        picture_.set_paintable(paintable);
        set_child(picture_);

#if HYALO_HAS_GTK4_LAYER_SHELL
        gtk_layer_init_for_window(GTK_WINDOW(gobj()));
        gtk_layer_set_layer(GTK_WINDOW(gobj()), GTK_LAYER_SHELL_LAYER_BACKGROUND);
        gtk_layer_set_namespace(GTK_WINDOW(gobj()), "hyalo-wallpaper");
        gtk_layer_set_anchor(GTK_WINDOW(gobj()), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(gobj()), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(gobj()), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(gobj()), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
        gtk_layer_set_keyboard_mode(GTK_WINDOW(gobj()), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
        if (monitor != nullptr) {
            gtk_layer_set_monitor(GTK_WINDOW(gobj()), monitor);
        }
#else
        fullscreen();
#endif
    }

private:
    Gtk::Picture picture_;
};

std::vector<GdkMonitor*> monitors_for_display(GdkDisplay* display) {
    auto monitors = std::vector<GdkMonitor*>{};
    if (display == nullptr) {
        return monitors;
    }

    auto* list = gdk_display_get_monitors(display);
    if (list == nullptr) {
        return monitors;
    }

    const auto count = g_list_model_get_n_items(G_LIST_MODEL(list));
    for (guint index = 0; index < count; ++index) {
        auto* monitor = GDK_MONITOR(g_list_model_get_item(G_LIST_MODEL(list), index));
        if (monitor != nullptr) {
            monitors.push_back(monitor);
        }
    }
    return monitors;
}

struct WallpaperRuntime {
    Gtk::Application* app = nullptr;
    Glib::RefPtr<Gdk::Paintable> paintable;
    std::vector<std::unique_ptr<WallpaperWindow>> windows;
};

void sync_windows(WallpaperRuntime& runtime) {
    if (runtime.app == nullptr || !runtime.paintable) {
        return;
    }

    for (auto& window : runtime.windows) {
        runtime.app->remove_window(*window);
        window->hide();
        window->close();
    }
    runtime.windows.clear();

    const auto display = Gdk::Display::get_default();
    if (!display) {
        return;
    }

    for (auto* monitor : monitors_for_display(display->gobj())) {
        runtime.windows.push_back(std::make_unique<WallpaperWindow>(monitor, runtime.paintable));
        runtime.app->add_window(*runtime.windows.back());
        runtime.windows.back()->present();
        g_object_unref(monitor);
    }

    if (runtime.windows.empty()) {
        runtime.windows.push_back(std::make_unique<WallpaperWindow>(nullptr, runtime.paintable));
        runtime.app->add_window(*runtime.windows.back());
        runtime.windows.back()->present();
    }
}

void on_monitors_changed(GListModel*, guint, guint, guint, gpointer user_data) {
    auto* runtime = static_cast<WallpaperRuntime*>(user_data);
    if (runtime == nullptr) {
        return;
    }

    sync_windows(*runtime);
}

}  // namespace

int main(int argc, char** argv) {
    configure_stable_gsk_renderer();

    auto app = Gtk::Application::create("org.hyalo.Wallpaper");

    Glib::OptionContext option_context;
    std::string image_path;
    Glib::OptionGroup option_group("hyalo-wallpaper", "hyalo-wallpaper", "Wallpaper options");
    Glib::OptionEntry image_option;
    image_option.set_long_name("image");
    image_option.set_short_name('i');
    image_option.set_description("Path to wallpaper image");
    image_option.set_arg_description("FILE");
    option_group.add_entry_filename(image_option, image_path);

    bool check_only = false;
    Glib::OptionEntry check_option;
    check_option.set_long_name("check");
    check_option.set_description("Validate that the image can be loaded and exit");
    option_group.add_entry(check_option, check_only);
    option_context.set_main_group(option_group);

    try {
        option_context.parse(argc, argv);
    } catch (const Glib::Error& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }

    if (image_path.empty()) {
        std::cerr << "missing --image\n";
        return 2;
    }

    const auto resolved_path = std::filesystem::path(image_path);
    if (!std::filesystem::is_regular_file(resolved_path)) {
        std::cerr << "image does not exist: " << resolved_path << '\n';
        return 1;
    }

    auto texture = Gdk::Texture::create_from_filename(resolved_path.string());
    if (!texture) {
        std::cerr << "failed to load image: " << resolved_path << '\n';
        return 1;
    }

    if (check_only) {
        return 0;
    }

    const auto display = Gdk::Display::get_default();
    if (!display) {
        std::cerr << "no display available\n";
        return 1;
    }

    auto runtime = WallpaperRuntime{};
    runtime.app = app.get();
    runtime.paintable = texture;

    auto* monitor_list = gdk_display_get_monitors(display->gobj());
    if (monitor_list != nullptr) {
        g_signal_connect(monitor_list, "items-changed", G_CALLBACK(on_monitors_changed), &runtime);
    }

    sync_windows(runtime);

    return app->run();
}