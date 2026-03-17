#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <glib.h>
#include <vte/vte.h>

#include <gdkmm/display.h>
#include <gtkmm/application.h>
#include <gtkmm/applicationwindow.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/eventcontrollerscroll.h>

#include <gtkmm/label.h>
#include <gtkmm/notebook.h>
#include <gtkmm/popovermenu.h>
#include <gtkmm/widget.h>
#include <giomm/menu.h>
#include <giomm/simpleactiongroup.h>

#include "hyalo-core/config_manager.hpp"
#include "hyalo-core/style_manager.hpp"

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

std::string detect_shell() {
    const auto* shell = std::getenv("SHELL");
    if (shell && shell[0] != '\0') {
        return shell;
    }
    return "/bin/bash";
}

std::string detect_working_directory() {
    const auto* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return home;
    }

    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    return ec ? "/" : cwd.string();
}

struct TerminalConfig {
    std::string font_family = "Hack";
    int font_size = 11;
    int scrollback_lines = 10000;
    bool cursor_blink = true;
    std::string cursor_shape = "block";
    bool audible_bell = false;
    bool allow_hyperlinks = true;
    double opacity = 0.95;
};

TerminalConfig load_terminal_config(const hyalo::core::ConfigManager& config) {
    TerminalConfig tc;
    const auto& raw = config.raw();

    if (!raw.contains("terminal") || !raw["terminal"].is_object()) {
        return tc;
    }

    const auto& t = raw["terminal"];

    if (t.contains("font_family") && t["font_family"].is_string()) {
        tc.font_family = t["font_family"].get<std::string>();
    }
    if (t.contains("font_size") && t["font_size"].is_number_integer()) {
        tc.font_size = std::clamp(t["font_size"].get<int>(), 6, 72);
    }
    if (t.contains("scrollback_lines") && t["scrollback_lines"].is_number_integer()) {
        tc.scrollback_lines = std::max(0, t["scrollback_lines"].get<int>());
    }
    if (t.contains("cursor_blink") && t["cursor_blink"].is_boolean()) {
        tc.cursor_blink = t["cursor_blink"].get<bool>();
    }
    if (t.contains("cursor_shape") && t["cursor_shape"].is_string()) {
        tc.cursor_shape = t["cursor_shape"].get<std::string>();
    }
    if (t.contains("audible_bell") && t["audible_bell"].is_boolean()) {
        tc.audible_bell = t["audible_bell"].get<bool>();
    }
    if (t.contains("allow_hyperlinks") && t["allow_hyperlinks"].is_boolean()) {
        tc.allow_hyperlinks = t["allow_hyperlinks"].get<bool>();
    }
    if (t.contains("opacity") && t["opacity"].is_number()) {
        tc.opacity = std::clamp(t["opacity"].get<double>(), 0.0, 1.0);
    }

    return tc;
}

VteCursorShape parse_cursor_shape(const std::string& value) {
    if (value == "ibeam") {
        return VTE_CURSOR_SHAPE_IBEAM;
    }
    if (value == "underline") {
        return VTE_CURSOR_SHAPE_UNDERLINE;
    }
    return VTE_CURSOR_SHAPE_BLOCK;
}

void apply_terminal_colors(VteTerminal* vte, double opacity) {
    GdkRGBA fg = {0.93, 0.95, 0.98, 1.0};
    GdkRGBA bg = {0.09f, 0.12f, 0.15f, static_cast<float>(opacity)};

    const std::array<GdkRGBA, 16> palette = {{
        {0.15, 0.20, 0.25, 1.0},   // 0  black
        {0.80, 0.32, 0.35, 1.0},   // 1  red
        {0.50, 0.84, 0.73, 1.0},   // 2  green (accent)
        {0.90, 0.78, 0.46, 1.0},   // 3  yellow
        {0.45, 0.62, 0.82, 1.0},   // 4  blue
        {0.72, 0.52, 0.78, 1.0},   // 5  magenta
        {0.50, 0.78, 0.82, 1.0},   // 6  cyan
        {0.78, 0.82, 0.88, 1.0},   // 7  white
        {0.30, 0.36, 0.42, 1.0},   // 8  bright black
        {0.92, 0.42, 0.45, 1.0},   // 9  bright red
        {0.56, 0.90, 0.78, 1.0},   // 10 bright green
        {0.96, 0.86, 0.54, 1.0},   // 11 bright yellow
        {0.55, 0.72, 0.90, 1.0},   // 12 bright blue
        {0.82, 0.62, 0.88, 1.0},   // 13 bright magenta
        {0.58, 0.86, 0.90, 1.0},   // 14 bright cyan
        {0.93, 0.95, 0.98, 1.0},   // 15 bright white
    }};

    vte_terminal_set_colors(vte, &fg, &bg, palette.data(), palette.size());

    GdkRGBA cursor_color = {0.50, 0.84, 0.73, 1.0};
    vte_terminal_set_color_cursor(vte, &cursor_color);
    vte_terminal_set_color_cursor_foreground(vte, &bg);
}

