#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <functional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <glib.h>

#include <glibmm/main.h>

#include <gtkmm/application.h>
#include <gtkmm/applicationwindow.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/dialog.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/listboxrow.h>
#include <gtkmm/revealer.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/searchentry.h>
#include <gtkmm/separator.h>
#include <gtkmm/spinner.h>
#include <gtkmm/textbuffer.h>
#include <gtkmm/textview.h>

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

void schedule_on_main(std::function<void()> callback) {
    auto* data = new std::function<void()>(std::move(callback));
    g_idle_add([](gpointer user_data) -> gboolean {
        auto* fn = static_cast<std::function<void()>*>(user_data);
        (*fn)();
        delete fn;
        return G_SOURCE_REMOVE;
    }, data);
}

bool command_exists(const std::string& command) {
    const auto probe = "command -v " + command + " >/dev/null 2>&1";
    return std::system(probe.c_str()) == 0;
}

void secure_clear(std::string& value) {
    std::fill(value.begin(), value.end(), '\0');
    value.clear();
    value.shrink_to_fit();
}

std::string shell_escape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('\'');

    for (char ch : value) {
        if (ch == '\'') {
            escaped += "'\\''";
        } else {
            escaped.push_back(ch);
        }
    }

    escaped.push_back('\'');
    return escaped;
}

struct CommandResult {
    int exit_code = 1;
    std::string output;
};

CommandResult run_command_with_status(const std::string& command) {
    const std::string marker = "__HYALO_STORE_EXIT__=";
    const auto payload = "{ " + command + "; } 2>&1; printf '\\n" + marker + "%d' $?";
    const auto invoke = "bash -lc " + shell_escape(payload);

    std::array<char, 512> buffer{};
    std::string output;

    FILE* pipe = popen(invoke.c_str(), "r");
    if (!pipe) {
        return {127, "Nie udało się uruchomić polecenia."};
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        output += buffer.data();
    }

    pclose(pipe);

    const auto marker_pos = output.rfind(marker);
    if (marker_pos == std::string::npos) {
        return {1, output};
    }

    int exit_code = 1;
    const auto code_text = output.substr(marker_pos + marker.size());
    try {
        exit_code = std::stoi(code_text);
    } catch (...) {
        exit_code = 1;
    }

    output.erase(marker_pos);
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }

    return {exit_code, output};
}

bool refresh_sudo_session_with_password(std::string& password) {
    if (password.empty()) {
        return false;
    }

    FILE* pipe = popen("sudo -S -p '' -v 2>/dev/null", "w");
    if (!pipe) {
        secure_clear(password);
        return false;
    }

    const auto payload = password + "\n";
    const auto write_ok = std::fwrite(payload.data(), 1, payload.size(), pipe) == payload.size();
    secure_clear(password);

    const int status = pclose(pipe);
    return write_ok && status == 0;
}

bool has_active_sudo_session() {
    return std::system("sudo -n -v >/dev/null 2>&1") == 0;
}

std::string inject_sudo_password_pipe(const std::string& command, const std::string& password) {
    // Replace "sudo -n" with password-piped "sudo -S -p ''" so it works in popen()
    // because popen() runs without a TTY, making sudo -n timestamp checks fail.
    std::string result = command;
    const std::string needle = "sudo -n ";
    std::string::size_type pos = 0;
    while ((pos = result.find(needle, pos)) != std::string::npos) {
        result.replace(pos, needle.size(), "sudo -S -p '' ");
        pos += 14;
    }
    // Pipe the password into the first sudo via a heredoc-style echo
    return "echo " + shell_escape(password) + " | " + result;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    return lines;
}

std::string trim(const std::string& text) {
    const auto start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}

struct PackageEntry {
    std::string source;
    std::string id;
    std::string name;
    std::string version;
    std::string description;
    bool installed = false;
};

std::vector<PackageEntry> parse_pacman_style_search(const std::string& output, const std::string& source) {
    std::vector<PackageEntry> results;
    auto lines = split_lines(output);

    PackageEntry current;
    bool has_current = false;

    for (const auto& raw_line : lines) {
        if (raw_line.empty()) {
            continue;
        }

        const bool is_description_line = !raw_line.empty() && std::isspace(static_cast<unsigned char>(raw_line.front()));
        if (is_description_line && has_current) {
            auto desc = trim(raw_line);
            if (!desc.empty()) {
                current.description = desc;
            }
            continue;
        }

        if (has_current) {
            results.push_back(current);
            has_current = false;
        }

        std::istringstream stream(raw_line);
        std::string first_token;
        std::string version;
        stream >> first_token >> version;
        if (first_token.empty()) {
            continue;
        }

        std::string id = first_token;
        const auto slash = first_token.find('/');
        if (slash != std::string::npos && slash + 1 < first_token.size()) {
            id = first_token.substr(slash + 1);
        }

        current = PackageEntry{};
        current.source = source;
        current.id = id;
        current.name = id;
        current.version = version;
        current.description = "Brak opisu";
        has_current = true;
    }

    if (has_current) {
        results.push_back(current);
    }

    return results;
}

std::vector<PackageEntry> parse_flatpak_search(const std::string& output) {
    std::vector<PackageEntry> results;
    auto lines = split_lines(output);

    for (const auto& line : lines) {
        const auto trimmed = trim(line);
        if (trimmed.empty()) {
            continue;
        }

        if (trimmed.rfind("Application", 0) == 0) {
            continue;
        }

        std::vector<std::string> columns;
        std::string token;
        std::istringstream stream(trimmed);
        while (std::getline(stream, token, '\t')) {
            columns.push_back(trim(token));
        }

        if (columns.size() < 2) {
            std::istringstream fallback(trimmed);
            std::string col1;
            std::string col2;
            fallback >> col1 >> col2;
            if (!col1.empty() && !col2.empty()) {
                columns = {col1, col2};
            }
        }

        if (columns.size() < 2) {
            continue;
        }

        PackageEntry entry;
        entry.source = "flatpak";
        entry.id = columns[0];
        entry.name = columns[1].empty() ? columns[0] : columns[1];
        entry.version = "flatpak";
        if (columns.size() > 2 && !columns[2].empty()) {
            entry.description = columns[2];
        } else {
            entry.description = "Pakiet Flatpak";
        }
        results.push_back(entry);
    }

    return results;
}

