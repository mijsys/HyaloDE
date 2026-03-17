#pragma once

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/dragsource.h>
#include <gtkmm/droptarget.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/overlay.h>
#include <gtkmm/picture.h>
#include <gtkmm/popover.h>
#include <gtkmm/scale.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/searchentry.h>
#include <gtkmm/separator.h>
#include <gtkmm/switch.h>
#include <gtkmm/window.h>

#include <gdkmm/texture.h>

#include <giomm/filemonitor.h>
#include <glibmm/dispatcher.h>

#include <memory>
#include <mutex>
#include <optional>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <thread>
#include <vector>

#include "hyalo-core/config_manager.hpp"
#include "hyalo-core/localization.hpp"
#include "hyalo-panel/window_tracker.hpp"

namespace hyalo::panel {

class PanelWindow : public Gtk::Window {
public:
    PanelWindow(core::ConfigManager& config_manager, const core::Localization& localization);
    ~PanelWindow() override;

private:
    struct NetworkAccessPointInfo {
        std::string ssid;
        std::string security;
        int signal_percent = -1;
        bool active = false;
    };

    struct NetworkInfo {
        std::string summary;
        std::string active_ssid;
        std::string active_security;
        int active_signal_percent = -1;
        bool available = false;
        bool wifi_enabled = false;
        bool bluetooth_enabled = false;
    };

    struct AudioInfo {
        std::string summary;
        bool available = false;
        bool muted = false;
        int volume_percent = -1;
    };

    struct AudioDeviceInfo {
        std::string id;
        std::string name;
        bool is_default = false;
    };

    struct AudioSourceInfo {
        std::string id;
        std::string name;
        bool is_default = false;
    };

    struct BluetoothDeviceInfo {
        std::string address;
        std::string name;
        bool paired = false;
        bool connected = false;
        int battery_percent = -1;
    };

    struct BrightnessInfo {
        bool available = false;
        int percentage = -1;
    };

    struct DoNotDisturbInfo {
        bool available = false;
        bool enabled = false;
    };

    struct LauncherEntry {
        std::string desktop_id;
        std::string desktop_file;
        std::string name;
        std::string description;
        std::string executable;
        std::string icon_name;
        std::string category;
        std::string search_blob;
        bool pinned = false;
    };

    struct QuickPanelSnapshot {
        bool detailed = false;
        NetworkInfo network_info;
        std::vector<NetworkAccessPointInfo> network_access_points;
        AudioInfo audio_info;
        std::vector<AudioDeviceInfo> audio_devices;
        std::vector<AudioSourceInfo> audio_sources;
        std::vector<BluetoothDeviceInfo> bluetooth_devices;
        BrightnessInfo brightness_info;
        DoNotDisturbInfo do_not_disturb_info;
    };

