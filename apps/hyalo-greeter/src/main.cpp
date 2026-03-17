/*
 * Hyalo Greeter — LightDM greeter for HyaloOS
 *
 * GTK4 login screen with user selection, password entry,
 * session picker, and power controls.
 */

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>

#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <lightdm.h>

// ======================== CSS loading ========================

static void load_css() {
    auto* provider = gtk_css_provider_new();
    // Try installed path first, then source tree
    const char* paths[] = {
        HYALO_GREETER_CSS_DIR "/hyalo-greeter.css",
        HYALO_SOURCE_GREETER_CSS_DIR "/hyalo-greeter.css",
        nullptr
    };
    for (auto** p = paths; *p; ++p) {
        if (g_file_test(*p, G_FILE_TEST_EXISTS)) {
            gtk_css_provider_load_from_path(provider, *p);
            gtk_style_context_add_provider_for_display(
                gdk_display_get_default(),
                GTK_STYLE_PROVIDER(provider),
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            return;
        }
    }
    g_warning("hyalo-greeter: CSS file not found");
}

// ======================== Greeter state ========================

struct GreeterApp {
    LightDMGreeter* greeter = nullptr;

    // Main layout
    GtkWidget* window       = nullptr;
    GtkWidget* overlay      = nullptr;
    GtkWidget* bg_picture   = nullptr;
    GtkWidget* center_box   = nullptr;

    // Clock
    GtkWidget* clock_label  = nullptr;
    GtkWidget* date_label   = nullptr;

    // User area
    GtkWidget* user_box     = nullptr;
    GtkWidget* avatar_image = nullptr;
    GtkWidget* username_label = nullptr;

    // Auth area
    GtkWidget* auth_box     = nullptr;
    GtkWidget* password_entry = nullptr;
    GtkWidget* message_label = nullptr;
    GtkWidget* login_btn    = nullptr;

    // Session selector
    GtkWidget* session_combo = nullptr;

    // Power bar
    GtkWidget* power_box    = nullptr;

    // User list (for multi-user)
    GtkWidget* user_list_box = nullptr;
    GtkWidget* user_scroll  = nullptr;

    // State
    std::string selected_user;
    std::string selected_session;
    bool authenticating = false;
    guint clock_timer = 0;

    // User list data
    int user_count = 0;
};

static GreeterApp app;

// ======================== Forward declarations ========================

static void select_user(const char* username);
static void attempt_login();
static void update_clock();
static void populate_users();
static void populate_sessions();
static void on_show_prompt(LightDMGreeter* greeter, const gchar* text,
                           LightDMPromptType type, gpointer user_data);
static void on_show_message(LightDMGreeter* greeter, const gchar* text,
                            LightDMMessageType type, gpointer user_data);
static void on_authentication_complete(LightDMGreeter* greeter, gpointer user_data);

// ======================== Clock ========================

static gboolean tick_clock(gpointer) {
    update_clock();
    return G_SOURCE_CONTINUE;
}

static void update_clock() {
    auto now = std::time(nullptr);
    auto* tm = std::localtime(&now);
    if (!tm) return;

    char time_buf[16];
    std::strftime(time_buf, sizeof(time_buf), "%H:%M", tm);
    gtk_label_set_text(GTK_LABEL(app.clock_label), time_buf);

    char date_buf[128];
    // Polish locale date
    std::strftime(date_buf, sizeof(date_buf), "%A, %e %B %Y", tm);
    gtk_label_set_text(GTK_LABEL(app.date_label), date_buf);
}

// ======================== User selection ========================

static void select_user(const char* username) {
    app.selected_user = username;

    // Update display
    auto* user_list = lightdm_user_list_get_instance();
    auto* user = lightdm_user_list_get_user_by_name(user_list, username);

    if (user) {
        const auto* display_name = lightdm_user_get_display_name(user);
        const auto* image = lightdm_user_get_image(user);

        gtk_label_set_text(GTK_LABEL(app.username_label),
                           display_name ? display_name : username);

        if (image && g_file_test(image, G_FILE_TEST_EXISTS)) {
            auto* paintable = gdk_texture_new_from_filename(image, nullptr);
            if (paintable) {
                gtk_image_set_from_paintable(GTK_IMAGE(app.avatar_image),
                                             GDK_PAINTABLE(paintable));
                g_object_unref(paintable);
            } else {
                gtk_image_set_from_icon_name(GTK_IMAGE(app.avatar_image),
                                             "avatar-default-symbolic");
            }
        } else {
            gtk_image_set_from_icon_name(GTK_IMAGE(app.avatar_image),
                                         "avatar-default-symbolic");
        }

        // Set user's preferred session if any
        const auto* user_session = lightdm_user_get_session(user);
        if (user_session) {
            gtk_drop_down_set_selected(GTK_DROP_DOWN(app.session_combo), 0);
            auto* model = gtk_drop_down_get_model(GTK_DROP_DOWN(app.session_combo));
            for (guint i = 0; i < g_list_model_get_n_items(model); i++) {
                auto* item = GTK_STRING_OBJECT(g_list_model_get_item(model, i));
                if (item && g_strcmp0(gtk_string_object_get_string(item), user_session) == 0) {
                    gtk_drop_down_set_selected(GTK_DROP_DOWN(app.session_combo), i);
                    g_object_unref(item);
                    break;
                }
                if (item) g_object_unref(item);
            }
        }
    } else {
        gtk_label_set_text(GTK_LABEL(app.username_label), username);
        gtk_image_set_from_icon_name(GTK_IMAGE(app.avatar_image),
                                     "avatar-default-symbolic");
    }

    // Show auth area, focus password
    gtk_widget_set_visible(app.auth_box, TRUE);
    gtk_editable_set_text(GTK_EDITABLE(app.password_entry), "");
    gtk_label_set_text(GTK_LABEL(app.message_label), "");
    gtk_widget_grab_focus(app.password_entry);

    // Start authentication
    GError* error = nullptr;
    if (app.authenticating) {
        lightdm_greeter_cancel_authentication(app.greeter, &error);
        g_clear_error(&error);
    }
    app.authenticating = true;
    lightdm_greeter_authenticate(app.greeter, username, &error);
    if (error) {
        gtk_label_set_text(GTK_LABEL(app.message_label), error->message);
        g_error_free(error);
    }
}

// ======================== Login attempt ========================

static void attempt_login() {
    const auto* password = gtk_editable_get_text(GTK_EDITABLE(app.password_entry));
    if (!password || password[0] == '\0') return;

    gtk_widget_set_sensitive(app.login_btn, FALSE);
    gtk_widget_set_sensitive(app.password_entry, FALSE);
    gtk_label_set_text(GTK_LABEL(app.message_label), "");

    GError* error = nullptr;
    lightdm_greeter_respond(app.greeter, password, &error);
    if (error) {
        gtk_label_set_text(GTK_LABEL(app.message_label), error->message);
        gtk_widget_set_sensitive(app.login_btn, TRUE);
        gtk_widget_set_sensitive(app.password_entry, TRUE);
        g_error_free(error);
    }
}

// ======================== LightDM callbacks ========================

static void on_show_prompt(LightDMGreeter*, const gchar* text,
                           LightDMPromptType type, gpointer) {
    gtk_widget_set_visible(app.auth_box, TRUE);

    if (type == LIGHTDM_PROMPT_TYPE_SECRET) {
        gtk_entry_set_visibility(GTK_ENTRY(app.password_entry), FALSE);
    } else {
        gtk_entry_set_visibility(GTK_ENTRY(app.password_entry), TRUE);
    }

    gtk_widget_set_sensitive(app.password_entry, TRUE);
    gtk_widget_set_sensitive(app.login_btn, TRUE);
    gtk_widget_grab_focus(app.password_entry);
}

static void on_show_message(LightDMGreeter*, const gchar* text,
                            LightDMMessageType type, gpointer) {
    gtk_label_set_text(GTK_LABEL(app.message_label), text ? text : "");
    if (type == LIGHTDM_MESSAGE_TYPE_ERROR) {
        gtk_widget_add_css_class(app.message_label, "greeter-error");
        gtk_widget_remove_css_class(app.message_label, "greeter-info");
    } else {
        gtk_widget_add_css_class(app.message_label, "greeter-info");
        gtk_widget_remove_css_class(app.message_label, "greeter-error");
    }
}

static void on_authentication_complete(LightDMGreeter* greeter, gpointer) {
    app.authenticating = false;

    if (lightdm_greeter_get_is_authenticated(greeter)) {
        // Get selected session
        const char* session = nullptr;
        auto sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(app.session_combo));
        auto* model = gtk_drop_down_get_model(GTK_DROP_DOWN(app.session_combo));
        if (model) {
            auto* item = GTK_STRING_OBJECT(g_list_model_get_item(model, sel));
            if (item) {
                session = gtk_string_object_get_string(item);
            }
        }

        if (!session || session[0] == '\0') {
            session = lightdm_greeter_get_default_session_hint(greeter);
        }

        GError* error = nullptr;
        lightdm_greeter_start_session_sync(greeter, session, &error);
        if (error) {
            gtk_label_set_text(GTK_LABEL(app.message_label), error->message);
            g_error_free(error);
            gtk_widget_set_sensitive(app.login_btn, TRUE);
            gtk_widget_set_sensitive(app.password_entry, TRUE);
        }
    } else {
        gtk_label_set_text(GTK_LABEL(app.message_label),
                           "Nieprawid\xc5\x82owe has\xc5\x82o");
        gtk_widget_add_css_class(app.message_label, "greeter-error");
        gtk_editable_set_text(GTK_EDITABLE(app.password_entry), "");
        gtk_widget_set_sensitive(app.login_btn, TRUE);
        gtk_widget_set_sensitive(app.password_entry, TRUE);
        gtk_widget_grab_focus(app.password_entry);

        // Re-authenticate
        GError* err = nullptr;
        app.authenticating = true;
        lightdm_greeter_authenticate(app.greeter, app.selected_user.c_str(), &err);
        g_clear_error(&err);
    }
}