class PackageRow final : public Gtk::ListBoxRow {
public:
    PackageRow(
        const PackageEntry& entry,
        const std::function<void(const std::string&)>& on_show_details,
        const std::function<void(const std::string&)>& on_toggle_install
    )
        : entry_(entry),
          root_(Gtk::Orientation::HORIZONTAL, 12),
          text_box_(Gtk::Orientation::VERTICAL, 4),
          title_(entry.name),
          subtitle_(entry.description),
          source_badge_(entry.source == "flatpak" ? "Flatpak" : (entry.source == "aur" ? "AUR" : "Pacman")),
          state_badge_(entry.installed ? "Zainstalowany" : "Dostępny"),
          action_button_(entry.installed ? "Usuń" : "Zainstaluj") {
        add_css_class("store-result-row");

        title_.set_halign(Gtk::Align::START);
        title_.set_hexpand(true);
        title_.add_css_class("store-result-title");

        subtitle_.set_halign(Gtk::Align::START);
        subtitle_.set_wrap(true);
        subtitle_.set_max_width_chars(72);
        subtitle_.set_ellipsize(Pango::EllipsizeMode::END);
        subtitle_.add_css_class("store-result-subtitle");

        source_badge_.add_css_class("store-result-badge");
        source_badge_.set_halign(Gtk::Align::CENTER);

        state_badge_.add_css_class("store-result-state-badge");
        state_badge_.add_css_class(entry.installed ? "store-result-state-installed" : "store-result-state-available");
        state_badge_.set_halign(Gtk::Align::CENTER);

        text_box_.set_hexpand(true);
        text_box_.append(title_);
        text_box_.append(subtitle_);

        action_button_.add_css_class("hyalo-button");
        action_button_.add_css_class(entry.installed ? "hyalo-button-danger" : "hyalo-button-primary");
        action_button_.signal_clicked().connect([this, on_toggle_install]() {
            on_toggle_install(entry_.id);
        });

        auto* details_button = Gtk::make_managed<Gtk::Button>("Szczegóły");
        details_button->add_css_class("hyalo-button");
        details_button->add_css_class("hyalo-button-secondary");
        details_button->signal_clicked().connect([this, on_show_details]() {
            on_show_details(entry_.id);
        });

        root_.append(source_badge_);
        root_.append(state_badge_);
        root_.append(text_box_);
        root_.append(*details_button);
        root_.append(action_button_);

        set_child(root_);
    }

    const std::string& id() const {
        return entry_.id;
    }

private:
    PackageEntry entry_;
    Gtk::Box root_;
    Gtk::Box text_box_;
    Gtk::Label title_;
    Gtk::Label subtitle_;
    Gtk::Label source_badge_;
    Gtk::Label state_badge_;
    Gtk::Button action_button_;
};

