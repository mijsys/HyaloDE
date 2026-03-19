/*
 * Hyalo Files — File manager for HyaloOS
 *
 * GTK4/gtkmm4 file browser with sidebar, grid view, context menu,
 * directory monitoring, and standard file operations.
 */

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <gdkmm/display.h>
#include <giomm.h>
#include <glibmm.h>

#include <gtkmm/application.h>
#include <gtkmm/applicationwindow.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/entry.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/flowboxchild.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/image.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/listboxrow.h>
#include <gtkmm/paned.h>
#include <gtkmm/popovermenu.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>
#include <gtkmm/settings.h>
#include <gtkmm/spinner.h>
#include <gtkmm/stack.h>
#include <gtkmm/stylecontext.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/window.h>

#include "hyalo-core/config_manager.hpp"
#include "hyalo-core/style_manager.hpp"

namespace {

// ---------- renderer ----------

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

// ---------- data types ----------

struct FileEntry {
    std::string name;
    std::string display_name;
    bool is_directory = false;
    bool is_hidden    = false;
    goffset size      = 0;
    Glib::RefPtr<Gio::Icon> icon;
};

struct PlaceInfo {
    std::string label;
    std::string icon_name;
    std::string path;
};

// ---------- helpers ----------

std::string format_size(goffset bytes) {
    if (bytes < 1024)                return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024)         return std::to_string(bytes / 1024) + " KB";
    if (bytes < 1024LL * 1024 * 1024) return std::to_string(bytes / (1024 * 1024)) + " MB";
    return std::to_string(bytes / (1024LL * 1024 * 1024)) + " GB";
}

std::vector<PlaceInfo> build_places() {
    std::vector<PlaceInfo> out;

    const auto* home = g_get_home_dir();
    if (home) out.push_back({"Katalog domowy", "user-home", home});

    auto add = [&](GUserDirectory dir, const char* label, const char* icon) {
        const auto* p = g_get_user_special_dir(dir);
        if (p) out.push_back({label, icon, p});
    };

    add(G_USER_DIRECTORY_DESKTOP,   "Pulpit",    "user-desktop");
    add(G_USER_DIRECTORY_DOCUMENTS, "Dokumenty", "folder-documents");
    add(G_USER_DIRECTORY_DOWNLOAD,  "Pobrane",   "folder-download");
    add(G_USER_DIRECTORY_MUSIC,     "Muzyka",    "folder-music");
    add(G_USER_DIRECTORY_PICTURES,  "Obrazy",    "folder-pictures");
    add(G_USER_DIRECTORY_VIDEOS,    "Wideo",     "folder-videos");

    // Trash (GIO virtual location)
    auto trash_path = std::string("trash:///");
    out.push_back({"Kosz", "user-trash", trash_path});

    out.push_back({"System plik\xc3\xb3w", "drive-harddisk", "/"});
    return out;
}

// ---------- helpers: threading ----------

void post_to_main(std::function<void()> fn) {
    auto* p = new std::function<void()>(std::move(fn));
    g_idle_add([](gpointer data) -> gboolean {
        auto* f = static_cast<std::function<void()>*>(data);
        (*f)();
        delete f;
        return G_SOURCE_REMOVE;
    }, p);
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return {};
    }

    std::ostringstream ss;
    ss << input.rdbuf();
    return ss.str();
}

void apply_hyalo_fallback_style(const hyalo::core::RuntimePaths& paths) {
    const auto display = Gdk::Display::get_default();
    if (!display) {
        return;
    }

    const auto base_css = read_text_file(paths.assets_root / "base.css");
    const auto theme_css = read_text_file(paths.assets_root / "hyalo.css");
    const auto css = base_css + "\n" + theme_css;

    if (!css.empty()) {
        auto provider = Gtk::CssProvider::create();
        provider->load_from_string(css);
        Gtk::StyleContext::add_provider_for_display(
            display,
            provider,
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
        );
    }

    if (auto icon_theme = Gtk::IconTheme::get_for_display(display)) {
        const auto icons_root = paths.assets_root.parent_path() / "icons";
        if (std::filesystem::exists(icons_root / "hyalo-icons")) {
            icon_theme->add_search_path(icons_root.string());
        }
    }

    if (auto settings = Gtk::Settings::get_default()) {
        g_object_set(settings->gobj(), "gtk-icon-theme-name", "hyalo-icons", nullptr);
    }
}

// ---------- FileManagerWindow ----------

class FileManagerWindow final : public Gtk::ApplicationWindow {
public:
    FileManagerWindow() {
        set_title("Hyalo Pliki");
        set_default_size(1050, 680);
        add_css_class("hyalo-files-window");

        setup_actions();
        setup_toolbar();
        setup_sidebar();
        setup_content();
        setup_layout();
        setup_keyboard_shortcuts();
        setup_mouse_navigation();
        setup_context_menu();

        const auto* home = g_get_home_dir();
        navigate_to(std::string(home ? home : "/"));
    }