    void configure_layer_shell();
    void configure_launcher_window();
    void start_runtime_watchers();
    void schedule_runtime_reload();
    void reload_runtime_config();
    void apply_panel_config();
    void configure_launcher();
    void present_launcher_power_dialog(bool logout_flow);
    void configure_quick_panel();
    void configure_clock_panel();
    void build_launcher_filter_buttons();
    void load_launcher_entries();
    void load_launcher_state();
    void save_launcher_state() const;
    void show_launcher();
    void hide_launcher();
    void toggle_launcher();
    void rebuild_empty_state_apps(const std::vector<WindowSnapshot>& windows);
    void rebuild_minimized_tray(const std::vector<WindowSnapshot>& windows);
    void clear_empty_state_apps();
    void clear_minimized_tray();
    void refresh_launcher_results();
    void update_quick_panel_status(bool refresh_live_state = false);
    void update_clock_panel_status();
    [[nodiscard]] QuickPanelSnapshot build_quick_panel_snapshot(bool detailed) const;
    void request_quick_panel_refresh(bool detailed);
    void on_quick_panel_refresh_ready();
    void apply_quick_panel_snapshot(const QuickPanelSnapshot& snapshot);
    void clear_launcher_context_popovers();
    [[nodiscard]] bool any_launcher_popover_visible() const;
    void clear_task_group_popovers();
    void clear_launcher_pinned_rows();
    void clear_launcher_rows();
    void request_launcher_refresh();
    void render_launcher_batch(std::uint64_t generation);
    void clear_quick_panel_audio_devices();
    void clear_quick_panel_audio_sources();
    void clear_quick_panel_bluetooth_devices();
    void rebuild_quick_panel_networks();
    void rebuild_quick_panel_audio_devices();
    void rebuild_quick_panel_audio_sources();
    void rebuild_quick_panel_bluetooth_devices();
    void set_launcher_filter(const std::string& filter_key);
    void hide_launcher_entry(const LauncherEntry& entry);
    void clear_quick_panel_networks();
    void restore_hidden_launcher_entries();
    [[nodiscard]] bool is_launcher_favorite(const LauncherEntry& entry) const;
    [[nodiscard]] bool matches_launcher_filter(const LauncherEntry& entry) const;
    [[nodiscard]] Gtk::Button* create_launcher_entry_button(const LauncherEntry& entry, bool compact);
    void show_launcher_context_menu(const LauncherEntry& entry, Gtk::Widget& anchor);
    void handle_launcher_activated(const LauncherEntry& entry);
    void handle_settings_activated();
    bool launch_desktop_entry(const LauncherEntry& entry);
    bool launch_command(const std::string& command_line);
    void update_launcher_filter_buttons();
    void toggle_launcher_favorite(const LauncherEntry& entry);
    bool update_clock();
    void update_task_summary(const std::vector<WindowSnapshot>& windows);
    void clear_task_rows();
    void handle_window_primary_action(const std::string& identifier);
    void handle_window_close(const std::string& identifier);
    void present_force_quit_dialog(const std::string& identifier,
                                   const std::string& app_id,
                                   const std::string& title);
    void toggle_wifi();
    void toggle_bluetooth();
    void toggle_do_not_disturb();
    void set_volume(int percentage);
    void connect_bluetooth_device(const std::string& address, bool connected);
    void set_default_audio_device(const AudioDeviceInfo& device);
    void set_default_audio_source(const AudioSourceInfo& source);
    void set_brightness(int percentage);
    [[nodiscard]] std::string resolve_window_icon_name(const WindowSnapshot& window) const;
    [[nodiscard]] std::string resolve_window_group_label(const WindowSnapshot& window) const;

    core::ConfigManager& config_manager_;
    const core::Localization& localization_;
    Glib::RefPtr<Gio::FileMonitor> runtime_directory_monitor_;
    std::unique_ptr<WindowTracker> window_tracker_;
    core::PanelConfig current_panel_config_;
    std::vector<Gtk::Box*> task_rows_;
    std::vector<Gtk::Button*> empty_state_app_buttons_;
    std::vector<Gtk::Button*> minimized_tray_buttons_;
    std::vector<Gtk::Button*> quick_panel_network_buttons_;
    std::vector<Gtk::Button*> quick_panel_audio_device_buttons_;
    std::vector<Gtk::Button*> quick_panel_audio_source_buttons_;
    std::vector<Gtk::Button*> quick_panel_bluetooth_device_buttons_;
    std::vector<WindowSnapshot> last_windows_;
    unsigned int status_refresh_tick_ = 0;
    bool quick_panel_refresh_ready_ = false;
    bool quick_panel_refresh_running_ = false;
    bool queued_quick_panel_refresh_ = false;
    bool queued_quick_panel_refresh_detailed_ = false;
    bool runtime_reload_pending_ = false;
    bool launcher_refresh_suppressed_ = false;
    bool launcher_refresh_pending_ = false;
    bool launcher_refresh_scheduled_ = false;
    bool quick_panel_switch_sync_in_progress_ = false;
    bool brightness_change_in_progress_ = false;
    bool volume_change_in_progress_ = false;
    NetworkInfo network_info_;
    std::vector<NetworkAccessPointInfo> network_access_points_;
    AudioInfo audio_info_;
    std::vector<AudioDeviceInfo> audio_devices_;
    std::vector<AudioSourceInfo> audio_sources_;
    std::vector<BluetoothDeviceInfo> bluetooth_devices_;
    BrightnessInfo brightness_info_;
    DoNotDisturbInfo do_not_disturb_info_;
    std::optional<QuickPanelSnapshot> pending_quick_panel_snapshot_;
    Glib::Dispatcher quick_panel_refresh_dispatcher_;
    std::mutex quick_panel_refresh_mutex_;
    std::jthread quick_panel_refresh_thread_;