class SoftwareStoreWindow final : public Gtk::ApplicationWindow {
public:
    SoftwareStoreWindow()
        : root_(Gtk::Orientation::VERTICAL, 8),
          header_row_(Gtk::Orientation::HORIZONTAL, 12),
          status_box_(Gtk::Orientation::HORIZONTAL, 8),
          toolbar_row_(Gtk::Orientation::HORIZONTAL, 8),
          filter_row_(Gtk::Orientation::HORIZONTAL, 10),
          chips_row_(Gtk::Orientation::HORIZONTAL, 6),
          content_row_(Gtk::Orientation::HORIZONTAL, 12),
          results_panel_(Gtk::Orientation::VERTICAL, 6),
          details_box_(Gtk::Orientation::VERTICAL, 10),
          pagination_row_(Gtk::Orientation::HORIZONTAL, 8),
          search_button_("Szukaj"),
          sample_az_button_("A-Z"),
          refresh_button_("Odśwież"),
          chip_all_button_("Wszystkie"),
          chip_pacman_button_("Pacman"),
          chip_aur_button_("AUR"),
          chip_flatpak_button_("Flatpak"),
          prev_page_button_("Poprzednia"),
          next_page_button_("Następna"),
          backend_status_(""),
          details_header_("Szczegóły pakietu"),
          details_body_("Wybierz pakiet z listy, aby zobaczyć szczegóły.") {
        set_title("Hyalo Software Store");
        set_default_size(1160, 760);

        root_.set_margin_top(14);
        root_.set_margin_bottom(14);
        root_.set_margin_start(16);
        root_.set_margin_end(16);

        // --- Header row: title + backend info ---
        title_.set_markup("<b>Sklep aplikacji HyaloOS</b>");
        title_.set_halign(Gtk::Align::START);
        title_.add_css_class("store-section-header");
        backend_status_.set_halign(Gtk::Align::END);
        backend_status_.set_hexpand(true);
        backend_status_.add_css_class("store-results-meta");
        header_row_.append(title_);
        header_row_.append(backend_status_);

        // --- Status bar (spinner + message) ---
        status_revealer_.set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
        status_revealer_.set_transition_duration(200);
        status_revealer_.set_reveal_child(false);
        status_revealer_.set_hexpand(true);
        status_revealer_.add_css_class("store-status-revealer");

        status_box_.add_css_class("store-status");
        status_spinner_.set_size_request(16, 16);
        status_message_.set_wrap(true);
        status_message_.set_xalign(0.0f);
        status_message_.set_hexpand(true);
        status_box_.append(status_spinner_);
        status_box_.append(status_message_);
        status_revealer_.set_child(status_box_);

        // --- Toolbar row: search entry + buttons ---
        search_entry_.set_hexpand(true);
        search_entry_.set_placeholder_text("Wyszukaj pakiet, np. firefox, vlc...");
        search_entry_.add_css_class("store-search-entry");

        search_button_.add_css_class("hyalo-button");
        search_button_.add_css_class("hyalo-button-primary");
        sample_az_button_.add_css_class("hyalo-button");
        sample_az_button_.add_css_class("hyalo-button-secondary");
        sample_az_button_.set_tooltip_text("Pokaż 50 pakietów A-Z");
        refresh_button_.add_css_class("hyalo-button");
        refresh_button_.add_css_class("hyalo-button-secondary");
        refresh_button_.set_tooltip_text("Odśwież metadane repozytoriów");

        toolbar_row_.append(search_entry_);
        toolbar_row_.append(search_button_);
        toolbar_row_.append(sample_az_button_);
        toolbar_row_.append(refresh_button_);

        // --- Filter row: source, installed, sort, power saver ---
        source_combo_.append("all", "Wszystkie źródła");
        source_combo_.append("pacman", "Pacman");
        source_combo_.append("aur", "AUR");
        source_combo_.append("flatpak", "Flatpak");
        source_combo_.set_active_id("all");

        installed_only_toggle_.set_label("Tylko zainstalowane");
        sort_combo_.append("name_asc", "Nazwa A-Z");
        sort_combo_.append("name_desc", "Nazwa Z-A");
        sort_combo_.append("source", "Źródło");
        sort_combo_.append("installed", "Najpierw zainstalowane");
        sort_combo_.set_active_id("name_asc");
        power_saver_toggle_.set_label("Oszczędny");
        power_saver_toggle_.set_tooltip_text("Wyłącza wyszukiwanie live, szukaj tylko Enter/Szukaj.");

        filter_row_.append(source_combo_);
        filter_row_.append(installed_only_toggle_);
        filter_row_.append(sort_combo_);
        filter_row_.append(power_saver_toggle_);

        // --- Source chips ---
        chip_all_button_.add_css_class("store-chip-button");
        chip_pacman_button_.add_css_class("store-chip-button");
        chip_aur_button_.add_css_class("store-chip-button");
        chip_flatpak_button_.add_css_class("store-chip-button");
        chips_row_.append(chip_all_button_);
        chips_row_.append(chip_pacman_button_);
        chips_row_.append(chip_aur_button_);
        chips_row_.append(chip_flatpak_button_);

        // --- Results panel ---
        results_list_.set_selection_mode(Gtk::SelectionMode::SINGLE);
        results_list_.set_vexpand(true);
        results_list_.add_css_class("store-results-list");
        results_scroll_.set_expand(true);
        results_scroll_.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
        results_scroll_.set_child(results_list_);

        results_header_.set_text("Lista aplikacji");
        results_header_.add_css_class("store-section-header");
        results_header_.set_halign(Gtk::Align::START);

        results_meta_.set_text("Brak wyników");
        results_meta_.add_css_class("store-results-meta");
        results_meta_.set_halign(Gtk::Align::START);

        results_panel_.add_css_class("store-panel");
        results_panel_.set_hexpand(true);
        results_panel_.append(results_header_);
        results_panel_.append(results_meta_);
        results_panel_.append(results_scroll_);

        // --- Details panel ---
        details_header_.set_halign(Gtk::Align::START);
        details_header_.add_css_class("store-details-header");

        details_body_.set_wrap(true);
        details_body_.set_halign(Gtk::Align::START);
        details_body_.set_xalign(0.0f);
        details_body_.add_css_class("store-details-body");

        details_box_.add_css_class("hyalo-surface");
        details_box_.add_css_class("store-panel");
        details_box_.set_size_request(320, -1);
        details_box_.append(details_header_);
        details_box_.append(details_body_);

        content_row_.set_vexpand(true);
        content_row_.append(results_panel_);
        content_row_.append(details_box_);

        // --- Pagination ---
        page_label_.set_halign(Gtk::Align::START);
        page_label_.set_hexpand(true);
        page_label_.add_css_class("store-page-label");
        prev_page_button_.add_css_class("hyalo-button");
        prev_page_button_.add_css_class("hyalo-button-secondary");
        next_page_button_.add_css_class("hyalo-button");
        next_page_button_.add_css_class("hyalo-button-secondary");
        pagination_row_.append(prev_page_button_);
        pagination_row_.append(next_page_button_);
        pagination_row_.append(page_label_);

        // --- Log ---
        log_view_.set_editable(false);
        log_view_.set_monospace(true);
        log_view_.set_vexpand(true);
        log_scroll_.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
        log_scroll_.set_size_request(-1, 100);
        log_scroll_.set_child(log_view_);

        // --- Assemble root ---
        root_.append(header_row_);
        root_.append(status_revealer_);
        root_.append(toolbar_row_);
        root_.append(filter_row_);
        root_.append(chips_row_);
        root_.append(content_row_);
        root_.append(pagination_row_);
        root_.append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));
        root_.append(log_scroll_);

        set_child(root_);

        // --- Signals ---
        search_button_.signal_clicked().connect(sigc::mem_fun(*this, &SoftwareStoreWindow::run_search));
        sample_az_button_.signal_clicked().connect(sigc::mem_fun(*this, &SoftwareStoreWindow::load_alphabetical_preview));
        refresh_button_.signal_clicked().connect(sigc::mem_fun(*this, &SoftwareStoreWindow::refresh_metadata));
        search_entry_.signal_activate().connect(sigc::mem_fun(*this, &SoftwareStoreWindow::run_search));
        search_entry_.signal_search_changed().connect(sigc::mem_fun(*this, &SoftwareStoreWindow::schedule_debounced_search));
        results_list_.signal_row_selected().connect(sigc::mem_fun(*this, &SoftwareStoreWindow::on_row_selected));
        installed_only_toggle_.signal_toggled().connect(sigc::mem_fun(*this, &SoftwareStoreWindow::on_filter_or_sort_changed));
        sort_combo_.signal_changed().connect(sigc::mem_fun(*this, &SoftwareStoreWindow::on_filter_or_sort_changed));
        power_saver_toggle_.signal_toggled().connect(sigc::mem_fun(*this, &SoftwareStoreWindow::on_power_mode_toggled));
        prev_page_button_.signal_clicked().connect(sigc::mem_fun(*this, &SoftwareStoreWindow::go_to_previous_page));
        next_page_button_.signal_clicked().connect(sigc::mem_fun(*this, &SoftwareStoreWindow::go_to_next_page));
        chip_all_button_.signal_clicked().connect([this]() { set_source_filter("all"); });
        chip_pacman_button_.signal_clicked().connect([this]() { set_source_filter("pacman"); });
        chip_aur_button_.signal_clicked().connect([this]() { set_source_filter("aur"); });
        chip_flatpak_button_.signal_clicked().connect([this]() { set_source_filter("flatpak"); });

        detect_backends();
        refresh_installed_caches(true);
        update_source_chip_styles();
        update_pagination_controls();
        append_log("[Software Store] Gotowe. Wyszukaj pakiet, aby zobaczyć listę z akcjami Zainstaluj/Usuń.");
    }