void configure_vte(VteTerminal* vte, const TerminalConfig& tc) {
    auto* font_desc = pango_font_description_new();
    pango_font_description_set_family(font_desc, tc.font_family.c_str());
    pango_font_description_set_size(font_desc, tc.font_size * PANGO_SCALE);
    vte_terminal_set_font(vte, font_desc);
    pango_font_description_free(font_desc);

    vte_terminal_set_scrollback_lines(vte, tc.scrollback_lines);
    vte_terminal_set_cursor_blink_mode(vte,
        tc.cursor_blink ? VTE_CURSOR_BLINK_ON : VTE_CURSOR_BLINK_OFF);
    vte_terminal_set_cursor_shape(vte, parse_cursor_shape(tc.cursor_shape));
    vte_terminal_set_audible_bell(vte, tc.audible_bell);
    vte_terminal_set_allow_hyperlink(vte, tc.allow_hyperlinks);
    vte_terminal_set_scroll_on_output(vte, FALSE);
    vte_terminal_set_scroll_on_keystroke(vte, TRUE);

    apply_terminal_colors(vte, tc.opacity);
}

void spawn_shell_in_vte(VteTerminal* vte, const std::string& cwd) {
    const auto shell = detect_shell();
    auto* argv = g_new0(char*, 2);
    argv[0] = g_strdup(shell.c_str());
    argv[1] = nullptr;

    vte_terminal_spawn_async(
        vte,
        VTE_PTY_DEFAULT,
        cwd.c_str(),
        argv,
        nullptr,
        G_SPAWN_DEFAULT,
        nullptr,
        nullptr,
        nullptr,
        -1,
        nullptr,
        [](VteTerminal*, GPid, GError* error, gpointer) {
            if (error) {
                g_warning("hyalo-terminal: shell spawn failed: %s", error->message);
            }
        },
        nullptr
    );
}

class TerminalWindow final : public Gtk::ApplicationWindow {
public:
    explicit TerminalWindow(const TerminalConfig& config)
        : config_(config),
          font_scale_(1.0) {
        set_title("Hyalo Terminal");
        set_default_size(920, 620);
        add_css_class("hyalo-terminal-window");

        // Use compositor server-side decorations — no custom headerbar
        auto* new_tab_btn = Gtk::make_managed<Gtk::Button>("\u002B");
        new_tab_btn->set_tooltip_text("Nowa karta  (Ctrl+Shift+T)");
        new_tab_btn->add_css_class("flat");
        new_tab_btn->add_css_class("terminal-new-tab");
        new_tab_btn->signal_clicked().connect(sigc::mem_fun(*this, &TerminalWindow::add_tab));

        notebook_.set_scrollable(true);
        notebook_.set_show_border(false);
        notebook_.set_show_tabs(true);
        notebook_.set_action_widget(new_tab_btn, Gtk::PackType::END);
        notebook_.signal_page_removed().connect([this](Gtk::Widget*, guint) {
            update_title();
        });
        notebook_.signal_switch_page().connect([this](Gtk::Widget*, guint) {
            update_title();
        });

        auto* root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
        root->append(notebook_);
        set_child(*root);

        add_tab();

        setup_keyboard_shortcuts();
        setup_scroll_zoom();
    }

private:
    void add_tab() {
        auto* vte_widget = vte_terminal_new();
        auto* vte = VTE_TERMINAL(vte_widget);

        configure_vte(vte, config_);
        vte_terminal_set_font_scale(vte, font_scale_);

        const auto cwd = detect_cwd_from_active_tab();
        spawn_shell_in_vte(vte, cwd);

        auto* managed = Gtk::manage(Glib::wrap(vte_widget));
        managed->set_hexpand(true);
        managed->set_vexpand(true);

        auto tab_index = tab_counter_++;
        auto* label_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
        auto* label = Gtk::make_managed<Gtk::Label>("Terminal " + std::to_string(tab_index));
        label->set_ellipsize(Pango::EllipsizeMode::END);
        label->set_max_width_chars(20);

        auto* close_btn = Gtk::make_managed<Gtk::Button>("\u00D7");
        close_btn->add_css_class("flat");
        close_btn->add_css_class("terminal-tab-close");
        close_btn->set_margin_start(4);

        label_box->append(*label);
        label_box->append(*close_btn);

        auto page_index = notebook_.append_page(*managed, *label_box);
        notebook_.set_tab_reorderable(*managed, true);
        notebook_.set_current_page(page_index);
        managed->grab_focus();

        g_signal_connect(vte_widget, "window-title-changed",
            G_CALLBACK(+[](VteTerminal* terminal, gpointer user_data) {
                auto* lbl = static_cast<Gtk::Label*>(user_data);
                G_GNUC_BEGIN_IGNORE_DEPRECATIONS
                const auto* title = vte_terminal_get_window_title(terminal);
                G_GNUC_END_IGNORE_DEPRECATIONS
                if (title && title[0] != '\0') {
                    lbl->set_text(title);
                }
            }),
            label
        );

        g_signal_connect(vte_widget, "child-exited",
            G_CALLBACK(+[](VteTerminal* vte_term, int, gpointer user_data) {
                auto* self = static_cast<TerminalWindow*>(user_data);
                auto* widget = Glib::wrap(GTK_WIDGET(vte_term));
                self->close_tab_for_widget(widget);
            }),
            this
        );

        close_btn->signal_clicked().connect([this, managed]() {
            close_tab_for_widget(managed);
        });
    }