    ~FileManagerWindow() override {
        alive_->store(false);
        if (refresh_timeout_connection_.connected()) {
            refresh_timeout_connection_.disconnect();
        }
        if (monitor_changed_connection_.connected()) {
            monitor_changed_connection_.disconnect();
        }
        if (ctx_popover_ && ctx_popover_->get_parent()) {
            ctx_popover_->unparent();
        }
        monitor_.reset();
    }

private:
    // ===================== setup =====================

    void setup_actions() {
        file_actions_ = Gio::SimpleActionGroup::create();
        file_actions_->add_action("open",       sigc::mem_fun(*this, &FileManagerWindow::open_selected));
        file_actions_->add_action("rename",     sigc::mem_fun(*this, &FileManagerWindow::rename_selected));
        file_actions_->add_action("trash",      sigc::mem_fun(*this, &FileManagerWindow::trash_selected));
        file_actions_->add_action("new-folder", sigc::mem_fun(*this, &FileManagerWindow::create_new_folder));
        file_actions_->add_action("refresh",    sigc::mem_fun(*this, &FileManagerWindow::refresh));
        file_actions_->add_action("copy",       sigc::mem_fun(*this, &FileManagerWindow::copy_selected));
        file_actions_->add_action("cut",        sigc::mem_fun(*this, &FileManagerWindow::cut_selected));
        file_actions_->add_action("paste",      sigc::mem_fun(*this, &FileManagerWindow::paste_clipboard));
        insert_action_group("files", file_actions_);
    }

    void setup_toolbar() {
        toolbar_.add_css_class("files-toolbar");
        toolbar_.set_margin_start(8);
        toolbar_.set_margin_end(8);
        toolbar_.set_margin_top(6);
        toolbar_.set_margin_bottom(2);

        auto make_nav = [&](Gtk::Button& btn, const char* icon_name, const char* tip) {
            auto* img = Gtk::make_managed<Gtk::Image>();
            img->set_from_icon_name(icon_name);
            img->set_pixel_size(16);
            btn.set_child(*img);
            btn.set_tooltip_text(tip);
            btn.add_css_class("flat");
            btn.add_css_class("files-nav-btn");
            toolbar_.append(btn);
        };

        make_nav(back_btn_, "go-previous-symbolic", "Wstecz");
        make_nav(fwd_btn_,  "go-next-symbolic",     "Dalej");
        make_nav(up_btn_,   "go-up-symbolic",        "Folder nadrz\xc4" "\x99" "dny");

        back_btn_.signal_clicked().connect(sigc::mem_fun(*this, &FileManagerWindow::go_back));
        fwd_btn_.signal_clicked().connect(sigc::mem_fun(*this, &FileManagerWindow::go_forward));
        up_btn_.signal_clicked().connect(sigc::mem_fun(*this, &FileManagerWindow::go_up));

        // Path label / location entry stack
        path_label_.set_hexpand(true);
        path_label_.set_halign(Gtk::Align::START);
        path_label_.set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        path_label_.add_css_class("files-path-label");

        location_entry_.set_hexpand(true);
        location_entry_.add_css_class("files-location-entry");
        location_entry_.signal_activate().connect([this]() {
            auto text = std::string(location_entry_.get_text());
            if (!text.empty()) {
                if (text[0] == '~') {
                    const auto* home = g_get_home_dir();
                    if (home) text = std::string(home) + text.substr(1);
                }
                std::error_code ec;
                std::filesystem::path p(text);
                if (std::filesystem::is_directory(p, ec)) navigate_to(p.string());
            }
            path_stack_.set_visible_child("label");
        });

        path_stack_.add(path_label_, "label");
        path_stack_.add(location_entry_, "entry");
        path_stack_.set_visible_child("label");
        path_stack_.set_hexpand(true);
        path_stack_.set_margin_start(8);
        path_stack_.set_margin_end(8);
        toolbar_.append(path_stack_);

        // Refresh
        {
            auto* img = Gtk::make_managed<Gtk::Image>();
            img->set_from_icon_name("view-refresh-symbolic");
            img->set_pixel_size(16);
            refresh_btn_.set_child(*img);
        }
        refresh_btn_.set_tooltip_text("Od\xc5\x9bwie\xc5\xbc  (F5 / Ctrl+R)");
        refresh_btn_.add_css_class("flat");
        refresh_btn_.add_css_class("files-nav-btn");
        refresh_btn_.signal_clicked().connect(sigc::mem_fun(*this, &FileManagerWindow::refresh));
        toolbar_.append(refresh_btn_);

        // Hidden toggle
        {
            auto* img = Gtk::make_managed<Gtk::Image>();
            img->set_from_icon_name("view-reveal-symbolic");
            img->set_pixel_size(16);
            hidden_btn_.set_child(*img);
        }
        hidden_btn_.set_tooltip_text("Poka\xc5\xbc ukryte pliki  (Ctrl+H)");
        hidden_btn_.add_css_class("flat");
        hidden_btn_.add_css_class("files-nav-btn");
        hidden_btn_.signal_toggled().connect([this]() {
            if (toggling_hidden_) return;
            show_hidden_ = hidden_btn_.get_active();
            refilter_files();
        });
        toolbar_.append(hidden_btn_);

        // New folder
        {
            auto* img = Gtk::make_managed<Gtk::Image>();
            img->set_from_icon_name("folder-new-symbolic");
            img->set_pixel_size(16);
            new_folder_btn_.set_child(*img);
        }
        new_folder_btn_.set_tooltip_text("Nowy folder  (Ctrl+N)");
        new_folder_btn_.add_css_class("flat");
        new_folder_btn_.add_css_class("files-nav-btn");
        new_folder_btn_.signal_clicked().connect(sigc::mem_fun(*this, &FileManagerWindow::create_new_folder));
        toolbar_.append(new_folder_btn_);
    }