private:
    static constexpr std::size_t kPageSize = 30;
    static constexpr std::size_t kMaxAllResults = 120;
    static constexpr std::size_t kAlphabeticalPreviewCount = 50;

    bool ensure_sudo_session() {
        if (!command_exists("sudo")) {
            append_log("sudo nie jest dostępne w systemie.");
            return false;
        }

        if (has_active_sudo_session()) {
            return true;
        }

        constexpr int kMaxAttempts = 3;

        Gtk::Window dialog;
        dialog.set_modal(true);
        dialog.set_hide_on_close(true);
        dialog.set_decorated(false);
        dialog.set_resizable(false);
        dialog.set_default_size(460, -1);
        dialog.set_title("Wymagana autoryzacja");
        dialog.set_transient_for(*this);
        dialog.add_css_class("update-auth-modal");

        auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        content->add_css_class("update-auth-modal-surface");
        dialog.set_child(*content);

        auto subtitle = Gtk::Label("Sesja sudo wygasła. Podaj hasło administratora, aby kontynuować operację.");
        subtitle.set_wrap(true);
        subtitle.set_xalign(0.0f);
        subtitle.add_css_class("update-auth-modal-subtitle");

        auto password_label = Gtk::Label("Hasło administratora");
        password_label.set_xalign(0.0f);
        password_label.add_css_class("update-auth-modal-label");

        auto password_entry = Gtk::Entry();
        password_entry.set_visibility(false);
        password_entry.set_hexpand(true);
        password_entry.add_css_class("update-auth-modal-entry");

        auto error_label = Gtk::Label();
        error_label.set_xalign(0.0f);
        error_label.set_wrap(true);
        error_label.add_css_class("update-auth-modal-error");

        content->append(subtitle);
        content->append(password_label);
        content->append(password_entry);
        content->append(error_label);

        auto* btn_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        btn_box->set_homogeneous(true);
        btn_box->set_margin_top(8);

        auto* cancel_button = Gtk::make_managed<Gtk::Button>("Anuluj");
        cancel_button->add_css_class("hyalo-button");
        cancel_button->add_css_class("hyalo-button-secondary");
        cancel_button->add_css_class("update-auth-modal-action");
        cancel_button->add_css_class("update-auth-modal-cancel");

        auto* unlock_button = Gtk::make_managed<Gtk::Button>("Odblokuj");
        unlock_button->add_css_class("hyalo-button");
        unlock_button->add_css_class("hyalo-button-primary");
        unlock_button->add_css_class("update-auth-modal-action");
        unlock_button->add_css_class("update-auth-modal-primary");

        btn_box->append(*cancel_button);
        btn_box->append(*unlock_button);
        content->append(*btn_box);

        auto loop = Glib::MainLoop::create();
        bool authorized = false;
        int attempts = 0;

        auto do_unlock = [&]() {
            std::string password = password_entry.get_text();
            if (refresh_sudo_session_with_password(password)) {
                authorized = true;
                sudo_password_ = password_entry.get_text();
                password_entry.set_text("");
                error_label.set_text("");
                dialog.close();
                return;
            }

            ++attempts;
            password_entry.set_text("");
            error_label.set_text("Niepoprawne hasło. Pozostałe próby: " + std::to_string(std::max(0, kMaxAttempts - attempts)) + ".");
            password_entry.grab_focus();

            if (attempts >= kMaxAttempts) {
                error_label.set_text("Przekroczono limit prób logowania.");
                password_entry.set_sensitive(false);
                unlock_button->set_sensitive(false);
                cancel_button->grab_focus();
            }
        };

        unlock_button->signal_clicked().connect(do_unlock);
        password_entry.signal_activate().connect(do_unlock);
        cancel_button->signal_clicked().connect([&]() { dialog.close(); });

        dialog.signal_hide().connect([&]() {
            if (loop && loop->is_running()) {
                loop->quit();
            }
        });

        dialog.present();
        password_entry.grab_focus();
        loop->run();

        if (!authorized) {
            append_log("Operacja anulowana - nie odświeżono sesji sudo.");
        }
        return authorized;
    }

    void detect_backends() {
        has_pacman_ = command_exists("pacman");
        has_flatpak_ = command_exists("flatpak");
        if (command_exists("paru")) {
            aur_helper_ = "paru";
        } else if (command_exists("yay")) {
            aur_helper_ = "yay";
        }

        std::ostringstream status;
        status << "Pacman: " << (has_pacman_ ? "tak" : "nie")
               << " | AUR helper: " << (aur_helper_.empty() ? "brak" : aur_helper_)
               << " | Flatpak: " << (has_flatpak_ ? "tak" : "nie");
        backend_status_.set_text(status.str());
    }

    std::string selected_source() const {
        const auto id = source_combo_.get_active_id();
        return id.empty() ? "pacman" : id;
    }

    bool needs_installed_cache_refresh() const {
        if (!installed_cache_ready_) {
            return true;
        }

        const auto now = std::chrono::steady_clock::now();
        return (now - installed_cache_updated_at_) > std::chrono::seconds(45);
    }

    void refresh_installed_caches(bool force) {
        if (!force && !needs_installed_cache_refresh()) {
            return;
        }

        installed_pacman_.clear();
        installed_flatpak_.clear();

        if (has_pacman_) {
            const auto pacman_list = run_command_with_status("pacman -Qq 2>/dev/null | head -n 20000").output;
            for (const auto& line : split_lines(pacman_list)) {
                const auto item = trim(line);
                if (!item.empty()) {
                    installed_pacman_.insert(item);
                }
            }
        }

        if (has_flatpak_) {
            const auto flatpak_list = run_command_with_status("flatpak list --columns=application 2>/dev/null | head -n 20000").output;
            for (const auto& line : split_lines(flatpak_list)) {
                const auto item = trim(line);
                if (!item.empty()) {
                    installed_flatpak_.insert(item);
                }
            }
        }

        installed_cache_ready_ = true;
        installed_cache_updated_at_ = std::chrono::steady_clock::now();
    }

    bool is_installed(const PackageEntry& entry) const {
        if (entry.source == "flatpak") {
            return installed_flatpak_.find(entry.id) != installed_flatpak_.end();
        }
        return installed_pacman_.find(entry.id) != installed_pacman_.end();
    }

    std::vector<PackageEntry> query_packages(const std::string& source, const std::string& query) {
        if (source == "all") {
            std::vector<PackageEntry> combined;
            combined.reserve(kMaxAllResults);

            const auto append_limited = [this, &combined](std::vector<PackageEntry>& items) {
                for (auto& item : items) {
                    if (combined.size() >= kMaxAllResults) {
                        last_search_truncated_ = true;
                        break;
                    }
                    combined.push_back(std::move(item));
                }
            };

            if (has_pacman_) {
                auto pac = query_packages("pacman", query);
                append_limited(pac);
            }
            if (!aur_helper_.empty() && combined.size() < kMaxAllResults) {
                auto aur = query_packages("aur", query);
                append_limited(aur);
            }
            if (has_flatpak_ && combined.size() < kMaxAllResults) {
                auto flat = query_packages("flatpak", query);
                append_limited(flat);
            }
            return combined;
        }

        std::string command;
        if (source == "pacman") {
            command = "pacman -Ss " + shell_escape(query) + " 2>/dev/null | head -n 300";
            auto parsed = parse_pacman_style_search(run_command_with_status(command).output, "pacman");
            for (auto& entry : parsed) {
                entry.installed = is_installed(entry);
            }
            return parsed;
        }

        if (source == "aur") {
            command = aur_helper_ + " -Ss " + shell_escape(query) + " 2>/dev/null | head -n 300";
            auto parsed = parse_pacman_style_search(run_command_with_status(command).output, "aur");
            for (auto& entry : parsed) {
                entry.installed = is_installed(entry);
            }
            return parsed;
        }

        command = "flatpak search --columns=application,name,description " + shell_escape(query) + " 2>/dev/null | head -n 300";
        auto parsed = parse_flatpak_search(run_command_with_status(command).output);
        for (auto& entry : parsed) {
            entry.installed = is_installed(entry);
        }
        return parsed;
    }

    std::vector<std::string> fetch_catalog_ids(const std::string& source) {
        std::string command;
        if (source == "pacman") {
            command = "pacman -Slq 2>/dev/null | head -n 1200";
        } else if (source == "aur") {
            if (aur_helper_.empty()) {
                return {};
            }
            command = aur_helper_ + " -Slq 2>/dev/null | head -n 1200";
        } else if (source == "flatpak") {
            command = "flatpak remote-ls --columns=application flathub 2>/dev/null | head -n 1200";
        } else {
            return {};
        }

        auto output = run_command_with_status(command).output;
        auto lines = split_lines(output);

        std::unordered_set<std::string> unique;
        std::vector<std::string> ids;
        ids.reserve(lines.size());

        for (auto& line : lines) {
            auto id = trim(line);
            if (id.empty()) {
                continue;
            }

            const auto slash = id.find('/');
            if (slash != std::string::npos && slash + 1 < id.size()) {
                id = id.substr(slash + 1);
            }

            if (unique.insert(id).second) {
                ids.push_back(id);
            }
        }

        std::sort(ids.begin(), ids.end());
        return ids;
    }

    void append_preview_entries_from_source(const std::string& source, std::vector<PackageEntry>& out) {
        auto ids = fetch_catalog_ids(source);
        for (const auto& id : ids) {
            if (out.size() >= kAlphabeticalPreviewCount) {
                break;
            }

            PackageEntry entry;
            entry.source = source;
            entry.id = id;
            entry.name = id;
            entry.version = source;
            entry.description = "Pozycja z listy alfabetycznej (podgląd działania sklepu).";
            entry.installed = is_installed(entry);
            out.push_back(entry);
        }
    }

    void show_status(const std::string& message) {
        status_message_.set_text(message);
        status_spinner_.start();
        status_spinner_.set_visible(true);
        status_revealer_.set_reveal_child(true);
    }

    void hide_status() {
        status_spinner_.stop();
        status_spinner_.set_visible(false);
        status_revealer_.set_reveal_child(false);
    }

    void load_alphabetical_preview() {
        if (async_busy_) {
            return;
        }

        refresh_installed_caches(false);

        async_busy_ = true;
        set_actions_sensitive(false);
        show_status("Ładowanie podglądu A-Z...");

        const auto source = selected_source();

        std::thread([this, source]() {
            std::vector<PackageEntry> preview;
            preview.reserve(kAlphabeticalPreviewCount);

            if (source == "all") {
                append_preview_entries_from_source("pacman", preview);
                append_preview_entries_from_source("aur", preview);
                append_preview_entries_from_source("flatpak", preview);
                std::sort(preview.begin(), preview.end(), [](const PackageEntry& a, const PackageEntry& b) {
                    return a.name < b.name;
                });
                if (preview.size() > kAlphabeticalPreviewCount) {
                    preview.resize(kAlphabeticalPreviewCount);
                }
            } else {
                append_preview_entries_from_source(source, preview);
            }

            schedule_on_main([this, preview = std::move(preview)]() {
                search_results_ = preview;
                current_page_ = 0;
                rebuild_results_list();

                async_busy_ = false;
                set_actions_sensitive(true);
                hide_status();
                detect_backends();

                append_log("[Podgląd A-Z] Załadowano " + std::to_string(search_results_.size()) + " pozycji.");
            });
        }).detach();
    }

    void run_search() {
        if (async_busy_) {
            return;
        }

        const auto query = trim(search_entry_.get_text());
        if (query.empty()) {
            append_log("Podaj frazę wyszukiwania.");
            search_results_.clear();
            visible_results_.clear();
            current_page_ = 0;
            rebuild_results_list();
            return;
        }

        const auto source = selected_source();
        if (source == "all" && !has_pacman_ && aur_helper_.empty() && !has_flatpak_) {
            append_log("Nie wykryto żadnego dostępnego źródła pakietów.");
            return;
        }
        if (source == "pacman" && !has_pacman_) {
            append_log("Pacman nie jest dostępny.");
            return;
        }
        if (source == "aur" && aur_helper_.empty()) {
            append_log("Brak helpera AUR (paru/yay).");
            return;
        }
        if (source == "flatpak" && !has_flatpak_) {
            append_log("Flatpak nie jest dostępny.");
            return;
        }

        refresh_installed_caches(false);

        async_busy_ = true;
        set_actions_sensitive(false);
        show_status("Wyszukiwanie: " + query + "...");

        std::thread([this, source, query]() {
            last_search_truncated_ = false;
            auto results = query_packages(source, query);

            schedule_on_main([this, results = std::move(results), query]() {
                search_results_ = results;
                current_page_ = 0;
                rebuild_results_list();

                async_busy_ = false;
                set_actions_sensitive(true);
                hide_status();
                detect_backends();

                append_log("Znaleziono " + std::to_string(search_results_.size()) + " wyników dla: " + query);
                if (last_search_truncated_) {
                    append_log("[INFO] Wyniki ograniczono do " + std::to_string(kMaxAllResults) + " pozycji.");
                }
            });
        }).detach();
    }

    void schedule_debounced_search() {
        if (power_saver_toggle_.get_active()) {
            return;
        }

        if (search_debounce_connection_.connected()) {
            search_debounce_connection_.disconnect();
        }

        search_debounce_connection_ = Glib::signal_timeout().connect(
            sigc::mem_fun(*this, &SoftwareStoreWindow::on_search_debounce_timeout),
            300,
            Glib::PRIORITY_DEFAULT
        );
    }

    bool on_search_debounce_timeout() {
        run_search();
        return false;
    }

    void on_power_mode_toggled() {
        if (power_saver_toggle_.get_active() && search_debounce_connection_.connected()) {
            search_debounce_connection_.disconnect();
        }

        append_log(power_saver_toggle_.get_active()
            ? "[Tryb oszczędny] Wyszukiwanie live wyłączone. Użyj Enter lub przycisku Szukaj."
            : "[Tryb oszczędny] Wyszukiwanie live włączone (debounce 300 ms).");
    }

    void set_source_filter(const std::string& filter) {
        active_source_filter_ = filter;
        update_source_chip_styles();
        current_page_ = 0;
        rebuild_results_list();
    }

    void update_source_chip_styles() {
        auto apply = [this](Gtk::Button& button, const std::string& id) {
            button.remove_css_class("store-chip-button-active");
            if (active_source_filter_ == id) {
                button.add_css_class("store-chip-button-active");
            }
        };

        apply(chip_all_button_, "all");
        apply(chip_pacman_button_, "pacman");
        apply(chip_aur_button_, "aur");
        apply(chip_flatpak_button_, "flatpak");
    }

    void on_filter_or_sort_changed() {
        current_page_ = 0;
        rebuild_results_list();
    }

    void go_to_previous_page() {
        if (current_page_ == 0) {
            return;
        }
        --current_page_;
        rebuild_results_list();
    }

    void go_to_next_page() {
        const auto total_pages = page_count();
        if (current_page_ + 1 >= total_pages) {
            return;
        }
        ++current_page_;
        rebuild_results_list();
    }

    std::size_t page_count() const {
        if (visible_results_.empty()) {
            return 1;
        }
        return (visible_results_.size() + kPageSize - 1) / kPageSize;
    }

    void rebuild_results_list() {
        apply_filters_and_sort();

        const auto total_pages = page_count();
        if (current_page_ >= total_pages) {
            current_page_ = total_pages > 0 ? total_pages - 1 : 0;
        }

        while (auto* row = results_list_.get_row_at_index(0)) {
            results_list_.remove(*row);
        }

        if (visible_results_.empty()) {
            details_header_.set_text("Brak wyników");
            details_body_.set_text("Spróbuj zmienić frazę, źródło lub ustawienia filtra/sortowania.");
            update_pagination_controls();
            return;
        }

        const auto start = current_page_ * kPageSize;
        const auto end = std::min(start + kPageSize, visible_results_.size());

        for (std::size_t i = start; i < end; ++i) {
            const auto& entry = visible_results_[i];
            auto* row = Gtk::make_managed<PackageRow>(
                entry,
                [this](const std::string& id) { show_details(id); },
                [this](const std::string& id) { toggle_install(id); }
            );
            results_list_.append(*row);
        }

        update_pagination_controls();
    }

    void update_pagination_controls() {
        const auto total_pages = page_count();
        std::ostringstream label;
        label << "Strona " << (current_page_ + 1) << " / " << total_pages
              << " | Wyników: " << visible_results_.size();
        page_label_.set_text(label.str());

        prev_page_button_.set_sensitive(current_page_ > 0);
        next_page_button_.set_sensitive((current_page_ + 1) < total_pages);

        std::ostringstream meta;
        meta << "Widoczne: " << visible_results_.size() << " | Strona " << (current_page_ + 1) << "/" << total_pages;
        results_meta_.set_text(meta.str());
    }

    void apply_filters_and_sort() {
        visible_results_ = search_results_;

        if (active_source_filter_ != "all") {
            visible_results_.erase(
                std::remove_if(
                    visible_results_.begin(),
                    visible_results_.end(),
                    [this](const PackageEntry& entry) { return entry.source != active_source_filter_; }
                ),
                visible_results_.end()
            );
        }

        if (installed_only_toggle_.get_active()) {
            visible_results_.erase(
                std::remove_if(
                    visible_results_.begin(),
                    visible_results_.end(),
                    [](const PackageEntry& entry) { return !entry.installed; }
                ),
                visible_results_.end()
            );
        }

        const auto sort_id = sort_combo_.get_active_id();
        if (sort_id == "name_desc") {
            std::sort(visible_results_.begin(), visible_results_.end(), [](const PackageEntry& a, const PackageEntry& b) {
                return a.name > b.name;
            });
        } else if (sort_id == "source") {
            std::sort(visible_results_.begin(), visible_results_.end(), [](const PackageEntry& a, const PackageEntry& b) {
                if (a.source == b.source) {
                    return a.name < b.name;
                }
                return a.source < b.source;
            });
        } else if (sort_id == "installed") {
            std::sort(visible_results_.begin(), visible_results_.end(), [](const PackageEntry& a, const PackageEntry& b) {
                if (a.installed == b.installed) {
                    return a.name < b.name;
                }
                return a.installed > b.installed;
            });
        } else {
            std::sort(visible_results_.begin(), visible_results_.end(), [](const PackageEntry& a, const PackageEntry& b) {
                return a.name < b.name;
            });
        }
    }

    PackageEntry* find_entry(const std::string& id) {
        for (auto& entry : search_results_) {
            if (entry.id == id) {
                return &entry;
            }
        }
        return nullptr;
    }

    void show_details(const std::string& id) {
        auto* entry = find_entry(id);
        if (!entry) {
            return;
        }

        details_header_.set_text(entry->name);

        std::ostringstream body;
        body << "Źródło: " << entry->source << "\n"
             << "ID/Nazwa: " << entry->id << "\n"
             << "Wersja: " << entry->version << "\n"
             << "Status: " << (entry->installed ? "zainstalowany" : "niezainstalowany") << "\n\n"
             << "Opis:\n" << entry->description;
        details_body_.set_text(body.str());
    }

    void on_row_selected(Gtk::ListBoxRow* row) {
        if (!row) {
            return;
        }

        auto* package_row = dynamic_cast<PackageRow*>(row);
        if (!package_row) {
            return;
        }

        show_details(package_row->id());
    }

    void refresh_metadata() {
        if (async_busy_) {
            return;
        }

        std::vector<std::string> commands;
        if (has_pacman_) {
            commands.emplace_back("sudo -n pacman -Sy");
        }
        if (!aur_helper_.empty()) {
            commands.emplace_back(aur_helper_ + " -Sy");
        }
        if (has_flatpak_) {
            commands.emplace_back("flatpak update --appstream -y");
        }

        if (commands.empty()) {
            append_log("Nie wykryto backendów pakietów.");
            return;
        }

        execute_action_async("Odświeżanie metadanych", join_commands(commands));
    }

    void toggle_install(const std::string& id) {
        if (async_busy_) {
            return;
        }

        auto* entry = find_entry(id);
        if (!entry) {
            return;
        }

        const auto action_verb = std::string{entry->installed ? "Usunąć" : "Zainstalować"};
        const auto confirm_body = action_verb + " pakiet \"" + entry->name + "\" (" + entry->source + ")?";
        if (!show_confirm_dialog(entry->installed ? "Usuwanie pakietu" : "Instalacja pakietu", confirm_body)) {
            return;
        }

        std::string command;
        std::string action_name;
        if (!entry->installed) {
            action_name = "Instalacja";
            if (entry->source == "flatpak") {
                command = "flatpak install -y flathub " + shell_escape(entry->id);
            } else if (entry->source == "aur") {
                command = aur_helper_ + " -S --noconfirm " + shell_escape(entry->id);
            } else {
                command = "sudo -n pacman -S --noconfirm " + shell_escape(entry->id);
            }
        } else {
            action_name = "Usuwanie";
            if (entry->source == "flatpak") {
                command = "flatpak uninstall -y " + shell_escape(entry->id);
            } else if (entry->source == "aur") {
                command = aur_helper_ + " -Rns --noconfirm " + shell_escape(entry->id);
            } else {
                command = "sudo -n pacman -Rns --noconfirm " + shell_escape(entry->id);
            }
        }

        auto entry_id = entry->id;
        auto entry_source = entry->source;
        auto was_installed = entry->installed;

        execute_action_async(action_name + " pakietu " + entry_id, command,
            [this, entry_id, entry_source, was_installed](const CommandResult& result) {
                if (result.exit_code == 0) {
                    auto* e = find_entry(entry_id);
                    if (e) {
                        e->installed = !was_installed;
                    }
                    if (entry_source == "flatpak") {
                        if (!was_installed) {
                            installed_flatpak_.insert(entry_id);
                        } else {
                            installed_flatpak_.erase(entry_id);
                        }
                    } else {
                        if (!was_installed) {
                            installed_pacman_.insert(entry_id);
                        } else {
                            installed_pacman_.erase(entry_id);
                        }
                    }
                    rebuild_results_list();
                    show_details(entry_id);
                }
            }
        );
    }

    bool needs_sudo_for_command(const std::string& cmd) const {
        if (cmd.find("sudo -n ") != std::string::npos) {
            return true;
        }
        if (!aur_helper_.empty()) {
            if (cmd.find(aur_helper_ + " -S") != std::string::npos
                || cmd.find(aur_helper_ + " -Rns") != std::string::npos
                || cmd.find(aur_helper_ + " -Sy") != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    bool show_confirm_dialog(const std::string& title, const std::string& body) {
        Gtk::Window dialog;
        dialog.set_modal(true);
        dialog.set_transient_for(*this);
        dialog.set_hide_on_close(true);
        dialog.set_decorated(false);
        dialog.set_resizable(false);
        dialog.set_default_size(460, -1);
        dialog.set_title(title);
        dialog.add_css_class("update-auth-modal");

        auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
        content->add_css_class("update-auth-modal-surface");
        dialog.set_child(*content);

        auto title_label = Gtk::Label(title);
        title_label.set_xalign(0.0f);
        title_label.add_css_class("update-auth-modal-label");
        content->append(title_label);

        auto body_label = Gtk::Label(body);
        body_label.set_wrap(true);
        body_label.set_xalign(0.0f);
        body_label.add_css_class("update-auth-modal-subtitle");
        content->append(body_label);

        auto* btn_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        btn_box->set_homogeneous(true);
        btn_box->set_margin_top(8);

        auto* cancel_btn = Gtk::make_managed<Gtk::Button>("Anuluj");
        cancel_btn->add_css_class("hyalo-button");
        cancel_btn->add_css_class("hyalo-button-secondary");
        cancel_btn->add_css_class("update-auth-modal-action");
        cancel_btn->add_css_class("update-auth-modal-cancel");

        auto* confirm_btn = Gtk::make_managed<Gtk::Button>("Potwierdź");
        confirm_btn->add_css_class("hyalo-button");
        confirm_btn->add_css_class("hyalo-button-primary");
        confirm_btn->add_css_class("update-auth-modal-action");
        confirm_btn->add_css_class("update-auth-modal-primary");

        btn_box->append(*cancel_btn);
        btn_box->append(*confirm_btn);
        content->append(*btn_box);

        auto loop = Glib::MainLoop::create();
        bool confirmed = false;

        confirm_btn->signal_clicked().connect([&]() {
            confirmed = true;
            dialog.close();
        });
        cancel_btn->signal_clicked().connect([&]() { dialog.close(); });
        dialog.signal_hide().connect([&]() {
            if (loop->is_running()) {
                loop->quit();
            }
        });
        dialog.present();
        loop->run();
        return confirmed;
    }

    void execute_action_async(const std::string& title, const std::string& command,
                              std::function<void(CommandResult)> on_done = {}) {
        if (async_busy_) {
            append_log("[WARN] Inna operacja jest w trakcie.");
            return;
        }

        bool use_sudo_pipe = false;
        if (needs_sudo_for_command(command)) {
            if (!has_active_sudo_session() && sudo_password_.empty()) {
                append_log("Wymagane potwierdzenie hasła sudo.");
                if (!ensure_sudo_session()) {
                    return;
                }
            }
            use_sudo_pipe = !sudo_password_.empty();
        }

        append_log("\n== " + title + " ==");
        append_log("$ " + command);
        set_actions_sensitive(false);
        async_busy_ = true;
        show_status(title + "...");

        auto actual_command = use_sudo_pipe
            ? inject_sudo_password_pipe(command, sudo_password_)
            : command;

        std::thread([this, actual_command, title, on_done = std::move(on_done)]() {
            auto result = run_command_with_status(actual_command);
            schedule_on_main([this, result = std::move(result), title, on_done = std::move(on_done)]() {
                async_busy_ = false;
                set_actions_sensitive(true);
                hide_status();
                detect_backends();

                if (!result.output.empty()) {
                    append_log(result.output);
                }
                if (result.exit_code == 0) {
                    append_log("[OK] " + title + " — zakończono pomyślnie.");
                } else {
                    append_log("[BŁĄD] " + title + " — kod wyjścia: " + std::to_string(result.exit_code));
                }
                if (on_done) {
                    on_done(result);
                }
            });
        }).detach();
    }

    void append_log(const std::string& line) {
        auto buffer = log_view_.get_buffer();
        auto current = buffer->get_text();
        if (!current.empty()) {
            current += "\n";
        }
        current += line;
        buffer->set_text(current);
    }

    void set_actions_sensitive(bool enabled) {
        search_button_.set_sensitive(enabled);
        refresh_button_.set_sensitive(enabled);
        sample_az_button_.set_sensitive(enabled);
        source_combo_.set_sensitive(enabled);
        installed_only_toggle_.set_sensitive(enabled);
        sort_combo_.set_sensitive(enabled);
        power_saver_toggle_.set_sensitive(enabled);
        chip_all_button_.set_sensitive(enabled);
        chip_pacman_button_.set_sensitive(enabled);
        chip_aur_button_.set_sensitive(enabled);
        chip_flatpak_button_.set_sensitive(enabled);
        search_entry_.set_sensitive(enabled);
    }

    std::string join_commands(const std::vector<std::string>& commands) {
        std::ostringstream joined;
        for (std::size_t index = 0; index < commands.size(); ++index) {
            joined << commands[index];
            if (index + 1 < commands.size()) {
                joined << " && ";
            }
        }
        return joined.str();
    }

    Gtk::Box root_;
    Gtk::Label title_;
    Gtk::Label backend_status_;
    Gtk::Box header_row_;
    Gtk::Revealer status_revealer_;
    Gtk::Box status_box_;
    Gtk::Spinner status_spinner_;
    Gtk::Label status_message_;
    Gtk::Box toolbar_row_;
    Gtk::Box filter_row_;
    Gtk::Box chips_row_;
    Gtk::Box content_row_;
    Gtk::Box results_panel_;
    Gtk::Box pagination_row_;
    Gtk::ComboBoxText source_combo_;
    Gtk::CheckButton installed_only_toggle_;
    Gtk::CheckButton power_saver_toggle_;
    Gtk::ComboBoxText sort_combo_;
    Gtk::SearchEntry search_entry_;
    Gtk::Button search_button_;
    Gtk::Button sample_az_button_;
    Gtk::Button refresh_button_;
    Gtk::Button chip_all_button_;
    Gtk::Button chip_pacman_button_;
    Gtk::Button chip_aur_button_;
    Gtk::Button chip_flatpak_button_;
    Gtk::Button prev_page_button_;
    Gtk::Button next_page_button_;
    Gtk::Label page_label_;

    Gtk::ScrolledWindow results_scroll_;
    Gtk::ListBox results_list_;
    Gtk::Label results_header_;
    Gtk::Label results_meta_;

    Gtk::Box details_box_;
    Gtk::Label details_header_;
    Gtk::Label details_body_;

    Gtk::ScrolledWindow log_scroll_;
    Gtk::TextView log_view_;

    bool has_pacman_ = false;
    bool has_flatpak_ = false;
    std::string aur_helper_;
    std::vector<PackageEntry> search_results_;
    std::vector<PackageEntry> visible_results_;
    std::size_t current_page_ = 0;
    std::string active_source_filter_ = "all";

    bool installed_cache_ready_ = false;
    std::chrono::steady_clock::time_point installed_cache_updated_at_{};
    std::unordered_set<std::string> installed_pacman_;
    std::unordered_set<std::string> installed_flatpak_;
    bool last_search_truncated_ = false;
    bool async_busy_ = false;
    std::string sudo_password_;

    sigc::connection search_debounce_connection_;
};

}  // namespace

int main(int argc, char** argv) {
    configure_stable_gsk_renderer();

    auto app = Gtk::Application::create("org.hyalo.SoftwareStore");

    auto config_manager = hyalo::core::ConfigManager(hyalo::core::detect_runtime_paths());
    if (!config_manager.load()) {
        config_manager.load_defaults();
    }
    hyalo::core::StyleManager::apply(config_manager);

    return app->make_window_and_run<SoftwareStoreWindow>(argc, argv);
}