    void close_tab_for_widget(Gtk::Widget* widget) {
        auto page = notebook_.page_num(*widget);
        if (page >= 0) {
            notebook_.remove_page(page);
        }
        if (notebook_.get_n_pages() == 0) {
            close();
        }
    }

    void close_current_tab() {
        auto page = notebook_.get_current_page();
        if (page >= 0) {
            notebook_.remove_page(page);
        }
        if (notebook_.get_n_pages() == 0) {
            close();
        }
    }

    void update_title() {
        auto* vte = current_vte();
        if (!vte) {
            set_title("Hyalo Terminal");
            return;
        }
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        const auto* title = vte_terminal_get_window_title(vte);
        G_GNUC_END_IGNORE_DEPRECATIONS
        if (title && title[0] != '\0') {
            set_title(title);
        } else {
            set_title("Hyalo Terminal");
        }
    }

    std::string detect_cwd_from_active_tab() {
        auto* vte = current_vte();
        if (vte) {
            G_GNUC_BEGIN_IGNORE_DEPRECATIONS
            auto* uri = vte_terminal_get_current_directory_uri(vte);
            G_GNUC_END_IGNORE_DEPRECATIONS
            if (uri) {
                auto str = std::string(uri);
                const std::string prefix = "file://";
                if (str.rfind(prefix, 0) == 0) {
                    return str.substr(prefix.size());
                }
                return str;
            }
        }
        return detect_working_directory();
    }

    void change_font_scale(double delta) {
        font_scale_ = std::clamp(font_scale_ + delta, 0.4, 4.0);

        for (int i = 0; i < notebook_.get_n_pages(); ++i) {
            auto* child = notebook_.get_nth_page(i);
            if (child) {
                auto* vte = VTE_TERMINAL(child->gobj());
                vte_terminal_set_font_scale(vte, font_scale_);
            }
        }
    }

    void reset_font_scale() {
        font_scale_ = 1.0;
        for (int i = 0; i < notebook_.get_n_pages(); ++i) {
            auto* child = notebook_.get_nth_page(i);
            if (child) {
                auto* vte = VTE_TERMINAL(child->gobj());
                vte_terminal_set_font_scale(vte, font_scale_);
            }
        }
    }

    void next_tab() {
        auto current = notebook_.get_current_page();
        auto total = notebook_.get_n_pages();
        if (total > 1) {
            notebook_.set_current_page((current + 1) % total);
        }
    }

    void prev_tab() {
        auto current = notebook_.get_current_page();
        auto total = notebook_.get_n_pages();
        if (total > 1) {
            notebook_.set_current_page((current - 1 + total) % total);
        }
    }

    void toggle_fullscreen() {
        if (is_fullscreen()) {
            unfullscreen();
        } else {
            fullscreen();
        }
    }