// ======================== Power buttons ========================

static void on_power_shutdown(GtkButton*, gpointer) {
    GError* error = nullptr;
    lightdm_shutdown(&error);
    g_clear_error(&error);
}

static void on_power_restart(GtkButton*, gpointer) {
    GError* error = nullptr;
    lightdm_restart(&error);
    g_clear_error(&error);
}

static void on_power_suspend(GtkButton*, gpointer) {
    GError* error = nullptr;
    lightdm_suspend(&error);
    g_clear_error(&error);
}

// ======================== Build UI ========================

static GtkWidget* make_icon_button(const char* icon_name, const char* tooltip,
                                   const char* css_class, GCallback callback) {
    auto* btn = gtk_button_new();
    auto* img = gtk_image_new_from_icon_name(icon_name);
    gtk_image_set_pixel_size(GTK_IMAGE(img), 18);
    gtk_button_set_child(GTK_BUTTON(btn), img);
    gtk_widget_set_tooltip_text(btn, tooltip);
    gtk_widget_add_css_class(btn, "greeter-power-btn");
    if (css_class) gtk_widget_add_css_class(btn, css_class);
    g_signal_connect(btn, "clicked", callback, nullptr);
    return btn;
}

static void populate_users() {
    auto* user_list = lightdm_user_list_get_instance();
    auto* users = lightdm_user_list_get_users(user_list);
    app.user_count = static_cast<int>(g_list_length(users));

    // If only one user, don't show user list — select directly
    if (app.user_count <= 1) {
        gtk_widget_set_visible(app.user_scroll, FALSE);
        if (users) {
            auto* user = LIGHTDM_USER(users->data);
            select_user(lightdm_user_get_name(user));
        }
        return;
    }

    gtk_widget_set_visible(app.user_scroll, TRUE);

    for (auto* l = users; l; l = l->next) {
        auto* user = LIGHTDM_USER(l->data);
        const auto* name = lightdm_user_get_name(user);
        const auto* display_name = lightdm_user_get_display_name(user);
        const auto* image = lightdm_user_get_image(user);

        auto* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_add_css_class(row, "greeter-user-row");
        gtk_widget_set_margin_top(row, 4);
        gtk_widget_set_margin_bottom(row, 4);
        gtk_widget_set_margin_start(row, 12);
        gtk_widget_set_margin_end(row, 12);

        auto* avatar = gtk_image_new();
        gtk_widget_add_css_class(avatar, "greeter-user-row-avatar");
        gtk_image_set_pixel_size(GTK_IMAGE(avatar), 36);

        if (image && g_file_test(image, G_FILE_TEST_EXISTS)) {
            auto* tex = gdk_texture_new_from_filename(image, nullptr);
            if (tex) {
                gtk_image_set_from_paintable(GTK_IMAGE(avatar), GDK_PAINTABLE(tex));
                g_object_unref(tex);
            } else {
                gtk_image_set_from_icon_name(GTK_IMAGE(avatar), "avatar-default-symbolic");
            }
        } else {
            gtk_image_set_from_icon_name(GTK_IMAGE(avatar), "avatar-default-symbolic");
        }

        auto* lbl = gtk_label_new(display_name ? display_name : name);
        gtk_widget_add_css_class(lbl, "greeter-user-row-name");
        gtk_label_set_xalign(GTK_LABEL(lbl), 0);
        gtk_widget_set_hexpand(lbl, TRUE);

        gtk_box_append(GTK_BOX(row), avatar);
        gtk_box_append(GTK_BOX(row), lbl);

        // Wrap in a GtkListBoxRow via GtkButton for click handling
        auto* btn = gtk_button_new();
        gtk_button_set_child(GTK_BUTTON(btn), row);
        gtk_widget_add_css_class(btn, "greeter-user-btn");

        auto* name_copy = g_strdup(name);
        g_signal_connect_data(btn, "clicked",
            G_CALLBACK(+[](GtkButton*, gpointer data) {
                select_user(static_cast<const char*>(data));
            }),
            name_copy, [](gpointer data, GClosure*) { g_free(data); },
            GConnectFlags(0));

        gtk_box_append(GTK_BOX(app.user_list_box), btn);
    }

    // Auto-select first user
    if (users) {
        select_user(lightdm_user_get_name(LIGHTDM_USER(users->data)));
    }
}

