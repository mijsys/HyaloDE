#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <glib.h>

#include <glibmm/dispatcher.h>
#include <glibmm/main.h>

#include <gtkmm/application.h>
#include <gtkmm/applicationwindow.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/dialog.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/revealer.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/spinner.h>
#include <gtkmm/textbuffer.h>
#include <gtkmm/textview.h>

#include "hyalo-core/config_manager.hpp"
#include "hyalo-core/localization.hpp"
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

std::string run_command(const std::string& command) {
    std::array<char, 512> buffer{};
    std::string output;

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return {};
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        output += buffer.data();
    }

    pclose(pipe);
    return output;
}

struct CommandResult {
    int exit_code = 1;
    std::string output;
};

CommandResult run_command_with_status(const std::string& command) {
    const std::string marker = "__HYALO_UPDATE_CENTER_EXIT__=";
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

bool verify_password_with_sudo(std::string& password) {
    if (password.empty()) {
        return false;
    }

    FILE* pipe = popen("sudo -S -k -p '' /usr/bin/true 2>/dev/null", "w");
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
    std::string result = command;
    std::string::size_type pos = 0;
    while ((pos = result.find("sudo -n ", pos)) != std::string::npos) {
        result.replace(pos, 8, "sudo -S -p '' ");
        pos += 14;
    }
    return "echo '" + password + "' | " + result;
}

bool ensure_sudo_session(const hyalo::core::Localization& localization, Gtk::Window* parent = nullptr, std::string* out_password = nullptr) {
    if (!command_exists("sudo")) {
        return false;
    }

    if (has_active_sudo_session()) {
        return true;
    }

    const auto tr = [&localization](const std::string& key) {
        return localization.translate(key);
    };

    constexpr int kMaxAttempts = 3;

    Gtk::Window dialog;
    dialog.set_modal(true);
    dialog.set_hide_on_close(true);
    dialog.set_decorated(false);
    dialog.set_resizable(false);
    dialog.set_default_size(460, -1);
    dialog.set_title(tr("uc_auth_title"));
    dialog.add_css_class("update-auth-modal");
    if (parent) {
        dialog.set_transient_for(*parent);
    }

    auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    content->add_css_class("update-auth-modal-surface");
    dialog.set_child(*content);

    auto subtitle = Gtk::Label(tr("uc_auth_subtitle_refresh"));
    subtitle.set_wrap(true);
    subtitle.set_xalign(0.0f);
    subtitle.add_css_class("update-auth-modal-subtitle");

    auto password_label = Gtk::Label(tr("uc_auth_password"));
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

    const auto build_invalid_password_message = [&](int attempts) {
        const auto remaining = std::max(0, kMaxAttempts - attempts);
        std::ostringstream message;
        message << tr("uc_auth_error_invalid") << " " << tr("uc_auth_attempts_left") << " " << remaining << ".";
        return message.str();
    };

    content->append(subtitle);
    content->append(password_label);
    content->append(password_entry);
    content->append(error_label);

    auto* btn_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    btn_box->set_homogeneous(true);
    btn_box->set_margin_top(8);

    auto* cancel_button = Gtk::make_managed<Gtk::Button>(tr("uc_auth_cancel"));
    cancel_button->add_css_class("hyalo-button");
    cancel_button->add_css_class("hyalo-button-secondary");
    cancel_button->add_css_class("update-auth-modal-action");
    cancel_button->add_css_class("update-auth-modal-cancel");

    auto* unlock_button = Gtk::make_managed<Gtk::Button>(tr("uc_auth_unlock"));
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
            if (out_password) {
                *out_password = password;
            }
            password_entry.set_text("");
            error_label.set_text("");
            dialog.close();
            return;
        }

        ++attempts;
        password_entry.set_text("");
        error_label.set_text(build_invalid_password_message(attempts));
        password_entry.grab_focus();

        if (attempts >= kMaxAttempts) {
            error_label.set_text(tr("uc_auth_error_attempts"));
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

    return authorized;
}

bool require_startup_authentication(const hyalo::core::Localization& localization, std::string* out_password = nullptr) {
    const auto tr = [&localization](const std::string& key) {
        return localization.translate(key);
    };

    if (!command_exists("sudo")) {
        g_printerr("Hyalo Update Center: sudo is missing. Access denied.\n");
        return false;
    }

    constexpr int kMaxAttempts = 3;

    Gtk::Window dialog;
    dialog.set_modal(true);
    dialog.set_hide_on_close(true);
    dialog.set_decorated(false);
    dialog.set_resizable(false);
    dialog.set_default_size(460, -1);
    dialog.set_title(tr("uc_auth_title"));
    dialog.add_css_class("update-auth-modal");

    auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    content->add_css_class("update-auth-modal-surface");
    dialog.set_child(*content);

    auto subtitle = Gtk::Label(tr("uc_auth_subtitle"));
    subtitle.set_wrap(true);
    subtitle.set_xalign(0.0f);
    subtitle.add_css_class("update-auth-modal-subtitle");

    auto password_label = Gtk::Label(tr("uc_auth_password"));
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

    const auto build_invalid_password_message = [&](int attempts) {
        const auto remaining = std::max(0, kMaxAttempts - attempts);
        std::ostringstream message;
        message << tr("uc_auth_error_invalid") << " " << tr("uc_auth_attempts_left") << " " << remaining << ".";
        return message.str();
    };

    content->append(subtitle);
    content->append(password_label);
    content->append(password_entry);
    content->append(error_label);

    auto* btn_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    btn_box->set_homogeneous(true);
    btn_box->set_margin_top(8);

    auto* cancel_button = Gtk::make_managed<Gtk::Button>(tr("uc_auth_cancel"));
    cancel_button->add_css_class("hyalo-button");
    cancel_button->add_css_class("hyalo-button-secondary");
    cancel_button->add_css_class("update-auth-modal-action");
    cancel_button->add_css_class("update-auth-modal-cancel");

    auto* unlock_button = Gtk::make_managed<Gtk::Button>(tr("uc_auth_unlock"));
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
        if (verify_password_with_sudo(password)) {
            authorized = true;
            if (out_password) {
                *out_password = password;
            }
            password_entry.set_text("");
            error_label.set_text("");
            dialog.close();
            return;
        }

        ++attempts;
        password_entry.set_text("");
        error_label.set_text(build_invalid_password_message(attempts));
        password_entry.grab_focus();

        if (attempts >= kMaxAttempts) {
            error_label.set_text(tr("uc_auth_error_attempts"));
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
        g_printerr("Hyalo Update Center: authentication denied or canceled.\n");
    }

    return authorized;
}

class UpdateCenterWindow final : public Gtk::ApplicationWindow {
public:
    explicit UpdateCenterWindow(const hyalo::core::Localization& localization, const std::string& sudo_password = {})
        : localization_(localization),
          sudo_password_(sudo_password),
          root_(Gtk::Orientation::VERTICAL, 12),
          notification_box_(Gtk::Orientation::HORIZONTAL, 8),
          actions_(Gtk::Orientation::HORIZONTAL, 8),
          backend_status_(""),
          refresh_button_(tr("uc_btn_refresh")),
          update_all_button_(tr("uc_btn_update_all")),
          update_system_button_(tr("uc_btn_update_system")),
          update_flatpak_button_(tr("uc_btn_update_flatpak")) {
        set_title(tr("uc_title"));
        add_css_class("hyalo-update-center");
        set_default_size(920, 620);

        root_.set_margin_top(16);
        root_.set_margin_bottom(16);
        root_.set_margin_start(16);
        root_.set_margin_end(16);

        header_.set_markup("<b>" + tr("uc_header") + "</b>");
        header_.set_halign(Gtk::Align::START);
        backend_status_.set_halign(Gtk::Align::START);

        notification_revealer_.set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
        notification_revealer_.set_transition_duration(240);
        notification_revealer_.set_reveal_child(false);
        notification_revealer_.set_hexpand(true);
        notification_revealer_.add_css_class("uc-notification-revealer");

        notification_box_.add_css_class("uc-notification");
        notification_spinner_.set_size_request(18, 18);
        notification_message_.set_wrap(true);
        notification_message_.set_xalign(0.0f);
        notification_message_.set_hexpand(true);
        notification_box_.append(notification_spinner_);
        notification_box_.append(notification_message_);
        notification_revealer_.set_child(notification_box_);

        refresh_button_.add_css_class("hyalo-button");
        refresh_button_.add_css_class("hyalo-button-secondary");
        update_system_button_.add_css_class("hyalo-button");
        update_system_button_.add_css_class("hyalo-button-primary");
        update_flatpak_button_.add_css_class("hyalo-button");
        update_flatpak_button_.add_css_class("hyalo-button-secondary");
        update_all_button_.add_css_class("hyalo-button");
        update_all_button_.add_css_class("hyalo-button-primary");

        actions_.append(refresh_button_);
        actions_.append(update_system_button_);
        actions_.append(update_flatpak_button_);
        actions_.append(update_all_button_);

        log_view_.set_editable(false);
        log_view_.set_monospace(true);
        log_scroll_.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
        log_scroll_.set_expand(true);
        log_scroll_.set_child(log_view_);

        root_.append(header_);
        root_.append(backend_status_);
        root_.append(notification_revealer_);
        root_.append(actions_);
        root_.append(log_scroll_);

        set_child(root_);

        refresh_button_.signal_clicked().connect(sigc::mem_fun(*this, &UpdateCenterWindow::refresh_updates));
        update_system_button_.signal_clicked().connect(sigc::mem_fun(*this, &UpdateCenterWindow::run_system_update));
        update_flatpak_button_.signal_clicked().connect(sigc::mem_fun(*this, &UpdateCenterWindow::run_flatpak_update));
        update_all_button_.signal_clicked().connect(sigc::mem_fun(*this, &UpdateCenterWindow::run_full_update));
        command_finished_dispatcher_.connect(sigc::mem_fun(*this, &UpdateCenterWindow::on_command_finished));

        detect_backends();
        refresh_updates();
    }

    ~UpdateCenterWindow() override {
        if (command_thread_.joinable()) {
            command_thread_.join();
        }
    }

private:
    std::string tr(const std::string& key) const {
        return localization_.translate(key);
    }

    void detect_backends() {
        has_pacman_ = command_exists("pacman");
        has_flatpak_ = command_exists("flatpak");

        if (command_exists("paru")) {
            aur_helper_ = "paru";
        } else if (command_exists("yay")) {
            aur_helper_ = "yay";
        }

        const auto yes = tr("uc_word_yes");
        const auto no = tr("uc_word_no");
        const auto missing = tr("uc_word_missing");

        std::ostringstream status;
        status << "Pacman: " << (has_pacman_ ? yes : no)
               << " | AUR: " << (aur_helper_.empty() ? missing : aur_helper_)
               << " | Flatpak: " << (has_flatpak_ ? yes : no);

        backend_status_.set_text(status.str());
        update_system_button_.set_sensitive(has_pacman_ || !aur_helper_.empty());
        update_flatpak_button_.set_sensitive(has_flatpak_);
        update_all_button_.set_sensitive(has_pacman_ || has_flatpak_ || !aur_helper_.empty());
    }

    void refresh_updates() {
        if (command_running_ || refreshing_) {
            return;
        }

        refreshing_ = true;
        set_actions_sensitive(false);
        set_notification("running", tr("uc_scan_header"), true);

        const auto has_pacman = has_pacman_;
        const auto has_flatpak = has_flatpak_;
        const auto aur_helper = aur_helper_;

        const auto scan_header = tr("uc_scan_header");
        const auto section_pacman = tr("uc_section_pacman");
        const auto section_aur = tr("uc_section_aur");
        const auto section_flatpak = tr("uc_section_flatpak");
        const auto no_updates = tr("uc_no_updates");
        const auto missing_checkupdates = tr("uc_missing_checkupdates");
        const auto missing_backend = tr("uc_missing_backend");
        const auto missing_aur = tr("uc_missing_aur");

        std::thread([this, has_pacman, has_flatpak, aur_helper,
                     scan_header, section_pacman, section_aur, section_flatpak,
                     no_updates, missing_checkupdates, missing_backend, missing_aur]() {
            std::ostringstream report;
            report << scan_header << "\n\n";

            if (has_pacman) {
                report << "== " << section_pacman << " ==\n";
                if (command_exists("checkupdates")) {
                    const auto result = run_command("checkupdates 2>/dev/null | head -n 200");
                    report << (result.empty() ? no_updates + "\n" : result) << "\n";
                } else {
                    const auto result = run_command("pacman -Qu 2>/dev/null | head -n 200");
                    report << (result.empty() ? no_updates + "\n" : result) << "\n";
                }
            } else {
                report << "== " << section_pacman << " ==\n" << missing_backend << "\n\n";
            }

            report << "== " << section_aur << " ==\n";
            if (!aur_helper.empty()) {
                const auto result = run_command(aur_helper + " -Qua 2>/dev/null | head -n 200");
                report << (result.empty() ? no_updates + "\n" : result) << "\n";
            } else {
                report << missing_aur << "\n\n";
            }

            report << "== " << section_flatpak << " ==\n";
            if (has_flatpak) {
                auto result = run_command("flatpak remote-ls --updates 2>/dev/null | head -n 200");
                if (result.empty()) {
                    result = no_updates + "\n";
                }
                report << result;
            } else {
                report << missing_backend << "\n";
            }

            auto text = report.str();
            schedule_on_main([this, text = std::move(text)]() {
                log_view_.get_buffer()->set_text(text);
                refreshing_ = false;
                set_actions_sensitive(true);
                notification_revealer_.set_reveal_child(false);
            });
        }).detach();
    }

    void run_system_update() {
        std::vector<std::string> commands;
        if (has_pacman_) {
            commands.emplace_back("sudo -n pacman -Syu --noconfirm");
        }
        if (!aur_helper_.empty()) {
            commands.emplace_back(aur_helper_ + " -Syu --devel --noconfirm");
        }

        if (commands.empty()) {
            append_status(tr("uc_system_backend_missing"));
            return;
        }

        launch_or_report(tr("uc_btn_update_system"), join_commands(commands));
    }

    void run_flatpak_update() {
        if (!has_flatpak_) {
            append_status(tr("uc_flatpak_missing"));
            return;
        }

        launch_or_report(tr("uc_btn_update_flatpak"), "flatpak update -y");
    }

    void run_full_update() {
        std::vector<std::string> commands;
        if (has_pacman_) {
            commands.emplace_back("sudo -n pacman -Syu --noconfirm");
        }
        if (!aur_helper_.empty()) {
            commands.emplace_back(aur_helper_ + " -Syu --devel --noconfirm");
        }
        if (has_flatpak_) {
            commands.emplace_back("flatpak update -y");
        }

        if (commands.empty()) {
            append_status(tr("uc_no_backends"));
            return;
        }

        launch_or_report(tr("uc_btn_update_all"), join_commands(commands));
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

    void launch_or_report(const std::string& title, const std::string& command) {
        if (command_running_) {
            set_notification("running", tr("uc_notify_busy"), true);
            append_status(tr("uc_notify_busy"));
            return;
        }

        bool needs_sudo = command.find("sudo -n ") != std::string::npos;
        if (!needs_sudo && !aur_helper_.empty()) {
            needs_sudo = command.find(aur_helper_ + " -S") != std::string::npos
                      || command.find(aur_helper_ + " -R") != std::string::npos;
        }
        if (needs_sudo && !has_active_sudo_session()) {
            append_status(tr("uc_auth_sudo_refresh_needed"));
            if (!ensure_sudo_session(localization_, this, &sudo_password_)) {
                set_notification("error", tr("uc_auth_sudo_refresh_canceled"), false);
                append_status(tr("uc_auth_sudo_refresh_canceled"));
                return;
            }
        }

        std::string actual_command = command;
        if (needs_sudo && !sudo_password_.empty()) {
            actual_command = inject_sudo_password_pipe(command, sudo_password_);
        }

        append_status("\n== " + title + " ==");
        append_status("$ " + command);

        set_actions_sensitive(false);
        set_notification("running", tr("uc_notify_running") + " " + title, true);
        command_running_ = true;

        command_thread_ = std::thread([this, title, actual_command]() {
            const auto result = run_command_with_status(actual_command);
            {
                std::lock_guard<std::mutex> lock(command_result_mutex_);
                pending_result_ = result;
                pending_title_ = title;
            }
            command_finished_dispatcher_.emit();
        });
    }

    void on_command_finished() {
        if (command_thread_.joinable()) {
            command_thread_.join();
        }

        CommandResult result;
        std::string title;
        {
            std::lock_guard<std::mutex> lock(command_result_mutex_);
            result = pending_result_;
            title = pending_title_;
        }

        command_running_ = false;
        set_actions_sensitive(true);

        if (!result.output.empty()) {
            append_status(result.output);
        }

        if (result.exit_code == 0) {
            set_notification("success", tr("uc_notify_success") + " " + title, false);
            append_status(tr("uc_embedded_done"));
            return;
        }

        set_notification("error", tr("uc_notify_error") + " " + title, false);
        append_status(tr("uc_embedded_failed") + " " + std::to_string(result.exit_code));
    }

    void append_status(const std::string& line) {
        auto buffer = log_view_.get_buffer();
        auto current = buffer->get_text();
        current += "\n" + line;
        buffer->set_text(current);
    }

    void set_actions_sensitive(bool enabled) {
        refresh_button_.set_sensitive(enabled);
        update_system_button_.set_sensitive(enabled && (has_pacman_ || !aur_helper_.empty()));
        update_flatpak_button_.set_sensitive(enabled && has_flatpak_);
        update_all_button_.set_sensitive(enabled && (has_pacman_ || has_flatpak_ || !aur_helper_.empty()));
    }

    void set_notification(const std::string& state, const std::string& message, bool busy) {
        notification_box_.remove_css_class("uc-notification-running");
        notification_box_.remove_css_class("uc-notification-success");
        notification_box_.remove_css_class("uc-notification-error");

        if (state == "running") {
            notification_box_.add_css_class("uc-notification-running");
        } else if (state == "success") {
            notification_box_.add_css_class("uc-notification-success");
        } else if (state == "error") {
            notification_box_.add_css_class("uc-notification-error");
        }

        notification_message_.set_text(message);
        if (busy) {
            notification_spinner_.start();
            notification_spinner_.set_visible(true);
        } else {
            notification_spinner_.stop();
            notification_spinner_.set_visible(false);
        }

        notification_revealer_.set_reveal_child(true);
    }

    const hyalo::core::Localization& localization_;
    Gtk::Box root_;
    Gtk::Label header_;
    Gtk::Label backend_status_;
    Gtk::Revealer notification_revealer_;
    Gtk::Box notification_box_;
    Gtk::Spinner notification_spinner_;
    Gtk::Label notification_message_;
    Gtk::Box actions_;
    Gtk::Button refresh_button_;
    Gtk::Button update_all_button_;
    Gtk::Button update_system_button_;
    Gtk::Button update_flatpak_button_;
    Gtk::ScrolledWindow log_scroll_;
    Gtk::TextView log_view_;

    bool has_pacman_ = false;
    bool has_flatpak_ = false;
    std::string aur_helper_;
    std::string sudo_password_;
    bool command_running_ = false;
    bool refreshing_ = false;

    Glib::Dispatcher command_finished_dispatcher_;
    std::thread command_thread_;
    std::mutex command_result_mutex_;
    CommandResult pending_result_;
    std::string pending_title_;
};

}  // namespace

int main(int argc, char** argv) {
    configure_stable_gsk_renderer();

    auto app = Gtk::Application::create("org.hyalo.UpdateCenter");

    auto config_manager = hyalo::core::ConfigManager(hyalo::core::detect_runtime_paths());
    if (!config_manager.load()) {
        config_manager.load_defaults();
    }

    auto localization = hyalo::core::Localization(config_manager);
    localization.load();

    hyalo::core::StyleManager::apply(config_manager);

    std::string sudo_password;
    if (!require_startup_authentication(localization, &sudo_password)) {
        return 1;
    }

    return app->make_window_and_run<UpdateCenterWindow>(argc, argv, std::cref(localization), sudo_password);
}