    void setup_keyboard_shortcuts() {
        auto controller = Gtk::EventControllerKey::create();
        controller->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
        controller->signal_key_pressed().connect(
            [this](guint keyval, guint, Gdk::ModifierType state) -> bool {
                const auto ctrl = Gdk::ModifierType::CONTROL_MASK;
                const auto shift = Gdk::ModifierType::SHIFT_MASK;
                const auto ctrl_shift = ctrl | shift;
                const auto mods = state & (ctrl | shift);

                // Ctrl+Shift shortcuts
                if (mods == ctrl_shift) {
                    switch (keyval) {
                    case GDK_KEY_T: case GDK_KEY_t:
                        add_tab();
                        return true;
                    case GDK_KEY_W: case GDK_KEY_w:
                        close_current_tab();
                        return true;
                    case GDK_KEY_C: case GDK_KEY_c:
                        copy_from_current_vte();
                        return true;
                    case GDK_KEY_V: case GDK_KEY_v:
                        paste_to_current_vte();
                        return true;
                    case GDK_KEY_Page_Up:
                        prev_tab();
                        return true;
                    case GDK_KEY_Page_Down:
                        next_tab();
                        return true;
                    }
                }

                // Ctrl-only shortcuts
                if (mods == ctrl) {
                    switch (keyval) {
                    case GDK_KEY_plus: case GDK_KEY_equal:
                        change_font_scale(0.1);
                        return true;
                    case GDK_KEY_minus:
                        change_font_scale(-0.1);
                        return true;
                    case GDK_KEY_0:
                        reset_font_scale();
                        return true;
                    }
                }

                // F11 fullscreen
                if (keyval == GDK_KEY_F11) {
                    toggle_fullscreen();
                    return true;
                }

                // Ctrl+Tab / Ctrl+Shift+Tab
                if ((state & ctrl) != Gdk::ModifierType{} && keyval == GDK_KEY_Tab) {
                    if ((state & shift) != Gdk::ModifierType{}) {
                        prev_tab();
                    } else {
                        next_tab();
                    }
                    return true;
                }

                // Ctrl+1..9 — switch to tab by number
                if (mods == ctrl && keyval >= GDK_KEY_1 && keyval <= GDK_KEY_9) {
                    auto target = static_cast<int>(keyval - GDK_KEY_1);
                    if (target < notebook_.get_n_pages()) {
                        notebook_.set_current_page(target);
                    }
                    return true;
                }

                return false;
            },
            true
        );
        add_controller(controller);
    }

    void setup_scroll_zoom() {
        auto scroll_controller = Gtk::EventControllerScroll::create();
        scroll_controller->set_flags(
            Gtk::EventControllerScroll::Flags::VERTICAL |
            Gtk::EventControllerScroll::Flags::DISCRETE
        );
        scroll_controller->signal_scroll().connect(
            [this](double, double dy) -> bool {
                auto* seat = gdk_display_get_default_seat(gdk_display_get_default());
                auto* device = seat ? gdk_seat_get_keyboard(seat) : nullptr;
                auto mods = device
                    ? gdk_device_get_modifier_state(device)
                    : static_cast<GdkModifierType>(0);

                if (mods & GDK_CONTROL_MASK) {
                    if (dy < 0) {
                        change_font_scale(0.1);
                    } else if (dy > 0) {
                        change_font_scale(-0.1);
                    }
                    return true;
                }
                return false;
            },
            false
        );
        add_controller(scroll_controller);
    }

    VteTerminal* current_vte() const {
        auto page = notebook_.get_current_page();
        if (page < 0) {
            return nullptr;
        }
        auto* child = notebook_.get_nth_page(page);
        if (!child) {
            return nullptr;
        }
        return VTE_TERMINAL(child->gobj());
    }

    void copy_from_current_vte() {
        auto* vte = current_vte();
        if (vte) {
            vte_terminal_copy_clipboard_format(vte, VTE_FORMAT_TEXT);
        }
    }

    void paste_to_current_vte() {
        auto* vte = current_vte();
        if (vte) {
            vte_terminal_paste_clipboard(vte);
        }
    }

    TerminalConfig config_;
    double font_scale_;
    Gtk::Notebook notebook_;
    int tab_counter_ = 1;
};

}  // namespace

int main(int argc, char** argv) {
    configure_stable_gsk_renderer();

    auto app = Gtk::Application::create("org.hyalo.Terminal");

    auto config_manager = hyalo::core::ConfigManager(hyalo::core::detect_runtime_paths());
    if (!config_manager.load()) {
        config_manager.load_defaults();
    }
    hyalo::core::StyleManager::apply(config_manager);

    const auto terminal_config = load_terminal_config(config_manager);

    app->signal_activate().connect([&app, &terminal_config]() {
        auto* window = new TerminalWindow(terminal_config);
        app->add_window(*window);
        window->present();
    });

    return app->run(argc, argv);
}