static void populate_sessions() {
    auto* sessions = lightdm_get_sessions();
    auto* strings = gtk_string_list_new(nullptr);

    guint hyalo_index = 0;
    guint idx = 0;
    for (auto* l = sessions; l; l = l->next) {
        auto* session = LIGHTDM_SESSION(l->data);
        const auto* key = lightdm_session_get_key(session);
        gtk_string_list_append(strings, key);
        if (key && g_str_has_prefix(key, "hyalo")) {
            hyalo_index = idx;
        }
        idx++;
    }

    gtk_drop_down_set_model(GTK_DROP_DOWN(app.session_combo),
                            G_LIST_MODEL(strings));

    // Create factory that shows session name instead of key
    auto* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(+[](GtkSignalListItemFactory*,
                                                       GtkListItem* item, gpointer) {
        auto* label = gtk_label_new(nullptr);
        gtk_label_set_xalign(GTK_LABEL(label), 0);
        gtk_list_item_set_child(item, label);
    }), nullptr);

    g_signal_connect(factory, "bind", G_CALLBACK(+[](GtkSignalListItemFactory*,
                                                      GtkListItem* item, gpointer) {
        auto* str_obj = GTK_STRING_OBJECT(gtk_list_item_get_item(item));
        const auto* key = gtk_string_object_get_string(str_obj);
        auto* label = GTK_LABEL(gtk_list_item_get_child(item));

        // Find display name from session list
        auto* sessions = lightdm_get_sessions();
        for (auto* l = sessions; l; l = l->next) {
            auto* session = LIGHTDM_SESSION(l->data);
            if (g_strcmp0(lightdm_session_get_key(session), key) == 0) {
                gtk_label_set_text(label, lightdm_session_get_name(session));
                return;
            }
        }
        gtk_label_set_text(label, key);
    }), nullptr);

    gtk_drop_down_set_factory(GTK_DROP_DOWN(app.session_combo), factory);

    // Pre-select HyaloOS session
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app.session_combo), hyalo_index);

    g_object_unref(strings);
    g_object_unref(factory);
}