    void setup_sidebar() {
        places_list_.add_css_class("files-places");
        places_list_.set_selection_mode(Gtk::SelectionMode::SINGLE);

        for (const auto& place : build_places()) {
            auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
            auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
            box->set_margin_top(6);
            box->set_margin_bottom(6);
            box->set_margin_start(12);
            box->set_margin_end(12);

            auto* icon = Gtk::make_managed<Gtk::Image>();
            icon->set_from_icon_name(place.icon_name);
            icon->set_pixel_size(18);

            auto* label = Gtk::make_managed<Gtk::Label>(place.label);
            label->set_halign(Gtk::Align::START);

            box->append(*icon);
            box->append(*label);
            row->set_child(*box);
            row->set_name(place.path);
            places_list_.append(*row);
        }

        places_list_.signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
            if (row) navigate_to(std::string(row->get_name()));
        });

        sidebar_scroll_.set_child(places_list_);
        sidebar_scroll_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        sidebar_scroll_.set_size_request(180, -1);
        sidebar_scroll_.add_css_class("files-sidebar");
    }

    void setup_content() {
        file_grid_.set_homogeneous(true);
        file_grid_.set_min_children_per_line(3);
        file_grid_.set_max_children_per_line(20);
        file_grid_.set_selection_mode(Gtk::SelectionMode::MULTIPLE);
        file_grid_.set_activate_on_single_click(false);
        file_grid_.add_css_class("files-grid");
        file_grid_.set_row_spacing(4);
        file_grid_.set_column_spacing(4);

        file_grid_.signal_child_activated().connect(
            sigc::mem_fun(*this, &FileManagerWindow::on_file_activated));

        // Loading spinner (centered in content area)
        loading_spinner_.set_spinning(false);
        loading_spinner_.set_halign(Gtk::Align::CENTER);
        loading_spinner_.set_valign(Gtk::Align::CENTER);
        loading_spinner_.set_size_request(32, 32);
        loading_spinner_.add_css_class("files-spinner");

        content_stack_.add(content_scroll_, "grid");
        content_stack_.add(loading_spinner_, "loading");
        content_stack_.set_visible_child("grid");

        content_scroll_.set_child(file_grid_);
        content_scroll_.set_hexpand(true);
        content_scroll_.set_vexpand(true);
        content_scroll_.add_css_class("files-content");

        content_stack_.set_hexpand(true);
        content_stack_.set_vexpand(true);
    }

    void setup_layout() {
        paned_.set_start_child(sidebar_scroll_);
        paned_.set_end_child(content_stack_);
        paned_.set_position(200);
        paned_.set_shrink_start_child(false);

        status_label_.set_halign(Gtk::Align::START);
        status_label_.set_margin_start(12);
        status_label_.set_margin_top(4);
        status_label_.set_margin_bottom(4);
        status_label_.add_css_class("files-status");

        root_.append(toolbar_);
        root_.append(paned_);
        root_.append(status_label_);
        set_child(root_);
    }

    void setup_keyboard_shortcuts() {
        auto controller = Gtk::EventControllerKey::create();
        controller->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
        controller->signal_key_pressed().connect(
            [this](guint keyval, guint, Gdk::ModifierType state) -> bool {
                const bool ctrl = (state & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType{};
                const bool alt  = (state & Gdk::ModifierType::ALT_MASK)     != Gdk::ModifierType{};

                if (ctrl && keyval == GDK_KEY_c)    { copy_selected();        return true; }
                if (ctrl && keyval == GDK_KEY_x)    { cut_selected();         return true; }
                if (ctrl && keyval == GDK_KEY_v)    { paste_clipboard();      return true; }
                if (ctrl && keyval == GDK_KEY_a)    { select_all();           return true; }
                if (ctrl && keyval == GDK_KEY_h)    { toggle_hidden();        return true; }
                if (ctrl && keyval == GDK_KEY_l)    { show_location_entry();  return true; }
                if (ctrl && keyval == GDK_KEY_n)    { create_new_folder();    return true; }
                if (ctrl && keyval == GDK_KEY_r)    { refresh();              return true; }
                if (alt  && keyval == GDK_KEY_Left) { go_back();              return true; }
                if (alt  && keyval == GDK_KEY_Right){ go_forward();           return true; }
                if (alt  && keyval == GDK_KEY_Up)   { go_up();                return true; }
                if (keyval == GDK_KEY_BackSpace)    { go_up();                return true; }
                if (keyval == GDK_KEY_F2)           { rename_selected();      return true; }
                if (keyval == GDK_KEY_Delete)       { trash_selected();       return true; }
                if (keyval == GDK_KEY_F5)           { refresh();              return true; }
                if (keyval == GDK_KEY_Return)       { open_selected();        return true; }
                if (keyval == GDK_KEY_Escape) {
                    path_stack_.set_visible_child("label");
                    return true;
                }
                return false;
            },
            false
        );
        add_controller(controller);
    }

    void setup_mouse_navigation() {
        auto gesture = Gtk::GestureClick::create();
        gesture->set_button(0);
        gesture->signal_pressed().connect([this](int, double, double) {
            const auto button = gtk_gesture_single_get_current_button(
                GTK_GESTURE_SINGLE(mouse_nav_gesture_->gobj()));
            if (button == 8) go_back();
            else if (button == 9) go_forward();
        });
        mouse_nav_gesture_ = gesture;
        add_controller(gesture);
    }

    void setup_context_menu() {
        auto gesture = Gtk::GestureClick::create();
        gesture->set_button(GDK_BUTTON_SECONDARY);
        gesture->signal_pressed().connect([this](int, double x, double y) {
            show_context_popup(x, y);
        });
        file_grid_.add_controller(gesture);
    }

    // ===================== navigation =====================

    bool is_virtual() const { return !current_uri_.empty(); }

    std::string current_location_string() const {
        return current_uri_.empty() ? current_path_.string() : current_uri_;
    }

    void set_location(const std::string& loc) {
        if (loc.find("://") != std::string::npos) {
            current_uri_ = loc;
            current_path_.clear();
        } else {
            current_uri_.clear();
            current_path_ = loc;
        }
    }

    void navigate_to(const std::string& location) {
        if (location.find("://") != std::string::npos) {
            // Virtual GIO location (e.g. trash:///)
            auto old = current_location_string();
            if (!old.empty() && location != old) {
                back_stack_.push_back(old);
                fwd_stack_.clear();
            }
            set_location(location);
        } else {
            std::error_code ec;
            auto canonical = std::filesystem::canonical(location, ec);
            if (ec || !std::filesystem::is_directory(canonical, ec)) return;

            auto old = current_location_string();
            auto new_str = canonical.string();
            if (!old.empty() && new_str != old) {
                back_stack_.push_back(old);
                fwd_stack_.clear();
            }
            current_uri_.clear();
            current_path_ = canonical;
        }

        update_nav_buttons();
        update_path_display();
        start_monitoring();
        populate_files();
    }

    void go_back() {
        if (back_stack_.empty()) return;
        fwd_stack_.push_back(current_location_string());
        auto loc = back_stack_.back();
        back_stack_.pop_back();
        set_location(loc);
        update_nav_buttons();
        update_path_display();
        start_monitoring();
        populate_files();
    }

    void go_forward() {
        if (fwd_stack_.empty()) return;
        back_stack_.push_back(current_location_string());
        auto loc = fwd_stack_.back();
        fwd_stack_.pop_back();
        set_location(loc);
        update_nav_buttons();
        update_path_display();
        start_monitoring();
        populate_files();
    }

    void go_up() {
        if (is_virtual()) return;
        auto parent = current_path_.parent_path();
        if (parent != current_path_) navigate_to(parent.string());
    }

    // ===================== file listing =====================

    void populate_files() {
        const auto gen = populate_generation_.fetch_add(1, std::memory_order_relaxed) + 1;
        render_index_ = 0;
        visible_entries_.clear();

        // Show loading state
        content_stack_.set_visible_child("loading");
        loading_spinner_.set_spinning(true);

        // Clear grid off-screen (no layout cost since it's hidden behind the stack)
        while (auto* child = file_grid_.get_first_child()) {
            file_grid_.remove(*child);
        }
        entries_.clear();

        auto location = current_location_string();
        bool use_uri = is_virtual();
        auto entries_out = std::make_shared<std::vector<FileEntry>>();
        auto error_out = std::make_shared<std::string>();
        auto alive = alive_;

        std::thread([location, use_uri, entries_out, error_out, gen, alive, this]() {
            try {
                auto gfile = use_uri ? Gio::File::create_for_uri(location)
                                     : Gio::File::create_for_path(location);
                auto enumerator = gfile->enumerate_children(
                    "standard::*", Gio::FileQueryInfoFlags::NOFOLLOW_SYMLINKS);

                while (auto info = enumerator->next_file()) {
                    if (!alive->load()) return; // bail early if window destroyed
                    FileEntry entry;
                    entry.name         = info->get_name();
                    entry.display_name = info->get_display_name();
                    entry.is_directory = (info->get_file_type() == Gio::FileType::DIRECTORY);
                    entry.is_hidden    = info->is_hidden();
                    entry.size         = info->get_size();
                    entry.icon         = info->get_icon();
                    entries_out->push_back(std::move(entry));
                }
            } catch (const Glib::Error& e) {
                *error_out = e.what();
            }

            post_to_main([this, gen, entries_out, error_out, alive]() {
                if (!alive->load() || gen != populate_generation_.load(std::memory_order_relaxed)) return;
                on_enumerate_done(gen, std::move(*entries_out), *error_out);
            });
        }).detach();
    }

    void on_enumerate_done(unsigned int gen, std::vector<FileEntry> new_entries,
                           const std::string& error) {
        loading_spinner_.set_spinning(false);

        if (gen != populate_generation_.load(std::memory_order_relaxed)) return;

        if (!error.empty()) {
            content_stack_.set_visible_child("grid");
            status_label_.set_text(std::string("B\xc5\x82\xc4\x85\x64: ") + error);
            return;
        }

        entries_ = std::move(new_entries);

        // Sort: directories first, then alphabetical (case-insensitive)
        std::sort(entries_.begin(), entries_.end(),
            [](const FileEntry& a, const FileEntry& b) {
                if (a.is_directory != b.is_directory)
                    return a.is_directory > b.is_directory;
                auto la = a.display_name, lb = b.display_name;
                std::transform(la.begin(), la.end(), la.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                std::transform(lb.begin(), lb.end(), lb.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return la < lb;
            });

        // Filter visible entries
        for (const auto& entry : entries_) {
            if (!show_hidden_ && entry.is_hidden) continue;
            visible_entries_.push_back(&entry);
        }

        update_status(static_cast<int>(visible_entries_.size()));
        render_batch(gen);
    }

    void refilter_files() {
        const auto gen = populate_generation_.fetch_add(1, std::memory_order_relaxed) + 1;
        render_index_ = 0;
        visible_entries_.clear();

        while (auto* child = file_grid_.get_first_child()) {
            file_grid_.remove(*child);
        }

        for (const auto& entry : entries_) {
            if (!show_hidden_ && entry.is_hidden) continue;
            visible_entries_.push_back(&entry);
        }

        update_status(static_cast<int>(visible_entries_.size()));
        render_batch(gen);
    }

    void render_batch(unsigned int generation) {
        if (generation != populate_generation_.load(std::memory_order_relaxed)) return;

        constexpr std::size_t kBatchSize = 80;
        const auto end = std::min(render_index_ + kBatchSize, visible_entries_.size());

        for (; render_index_ < end; ++render_index_) {
            const auto& entry = *visible_entries_[render_index_];
            append_file_item(entry);
        }

        if (render_index_ < visible_entries_.size()) {
            Glib::signal_idle().connect_once(
                [this, generation]() { render_batch(generation); });
        }

        // Switch to grid view after first batch rendered
        if (content_stack_.get_visible_child_name() != "grid") {
            content_stack_.set_visible_child("grid");
        }
    }

    void append_file_item(const FileEntry& entry) {
        auto* child = Gtk::make_managed<Gtk::FlowBoxChild>();
        child->set_name(entry.name);
        child->add_css_class("files-item");

        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        box->set_halign(Gtk::Align::CENTER);
        box->set_valign(Gtk::Align::START);
        box->set_margin_top(6);
        box->set_margin_bottom(2);
        box->set_margin_start(2);
        box->set_margin_end(2);

        auto* image = Gtk::make_managed<Gtk::Image>();
        image->set_pixel_size(36);

        if (entry.icon) {
            gtk_image_set_from_gicon(image->gobj(), entry.icon->gobj());
        } else if (entry.is_directory) {
            image->set_from_icon_name("folder");
        } else {
            image->set_from_icon_name("text-x-generic");
        }

        auto* label = Gtk::make_managed<Gtk::Label>(entry.display_name);
        label->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        label->set_max_width_chars(12);
        label->set_lines(2);
        label->set_wrap(true);
        label->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
        label->add_css_class("files-item-label");

        box->append(*image);
        box->append(*label);
        child->set_child(*box);
        file_grid_.append(*child);
    }

    void refresh() {
        populate_files();
    }

    // ===================== file operations =====================

    void on_file_activated(Gtk::FlowBoxChild* child) {
        if (!child || is_virtual()) return;
        auto path = current_path_ / std::string(child->get_name());

        std::error_code ec;
        if (std::filesystem::is_directory(path, ec)) {
            navigate_to(path.string());
        } else {
            try {
                auto gfile = Gio::File::create_for_path(path.string());
                auto launcher = gtk_uri_launcher_new(gfile->get_uri().c_str());
                gtk_uri_launcher_launch(launcher, GTK_WINDOW(gobj()), nullptr, nullptr, nullptr);
                g_object_unref(launcher);
            } catch (...) {}
        }
    }

    void open_selected() {
        auto selected = file_grid_.get_selected_children();
        if (!selected.empty()) on_file_activated(selected[0]);
    }

    void select_all() {
        file_grid_.select_all();
    }

    // ---------- dialog helpers (no nested main loop!) ----------

    void create_new_folder() {
        if (is_virtual()) return;
        auto* win = new Gtk::Window();
        win->set_title("Nowy folder");
        win->set_transient_for(*this);
        win->set_modal(true);
        win->set_decorated(false);
        win->set_resizable(false);
        win->set_hide_on_close(true);
        win->set_default_size(460, -1);
        win->add_css_class("hyalo-modal-window");

        auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
        content->add_css_class("hyalo-modal-surface");
        win->set_child(*content);

        auto* title_lbl = Gtk::make_managed<Gtk::Label>("Nazwa nowego folderu:");
        title_lbl->set_halign(Gtk::Align::START);
        title_lbl->add_css_class("hyalo-modal-title");

        auto* entry = Gtk::make_managed<Gtk::Entry>();
        entry->set_text("Nowy folder");
        entry->select_region(0, -1);
        entry->set_hexpand(true);
        entry->add_css_class("hyalo-modal-entry");

        content->append(*title_lbl);
        content->append(*entry);

        auto* btn_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        btn_box->set_homogeneous(true);
        btn_box->set_margin_top(8);

        auto* cancel_btn = Gtk::make_managed<Gtk::Button>("Anuluj");
        cancel_btn->add_css_class("hyalo-button");
        cancel_btn->add_css_class("hyalo-button-secondary");

        auto* create_btn = Gtk::make_managed<Gtk::Button>("Utw\xc3\xb3rz");
        create_btn->add_css_class("hyalo-button");
        create_btn->add_css_class("hyalo-button-primary");

        btn_box->append(*cancel_btn);
        btn_box->append(*create_btn);
        content->append(*btn_box);

        auto do_create = [this, win, entry]() {
            auto name = std::string(entry->get_text());
            if (!name.empty()) {
                std::error_code ec;
                std::filesystem::create_directory(current_path_ / name, ec);
            }
            win->close();
            refresh();
        };

        create_btn->signal_clicked().connect(do_create);
        cancel_btn->signal_clicked().connect([win]() { win->close(); });
        entry->signal_activate().connect(do_create);

        win->signal_hide().connect([win]() { delete win; });
        win->present();
        entry->grab_focus();
    }

    void rename_selected() {
        if (is_virtual()) return;
        auto selected = file_grid_.get_selected_children();
        if (selected.empty()) return;

        auto old_name = std::string(selected[0]->get_name());

        auto* win = new Gtk::Window();
        win->set_title("Zmie\xc5\x84 nazw\xc4\x99");
        win->set_transient_for(*this);
        win->set_modal(true);
        win->set_decorated(false);
        win->set_resizable(false);
        win->set_hide_on_close(true);
        win->set_default_size(460, -1);
        win->add_css_class("hyalo-modal-window");

        auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
        content->add_css_class("hyalo-modal-surface");
        win->set_child(*content);

        auto* title_lbl = Gtk::make_managed<Gtk::Label>("Nowa nazwa:");
        title_lbl->set_halign(Gtk::Align::START);
        title_lbl->add_css_class("hyalo-modal-title");

        auto* entry = Gtk::make_managed<Gtk::Entry>();
        entry->set_text(old_name);
        entry->set_hexpand(true);
        entry->add_css_class("hyalo-modal-entry");

        auto dot = old_name.rfind('.');
        if (dot != std::string::npos && dot > 0) {
            entry->select_region(0, static_cast<int>(dot));
        } else {
            entry->select_region(0, -1);
        }

        content->append(*title_lbl);
        content->append(*entry);

        auto* btn_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        btn_box->set_homogeneous(true);
        btn_box->set_margin_top(8);

        auto* cancel_btn = Gtk::make_managed<Gtk::Button>("Anuluj");
        cancel_btn->add_css_class("hyalo-button");
        cancel_btn->add_css_class("hyalo-button-secondary");

        auto* rename_btn = Gtk::make_managed<Gtk::Button>("Zmie\xc5\x84");
        rename_btn->add_css_class("hyalo-button");
        rename_btn->add_css_class("hyalo-button-primary");

        btn_box->append(*cancel_btn);
        btn_box->append(*rename_btn);
        content->append(*btn_box);

        auto do_rename = [this, win, entry, old_name]() {
            auto new_name = std::string(entry->get_text());
            if (!new_name.empty() && new_name != old_name) {
                std::error_code ec;
                std::filesystem::rename(current_path_ / old_name, current_path_ / new_name, ec);
            }
            win->close();
            refresh();
        };

        rename_btn->signal_clicked().connect(do_rename);
        cancel_btn->signal_clicked().connect([win]() { win->close(); });
        entry->signal_activate().connect(do_rename);

        win->signal_hide().connect([win]() { delete win; });
        win->present();
        entry->grab_focus();
    }

    void trash_selected() {
        if (is_virtual()) return;
        auto selected = file_grid_.get_selected_children();
        for (auto* child : selected) {
            try {
                auto gfile = Gio::File::create_for_path(
                    (current_path_ / std::string(child->get_name())).string());
                gfile->trash();
            } catch (...) {}
        }
        refresh();
    }

    void copy_selected() {
        if (is_virtual()) return;
        clipboard_paths_.clear();
        clipboard_is_cut_ = false;
        for (auto* child : file_grid_.get_selected_children()) {
            clipboard_paths_.push_back(current_path_ / std::string(child->get_name()));
        }
        if (!clipboard_paths_.empty())
            status_label_.set_text("Skopiowano " + std::to_string(clipboard_paths_.size()) + " element(ow)");
    }

    void cut_selected() {
        if (is_virtual()) return;
        clipboard_paths_.clear();
        clipboard_is_cut_ = true;
        for (auto* child : file_grid_.get_selected_children()) {
            clipboard_paths_.push_back(current_path_ / std::string(child->get_name()));
        }
        if (!clipboard_paths_.empty())
            status_label_.set_text("Wyci\xc4\x99to " + std::to_string(clipboard_paths_.size()) + " element(ow)");
    }

    void paste_clipboard() {
        if (clipboard_paths_.empty() || is_virtual()) return;

        auto paths  = clipboard_paths_;
        auto is_cut = clipboard_is_cut_;
        auto dst_dir = current_path_;
        auto alive  = alive_;

        if (is_cut) {
            clipboard_paths_.clear();
            clipboard_is_cut_ = false;
        }

        status_label_.set_text(is_cut ? "Przenoszenie..." : "Kopiowanie...");

        std::thread([paths, is_cut, dst_dir, alive, this]() {
            for (const auto& src_path : paths) {
                if (!alive->load()) return;
                std::error_code ec;
                if (!std::filesystem::exists(src_path, ec)) continue;

                auto dst_path = dst_dir / src_path.filename();

                if (std::filesystem::exists(dst_path, ec) && dst_path != src_path) {
                    auto stem = src_path.stem().string();
                    auto ext  = src_path.extension().string();
                    int n = 1;
                    do {
                        dst_path = dst_dir / (stem + " (" + std::to_string(n++) + ")" + ext);
                    } while (std::filesystem::exists(dst_path, ec));
                }

                if (dst_path == src_path) continue;

                if (is_cut) {
                    std::filesystem::rename(src_path, dst_path, ec);
                    if (ec) {
                        std::filesystem::copy(src_path, dst_path,
                            std::filesystem::copy_options::recursive, ec);
                        if (!ec) std::filesystem::remove_all(src_path, ec);
                    }
                } else {
                    std::filesystem::copy(src_path, dst_path,
                        std::filesystem::copy_options::recursive, ec);
                }
            }

            post_to_main([alive, this]() {
                if (!alive->load()) return;
                status_label_.set_text("Gotowe");
                refresh();
            });
        }).detach();
    }

    void toggle_hidden() {
        show_hidden_ = !show_hidden_;
        toggling_hidden_ = true;
        hidden_btn_.set_active(show_hidden_);
        toggling_hidden_ = false;
        refilter_files();
    }

    void show_location_entry() {
        location_entry_.set_text(current_location_string());
        path_stack_.set_visible_child("entry");
        location_entry_.grab_focus();
        location_entry_.select_region(0, -1);
    }

    // ===================== context menu =====================

    void show_context_popup(double x, double y) {
        if (ctx_popover_ && ctx_popover_->get_parent()) {
            ctx_popover_->unparent();
        }

        auto menu = Gio::Menu::create();
        auto selected = file_grid_.get_selected_children();
        if (!selected.empty()) {
            menu->append("Otw\xc3\xb3rz",              "files.open");
            menu->append("Kopiuj",              "files.copy");
            menu->append("Wytnij",              "files.cut");
            menu->append("Zmie\xc5\x84 nazw\xc4\x99",         "files.rename");
            menu->append("Przenie\xc5\x9b do kosza",   "files.trash");
        }
        if (!clipboard_paths_.empty()) {
            menu->append("Wklej",               "files.paste");
        }
        menu->append("Nowy folder", "files.new-folder");
        menu->append("Od\xc5\x9bwie\xc5\xbc",     "files.refresh");

        ctx_popover_ = Gtk::make_managed<Gtk::PopoverMenu>(menu);
        ctx_popover_->set_parent(file_grid_);
        ctx_popover_->set_has_arrow(false);

        GdkRectangle rect{static_cast<int>(x), static_cast<int>(y), 1, 1};
        gtk_popover_set_pointing_to(GTK_POPOVER(ctx_popover_->gobj()), &rect);

        ctx_popover_->popup();
    }

    // ===================== directory monitoring =====================

    void start_monitoring() {
        if (monitor_changed_connection_.connected()) {
            monitor_changed_connection_.disconnect();
        }
        monitor_.reset();
        try {
            auto gfile = is_virtual()
                ? Gio::File::create_for_uri(current_uri_)
                : Gio::File::create_for_path(current_path_.string());
            monitor_ = gfile->monitor_directory();
            monitor_->set_rate_limit(1000); // max 1 event per second
            monitor_changed_connection_ = monitor_->signal_changed().connect(
                [this, alive = alive_](const Glib::RefPtr<Gio::File>&,
                       const Glib::RefPtr<Gio::File>&,
                       Gio::FileMonitor::Event event) {
                    if (!alive->load()) {
                        return;
                    }
                    if (event == Gio::FileMonitor::Event::CREATED
                        || event == Gio::FileMonitor::Event::DELETED
                        || event == Gio::FileMonitor::Event::RENAMED
                        || event == Gio::FileMonitor::Event::MOVED_IN
                        || event == Gio::FileMonitor::Event::MOVED_OUT) {
                        schedule_refresh();
                    }
                });
        } catch (...) {}
    }

    void schedule_refresh() {
        if (!refresh_pending_) {
            refresh_pending_ = true;
            refresh_timeout_connection_ = Glib::signal_timeout().connect([this, alive = alive_]() {
                refresh_pending_ = false;
                if (alive->load()) {
                    refresh();
                }
                return false;
            }, 800);
        }
    }

    // ===================== ui helpers =====================

    void update_nav_buttons() {
        back_btn_.set_sensitive(!back_stack_.empty());
        fwd_btn_.set_sensitive(!fwd_stack_.empty());
        up_btn_.set_sensitive(!is_virtual() && current_path_ != current_path_.root_path());
    }

    void update_path_display() {
        if (is_virtual()) {
            if (current_uri_ == "trash:///") {
                path_label_.set_text("Kosz");
                set_title("Kosz \xe2\x80\x94 Hyalo Pliki");
            } else {
                path_label_.set_text(current_uri_);
                set_title(current_uri_ + std::string(" \xe2\x80\x94 Hyalo Pliki"));
            }
        } else {
            path_label_.set_text(current_path_.string());
            auto name = current_path_.filename().string();
            set_title((name.empty() ? "/" : name) + std::string(" \xe2\x80\x94 Hyalo Pliki"));
        }
    }

    void update_status(int count) {
        if (count == 1)
            status_label_.set_text("1 element");
        else
            status_label_.set_text(std::to_string(count) + " element\xc3\xb3w");
    }

    // ===================== members =====================

    // Layout
    Gtk::Box            root_{Gtk::Orientation::VERTICAL};
    Gtk::Box            toolbar_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Paned          paned_{Gtk::Orientation::HORIZONTAL};
    Gtk::ScrolledWindow sidebar_scroll_;
    Gtk::ListBox        places_list_;
    Gtk::ScrolledWindow content_scroll_;
    Gtk::Stack          content_stack_;
    Gtk::Spinner        loading_spinner_;
    Gtk::FlowBox        file_grid_;
    Gtk::Label          status_label_;

    // Toolbar widgets
    Gtk::Button         back_btn_;
    Gtk::Button         fwd_btn_;
    Gtk::Button         up_btn_;
    Gtk::Label          path_label_;
    Gtk::Entry          location_entry_;
    Gtk::Stack          path_stack_;
    Gtk::ToggleButton   hidden_btn_;
    Gtk::Button         new_folder_btn_;
    Gtk::Button         refresh_btn_;

    // State
    std::filesystem::path              current_path_;
    std::string                        current_uri_;   // non-empty ⇒ virtual GIO location
    std::vector<std::string>           back_stack_;
    std::vector<std::string>           fwd_stack_;
    bool                               show_hidden_        = false;
    bool                               refresh_pending_    = false;
    bool                               toggling_hidden_    = false;
    std::vector<FileEntry>             entries_;
    std::vector<const FileEntry*>      visible_entries_;
    std::size_t                        render_index_        = 0;
    std::atomic<unsigned int>          populate_generation_{0};

    // Clipboard
    std::vector<std::filesystem::path> clipboard_paths_;
    bool                               clipboard_is_cut_   = false;

    // Lifetime — atomic so worker threads can check safely
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    // GIO
    Glib::RefPtr<Gio::FileMonitor>      monitor_;
    Glib::RefPtr<Gio::SimpleActionGroup> file_actions_;
    sigc::connection                    monitor_changed_connection_;
    sigc::connection                    refresh_timeout_connection_;

    // Navigation / context
    Glib::RefPtr<Gtk::GestureClick>      mouse_nav_gesture_;
    Gtk::PopoverMenu* ctx_popover_ = nullptr;
};

}  // namespace

// ===================== main =====================

int main(int argc, char** argv) {
    configure_stable_gsk_renderer();

    auto app = Gtk::Application::create("org.hyalo.Files");

    const auto runtime_paths = hyalo::core::detect_runtime_paths();
    auto config_manager = hyalo::core::ConfigManager(runtime_paths);
    if (!config_manager.load()) {
        config_manager.load_defaults();
    }
    if (!hyalo::core::StyleManager::apply(config_manager)) {
        apply_hyalo_fallback_style(runtime_paths);
    }

    app->signal_activate().connect([&app]() {
        auto* window = new FileManagerWindow();
        app->add_window(*window);
        window->present();
    });

    return app->run(argc, argv);
}