    Gtk::Box root_box_{Gtk::Orientation::HORIZONTAL, 12};
    Gtk::Box left_box_{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Box center_box_{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Box right_box_{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Box tasks_box_{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Box empty_state_apps_box_{Gtk::Orientation::HORIZONTAL, 6};
    Gtk::Box minimized_tray_box_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Box launcher_box_{Gtk::Orientation::VERTICAL, 10};
    Gtk::Box launcher_content_box_{Gtk::Orientation::HORIZONTAL, 14};
    Gtk::Box launcher_sidebar_box_{Gtk::Orientation::VERTICAL, 10};
    Gtk::Box launcher_main_box_{Gtk::Orientation::VERTICAL, 10};
    Gtk::Box launcher_header_box_{Gtk::Orientation::VERTICAL, 2};
    Gtk::Box launcher_profile_box_{Gtk::Orientation::HORIZONTAL, 12};
    Gtk::Box launcher_identity_box_{Gtk::Orientation::VERTICAL, 2};
    Gtk::Box launcher_power_box_{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Box launcher_filter_box_{Gtk::Orientation::HORIZONTAL, 6};
    Gtk::Box launcher_pinned_box_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Box launcher_list_box_{Gtk::Orientation::VERTICAL, 6};

    Gtk::Button menu_button_;
    Gtk::Box menu_button_content_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Image menu_button_icon_;
    Gtk::Button settings_button_;
    Gtk::MenuButton clock_button_;
    Gtk::MenuButton quick_panel_button_;
    Gtk::Label tasks_label_;
    Gtk::Label quick_panel_summary_label_;
    Gtk::Label clock_label_;
    Gtk::Popover clock_popover_;
    Gtk::Box clock_panel_box_{Gtk::Orientation::VERTICAL, 10};
    Gtk::Box clock_panel_header_box_{Gtk::Orientation::VERTICAL, 2};
    Gtk::Box clock_panel_time_box_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Box clock_panel_footer_box_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Label clock_panel_title_label_;
    Gtk::Label clock_panel_subtitle_label_;
    Gtk::Label clock_panel_time_label_;
    Gtk::Label clock_panel_date_label_;
    Gtk::Label clock_panel_placeholder_label_;
    Gtk::Popover quick_panel_popover_;
    Gtk::Box quick_panel_box_{Gtk::Orientation::VERTICAL, 10};
    Gtk::Box quick_panel_header_box_{Gtk::Orientation::VERTICAL, 2};
    Gtk::Box quick_panel_status_box_{Gtk::Orientation::VERTICAL, 8};
    Gtk::Box quick_panel_controls_box_{Gtk::Orientation::VERTICAL, 8};
    Gtk::Box quick_panel_wifi_row_{Gtk::Orientation::HORIZONTAL, 10};
    Gtk::Box quick_panel_bluetooth_row_{Gtk::Orientation::HORIZONTAL, 10};
    Gtk::Box quick_panel_dnd_row_{Gtk::Orientation::HORIZONTAL, 10};
    Gtk::Box quick_panel_audio_box_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Box quick_panel_brightness_box_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Box quick_panel_networks_box_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Box quick_panel_audio_devices_box_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Box quick_panel_audio_sources_box_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Box quick_panel_bluetooth_devices_box_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Box quick_panel_actions_box_{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Box quick_panel_power_box_{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Label quick_panel_title_label_;
    Gtk::Label quick_panel_subtitle_label_;
    Gtk::Label quick_panel_network_label_;
    Gtk::Label quick_panel_volume_label_;
    Gtk::Label quick_panel_time_label_;
    Gtk::Label quick_panel_audio_slider_label_;
    Gtk::Label quick_panel_brightness_label_;
    Gtk::Label quick_panel_network_details_label_;
    Gtk::Label quick_panel_networks_title_label_;
    Gtk::Label quick_panel_audio_devices_title_label_;
    Gtk::Label quick_panel_audio_sources_title_label_;
    Gtk::Label quick_panel_bluetooth_devices_title_label_;
    Gtk::Label quick_panel_wifi_row_label_;
    Gtk::Label quick_panel_bluetooth_row_label_;
    Gtk::Label quick_panel_dnd_row_label_;
    Gtk::Scale quick_panel_volume_scale_{Gtk::Orientation::HORIZONTAL};
    Gtk::Scale quick_panel_brightness_scale_{Gtk::Orientation::HORIZONTAL};
    Gtk::Switch quick_panel_wifi_switch_;
    Gtk::Switch quick_panel_bluetooth_switch_;
    Gtk::Switch quick_panel_dnd_switch_;
    Gtk::Button quick_panel_network_button_;
    Gtk::Button quick_panel_audio_button_;
    Gtk::Button quick_panel_settings_button_;
    Gtk::Button quick_panel_logout_button_;
    Gtk::Button quick_panel_suspend_button_;
    Gtk::Button quick_panel_poweroff_button_;
    Gtk::Window launcher_window_;
    Gtk::SearchEntry launcher_search_entry_;
    Gtk::Overlay launcher_scroll_overlay_;
    Gtk::ScrolledWindow launcher_scroll_;
    Gtk::Box launcher_context_menu_{Gtk::Orientation::VERTICAL, 4};
    LauncherEntry launcher_context_entry_{};
    Gtk::Label launcher_avatar_label_;
    Gtk::Label launcher_title_label_;
    Gtk::Label launcher_subtitle_label_;
    Gtk::Label launcher_pinned_label_;
    Gtk::Label launcher_apps_title_label_;
    Gtk::Label launcher_session_value_label_;
    Gtk::Label launcher_windows_value_label_;
    Gtk::Label launcher_time_value_label_;
    Gtk::Label launcher_empty_label_;
    Gtk::Button launcher_logout_button_;
    Gtk::Button launcher_suspend_button_;
    Gtk::Button launcher_poweroff_button_;
    Gtk::Button launcher_settings_button_;
    Gtk::Button launcher_restore_hidden_button_;
    std::vector<LauncherEntry> launcher_entries_;
    std::vector<Gtk::Button*> launcher_filter_buttons_;
    std::vector<Gtk::Button*> launcher_pinned_rows_;
    std::vector<Gtk::Button*> launcher_rows_;

    std::vector<Gtk::Popover*> task_group_popovers_;
    std::vector<const LauncherEntry*> pending_launcher_entries_;
    std::unordered_set<std::string> launcher_favorite_ids_;
    std::unordered_set<std::string> launcher_hidden_ids_;
    std::string active_launcher_filter_ = "all";
    std::size_t launcher_render_index_ = 0;
    std::uint64_t launcher_refresh_generation_ = 0;
    bool launcher_batch_render_scheduled_ = false;
    bool launcher_loaded_ = false;
    bool panel_layer_shell_initialized_ = false;
    bool launcher_layer_shell_initialized_ = false;
    std::size_t launcher_inactive_ticks_ = 0;
    bool diagnostics_enabled_ = false;
    bool last_panel_active_state_ = false;
    bool last_launcher_visible_state_ = false;
    bool last_launcher_active_state_ = false;

    // Hot corners
    std::string hot_corners_mode_ = "off";
    bool hot_corner_armed_ = false;
    sigc::connection hot_corner_timer_;
    void setup_hot_corners();
    void trigger_hot_corner_action();
};

}  // namespace hyalo::panel