static void build_ui(GtkApplication* gtk_app) {
    load_css();

    // ---- Window ----
    app.window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(app.window), "Hyalo Greeter");
    gtk_window_set_decorated(GTK_WINDOW(app.window), FALSE);
    gtk_window_fullscreen(GTK_WINDOW(app.window));
    gtk_widget_add_css_class(app.window, "greeter-window");

    // ---- Overlay for background + content ----
    app.overlay = gtk_overlay_new();
    gtk_window_set_child(GTK_WINDOW(app.window), app.overlay);

    // Background with dark fallback
    auto* bg_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(bg_box, "greeter-background");
    gtk_widget_set_hexpand(bg_box, TRUE);
    gtk_widget_set_vexpand(bg_box, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(app.overlay), bg_box);

    // Try to set wallpaper as background
    const char* wallpaper_paths[] = {
        "/usr/share/hyalo/wallpapers/default.jpg",
        "/usr/share/hyalo/wallpapers/default.png",
        "/usr/local/share/hyalo/wallpapers/default.jpg",
        "/usr/local/share/hyalo/wallpapers/default.png",
        nullptr
    };
    for (auto** p = wallpaper_paths; *p; ++p) {
        if (g_file_test(*p, G_FILE_TEST_EXISTS)) {
            auto* file = g_file_new_for_path(*p);
            auto* paintable = gdk_texture_new_from_file(file, nullptr);
            if (paintable) {
                app.bg_picture = gtk_picture_new_for_paintable(GDK_PAINTABLE(paintable));
                gtk_picture_set_content_fit(GTK_PICTURE(app.bg_picture), GTK_CONTENT_FIT_COVER);
                gtk_widget_add_css_class(app.bg_picture, "greeter-wallpaper");
                gtk_widget_set_hexpand(app.bg_picture, TRUE);
                gtk_widget_set_vexpand(app.bg_picture, TRUE);
                gtk_overlay_set_child(GTK_OVERLAY(app.overlay), app.bg_picture);
                g_object_unref(paintable);
            }
            g_object_unref(file);
            break;
        }
    }

    // ---- Main content overlay ----
    auto* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(content, "greeter-content");
    gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(content, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(content, TRUE);
    gtk_widget_set_vexpand(content, TRUE);
    gtk_overlay_add_overlay(GTK_OVERLAY(app.overlay), content);

    // ---- Top: Clock ----
    auto* top_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(top_box, "greeter-clock-area");
    gtk_widget_set_halign(top_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(top_box, GTK_ALIGN_START);
    gtk_widget_set_margin_top(top_box, 64);

    app.clock_label = gtk_label_new("00:00");
    gtk_widget_add_css_class(app.clock_label, "greeter-clock");

    app.date_label = gtk_label_new("");
    gtk_widget_add_css_class(app.date_label, "greeter-date");

    gtk_box_append(GTK_BOX(top_box), app.clock_label);
    gtk_box_append(GTK_BOX(top_box), app.date_label);
    gtk_box_append(GTK_BOX(content), top_box);

    // ---- Center: Login card ----
    app.center_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(app.center_box, "greeter-card");
    gtk_widget_set_halign(app.center_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(app.center_box, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(app.center_box, TRUE);
    gtk_widget_set_size_request(app.center_box, 380, -1);
    gtk_box_append(GTK_BOX(content), app.center_box);

    // User list (multi-user)
    app.user_list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(app.user_list_box, "greeter-user-list");

    app.user_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(app.user_scroll),
                                  app.user_list_box);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(app.user_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(app.user_scroll, -1, 120);
    gtk_widget_set_visible(app.user_scroll, FALSE);
    gtk_box_append(GTK_BOX(app.center_box), app.user_scroll);

    // ---- Avatar + username ----
    app.user_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(app.user_box, "greeter-user-area");
    gtk_widget_set_halign(app.user_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(app.user_box, 24);
    gtk_widget_set_margin_bottom(app.user_box, 8);

    app.avatar_image = gtk_image_new_from_icon_name("avatar-default-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(app.avatar_image), 80);
    gtk_widget_add_css_class(app.avatar_image, "greeter-avatar");

    app.username_label = gtk_label_new("");
    gtk_widget_add_css_class(app.username_label, "greeter-username");

    gtk_box_append(GTK_BOX(app.user_box), app.avatar_image);
    gtk_box_append(GTK_BOX(app.user_box), app.username_label);
    gtk_box_append(GTK_BOX(app.center_box), app.user_box);

    // ---- Auth area ----
    app.auth_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(app.auth_box, "greeter-auth-area");
    gtk_widget_set_halign(app.auth_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(app.auth_box, 4);
    gtk_widget_set_margin_bottom(app.auth_box, 20);
    gtk_widget_set_margin_start(app.auth_box, 32);
    gtk_widget_set_margin_end(app.auth_box, 32);
    gtk_widget_set_visible(app.auth_box, FALSE);

    // Password row
    auto* pw_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(pw_box, "greeter-password-row");

    app.password_entry = gtk_password_entry_new();
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(app.password_entry), TRUE);
    gtk_widget_set_hexpand(app.password_entry, TRUE);
    gtk_widget_add_css_class(app.password_entry, "greeter-password");
    g_signal_connect(app.password_entry, "activate",
                     G_CALLBACK(+[](GtkEntry*, gpointer) { attempt_login(); }), nullptr);

    app.login_btn = gtk_button_new();
    auto* login_icon = gtk_image_new_from_icon_name("go-next-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(login_icon), 18);
    gtk_button_set_child(GTK_BUTTON(app.login_btn), login_icon);
    gtk_widget_add_css_class(app.login_btn, "greeter-login-btn");
    gtk_widget_set_tooltip_text(app.login_btn, "Zaloguj");
    g_signal_connect(app.login_btn, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer) { attempt_login(); }), nullptr);

    gtk_box_append(GTK_BOX(pw_box), app.password_entry);
    gtk_box_append(GTK_BOX(pw_box), app.login_btn);
    gtk_box_append(GTK_BOX(app.auth_box), pw_box);

    // Message label
    app.message_label = gtk_label_new("");
    gtk_widget_add_css_class(app.message_label, "greeter-message");
    gtk_label_set_wrap(GTK_LABEL(app.message_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(app.message_label), 40);
    gtk_widget_set_halign(app.message_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(app.auth_box), app.message_label);

    gtk_box_append(GTK_BOX(app.center_box), app.auth_box);

    // ---- Session selector ----
    auto* session_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(session_box, "greeter-session-area");
    gtk_widget_set_halign(session_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_bottom(session_box, 20);

    auto* session_label = gtk_label_new("Sesja:");
    gtk_widget_add_css_class(session_label, "greeter-session-label");

    app.session_combo = gtk_drop_down_new(nullptr, nullptr);
    gtk_widget_add_css_class(app.session_combo, "greeter-session-combo");
    gtk_widget_set_size_request(app.session_combo, 180, -1);

    gtk_box_append(GTK_BOX(session_box), session_label);
    gtk_box_append(GTK_BOX(session_box), app.session_combo);
    gtk_box_append(GTK_BOX(app.center_box), session_box);

    // ---- Bottom: Power buttons ----
    auto* bottom_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(bottom_box, "greeter-power-area");
    gtk_widget_set_halign(bottom_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(bottom_box, GTK_ALIGN_END);
    gtk_widget_set_margin_bottom(bottom_box, 40);

    if (lightdm_get_can_suspend()) {
        gtk_box_append(GTK_BOX(bottom_box),
            make_icon_button("media-playback-pause-symbolic", "Wstrzymaj",
                             "greeter-power-suspend",
                             G_CALLBACK(on_power_suspend)));
    }
    if (lightdm_get_can_restart()) {
        gtk_box_append(GTK_BOX(bottom_box),
            make_icon_button("system-reboot-symbolic",
                             "Uruchom ponownie", "greeter-power-restart",
                             G_CALLBACK(on_power_restart)));
    }
    if (lightdm_get_can_shutdown()) {
        gtk_box_append(GTK_BOX(bottom_box),
            make_icon_button("system-shutdown-symbolic",
                             "Wy\xc5\x82\xc4\x85\x63z", "greeter-power-shutdown",
                             G_CALLBACK(on_power_shutdown)));
    }

    gtk_box_append(GTK_BOX(content), bottom_box);

    // ---- Populate ----
    populate_sessions();
    update_clock();
    app.clock_timer = g_timeout_add_seconds(15, tick_clock, nullptr);

    // Greeter setup
    app.greeter = lightdm_greeter_new();
    lightdm_greeter_set_resettable(app.greeter, TRUE);

    g_signal_connect(app.greeter, LIGHTDM_GREETER_SIGNAL_SHOW_PROMPT,
                     G_CALLBACK(on_show_prompt), nullptr);
    g_signal_connect(app.greeter, LIGHTDM_GREETER_SIGNAL_SHOW_MESSAGE,
                     G_CALLBACK(on_show_message), nullptr);
    g_signal_connect(app.greeter, LIGHTDM_GREETER_SIGNAL_AUTHENTICATION_COMPLETE,
                     G_CALLBACK(on_authentication_complete), nullptr);

    GError* error = nullptr;
    if (!lightdm_greeter_connect_to_daemon_sync(app.greeter, &error)) {
        g_warning("Failed to connect to LightDM daemon: %s",
                  error ? error->message : "unknown");
        g_clear_error(&error);
    }

    populate_users();

    gtk_window_present(GTK_WINDOW(app.window));
}

// ======================== Main ========================

static void on_activate(GtkApplication* gtk_app, gpointer) {
    build_ui(gtk_app);
}

int main(int argc, char** argv) {
    setlocale(LC_ALL, "");

    auto* gtk_app = gtk_application_new("org.hyalo.Greeter",
                                        G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(on_activate), nullptr);
    int status = g_application_run(G_APPLICATION(gtk_app), argc, argv);
    g_object_unref(gtk_app);
    return status;
}
