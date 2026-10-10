/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "config.h"
#include "nautilus-terminal.h"

#include <adwaita.h>
#include <glib/gi18n.h>
#include <ptyxis-embed.h>
#include <ptyxis-find-bar.h>
#include <ptyxis-preferences-window.h>
#include <ptyxis-profile-dialog.h>
#include <ptyxis-tab.h>
#include <ptyxis-title-dialog.h>

struct _NautilusTerminal
{
    GtkBox parent_instance;

    GFile *location;
    GFile *session_location;
    PtyxisApplication *application;
    AdwTabView *tabs;
    GtkStack *stack;
    GtkWidget *unavailable;
    GtkWidget *new_session_button;
    GtkDropDown *profiles;
    GtkDropDown *containers;
    GtkRevealer *search_revealer;
    PtyxisFindBar *search;
    GtkLabel *status;
    GSimpleActionGroup *terminal_actions;
};

G_DEFINE_FINAL_TYPE (NautilusTerminal, nautilus_terminal, GTK_TYPE_BOX)

static PtyxisTab *
get_selected_tab (NautilusTerminal *self)
{
    AdwTabPage *page = self->tabs == NULL ? NULL : adw_tab_view_get_selected_page (self->tabs);

    return page == NULL ? NULL : PTYXIS_TAB (adw_tab_page_get_child (page));
}

static void
report_error (NautilusTerminal *self,
              const GError    *error)
{
    g_autofree char *message = g_strdup_printf (_("Could not start Ptyxis: %s"), error->message);

    g_warning ("Could not start inline Ptyxis: %s", error->message);
    gtk_label_set_text (self->status, message);
    gtk_widget_set_visible (GTK_WIDGET (self->status), TRUE);
}

static void
agent_failed (PtyxisApplication *application,
              const GError      *error,
              NautilusTerminal  *self)
{
    report_error (self, error);
    gtk_widget_set_sensitive (self->new_session_button, FALSE);
    self->application = NULL;
}

static void
selected_page_changed (NautilusTerminal *self)
{
    PtyxisTab *tab = get_selected_tab (self);

    ptyxis_find_bar_set_terminal (self->search, tab == NULL ? NULL : ptyxis_tab_get_terminal (tab));
    g_action_map_remove_action (G_ACTION_MAP (self->terminal_actions), "tab.read-only");
    if (tab != NULL)
    {
        g_autoptr (GPropertyAction) read_only = g_property_action_new ("tab.read-only", tab, "read-only");

        g_action_map_add_action (G_ACTION_MAP (self->terminal_actions), G_ACTION (read_only));
    }
}

static void
profile_changed (NautilusTerminal *self)
{
    PtyxisProfile *profile = gtk_drop_down_get_selected_item (self->profiles);
    GListModel *containers = gtk_drop_down_get_model (self->containers);
    g_autofree char *container_id = profile == NULL ? NULL :
        ptyxis_profile_dup_default_container (profile);

    gtk_drop_down_set_selected (self->containers, GTK_INVALID_LIST_POSITION);
    for (guint i = 0; i < g_list_model_get_n_items (containers); i++)
    {
        g_autoptr (PtyxisIpcContainer) container = g_list_model_get_item (containers, i);

        if (g_strcmp0 (ptyxis_ipc_container_get_id (container), container_id) == 0)
        {
            gtk_drop_down_set_selected (self->containers, i);
            break;
        }
    }
}

static void
start_session (NautilusTerminal *self)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (GFileInfo) info = NULL;
    g_autoptr (PtyxisProfile) profile = NULL;
    g_autofree char *uri = NULL;
    PtyxisIpcContainer *container;
    PtyxisTab *tab;
    AdwTabPage *page;

    if (self->application == NULL || self->location == NULL ||
        !g_file_is_native (self->location))
    {
        return;
    }
    if (!ptyxis_embed_initialize (&error))
    {
        report_error (self, error);
        return;
    }

    info = g_file_query_info (self->location, G_FILE_ATTRIBUTE_STANDARD_TYPE,
                              G_FILE_QUERY_INFO_NONE, NULL, &error);
    if (info == NULL)
    {
        report_error (self, error);
        return;
    }
    if (g_file_info_get_file_type (info) != G_FILE_TYPE_DIRECTORY)
    {
        g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY,
                             _("The terminal location is not a folder."));
        report_error (self, error);
        return;
    }

    profile = gtk_drop_down_get_selected_item (self->profiles);
    if (profile != NULL)
    {
        g_object_ref (profile);
    }
    else
    {
        profile = ptyxis_application_dup_default_profile (self->application);
    }
    if (profile == NULL)
    {
        g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                             _("No Ptyxis profile is available."));
        report_error (self, error);
        return;
    }

    uri = g_file_get_uri (self->location);
    tab = ptyxis_tab_new (profile);
    ptyxis_tab_set_initial_working_directory_uri (tab, uri);
    container = gtk_drop_down_get_selected_item (self->containers);
    if (container != NULL)
    {
        ptyxis_tab_set_container (tab, container);
    }
    page = adw_tab_view_append (self->tabs, GTK_WIDGET (tab));
    g_object_bind_property (tab, "title", page, "title", G_BINDING_SYNC_CREATE);
    adw_tab_page_set_tooltip (page, uri);
    adw_tab_view_set_selected_page (self->tabs, page);
    gtk_stack_set_visible_child (self->stack, GTK_WIDGET (self->tabs));
    gtk_widget_set_visible (GTK_WIDGET (self->status), FALSE);
    g_set_object (&self->session_location, self->location);
}

void
nautilus_terminal_set_location (NautilusTerminal *self,
                               GFile            *location)
{
    gboolean local = location != NULL && g_file_is_native (location);

    g_return_if_fail (NAUTILUS_IS_TERMINAL (self));
    g_set_object (&self->location, location);
    gtk_widget_set_sensitive (self->new_session_button, local && self->application != NULL);
    if (!gtk_widget_get_visible (GTK_WIDGET (self)))
    {
        return;
    }
    if (!local)
    {
        gtk_stack_set_visible_child (self->stack, self->unavailable);
        g_clear_object (&self->session_location);
    }
    else if (self->session_location == NULL || !g_file_equal (location, self->session_location))
    {
        start_session (self);
    }
}

static void
show_preferences (NautilusTerminal *self)
{
    PtyxisPreferencesWindow *preferences = ptyxis_preferences_window_get_default ();
    GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (self));

    if (GTK_IS_WINDOW (root))
    {
        gtk_window_set_transient_for (GTK_WINDOW (preferences), GTK_WINDOW (root));
    }
    gtk_window_present (GTK_WINDOW (preferences));
}

static void
show_search (NautilusTerminal *self)
{
    selected_page_changed (self);
    gtk_revealer_set_reveal_child (self->search_revealer, TRUE);
    gtk_widget_grab_focus (GTK_WIDGET (self->search));
}

static void
terminal_action (GtkWidget  *widget,
                 const char *action,
                 GVariant   *parameter)
{
    NautilusTerminal *self = NAUTILUS_TERMINAL (widget);
    PtyxisTab *tab = get_selected_tab (self);
    AdwDialog *dialog;

    if (g_str_equal (action, "win.search"))
    {
        show_search (self);
        return;
    }
    if (tab == NULL)
    {
        return;
    }
    dialog = g_object_new (g_str_equal (action, "win.set-profile") ?
                           PTYXIS_TYPE_PROFILE_DIALOG : PTYXIS_TYPE_TITLE_DIALOG,
                           "tab", tab, NULL);
    adw_dialog_set_presentation_mode (dialog, ADW_DIALOG_FLOATING);
    adw_dialog_present (dialog, widget);
}

static void
finish_close (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
    g_autoptr (GTask) task = user_data;
    AdwTabView *view = g_task_get_source_object (task);
    AdwTabPage *page = g_task_get_task_data (task);
    const char *response = adw_alert_dialog_choose_finish (ADW_ALERT_DIALOG (source), result);
    gboolean close = g_str_equal (response, "close");

    if (close)
    {
        ptyxis_tab_force_quit (PTYXIS_TAB (adw_tab_page_get_child (page)));
    }
    adw_tab_view_close_page_finish (view, page, close);
}

static gboolean
close_page (AdwTabView       *view,
            AdwTabPage       *page,
            NautilusTerminal *self)
{
    PtyxisTab *tab = PTYXIS_TAB (adw_tab_page_get_child (page));

    if (!ptyxis_tab_has_foreground_process (tab, NULL, NULL))
    {
        ptyxis_tab_force_quit (tab);
        adw_tab_view_close_page_finish (view, page, TRUE);
    }
    else
    {
        AdwDialog *dialog = adw_alert_dialog_new (_("Close Terminal?"),
                                                 _("A command is still running in this terminal. Closing it will end the session."));
        GTask *task = g_task_new (view, NULL, NULL, NULL);

        g_task_set_task_data (task, g_object_ref (page), g_object_unref);
        adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                        "cancel", _("_Cancel"), "close", _("_Close"), NULL);
        adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog),
                                                 "close", ADW_RESPONSE_DESTRUCTIVE);
        adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "cancel");
        adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");
        adw_alert_dialog_choose (ADW_ALERT_DIALOG (dialog), GTK_WIDGET (self), NULL, finish_close, task);
    }

    return GDK_EVENT_STOP;
}

static gboolean
key_pressed (GtkEventControllerKey *controller,
             guint                  keyval,
             guint                  keycode,
             GdkModifierType        state,
             NautilusTerminal      *self)
{
    GdkModifierType modifiers = state & gtk_accelerator_get_default_mod_mask ();
    PtyxisTab *tab = get_selected_tab (self);

    if (keyval == GDK_KEY_F4 && modifiers == 0)
    {
        return gtk_widget_activate_action (GTK_WIDGET (self), "slot.terminal-visible", NULL);
    }
    if (modifiers == (GDK_CONTROL_MASK | GDK_SHIFT_MASK))
    {
        switch (keyval)
        {
            case GDK_KEY_t:
            case GDK_KEY_T:
                start_session (self);
                return GDK_EVENT_STOP;

            case GDK_KEY_f:
            case GDK_KEY_F:
                show_search (self);
                return GDK_EVENT_STOP;

            case GDK_KEY_w:
            case GDK_KEY_W:
                if (tab != NULL)
                {
                    adw_tab_view_close_page (self->tabs, adw_tab_view_get_selected_page (self->tabs));
                }
                return GDK_EVENT_STOP;
        }
    }
    if (tab != NULL &&
        (modifiers == GDK_CONTROL_MASK ||
         (keyval == GDK_KEY_plus && modifiers == (GDK_CONTROL_MASK | GDK_SHIFT_MASK))))
    {
        if (keyval == GDK_KEY_plus || keyval == GDK_KEY_equal)
        {
            ptyxis_tab_zoom_in (tab);
            return GDK_EVENT_STOP;
        }
        if (keyval == GDK_KEY_minus)
        {
            ptyxis_tab_zoom_out (tab);
            return GDK_EVENT_STOP;
        }
        if (keyval == GDK_KEY_0)
        {
            ptyxis_tab_set_zoom (tab, PTYXIS_ZOOM_LEVEL_DEFAULT);
            return GDK_EVENT_STOP;
        }
    }

    return GDK_EVENT_PROPAGATE;
}

static gboolean
grab_focus (GtkWidget *widget)
{
    PtyxisTab *tab = get_selected_tab (NAUTILUS_TERMINAL (widget));

    if (tab == NULL)
    {
        return FALSE;
    }
    ptyxis_tab_grab_focus (tab);
    return TRUE;
}

static void
dispose (GObject *object)
{
    NautilusTerminal *self = NAUTILUS_TERMINAL (object);

    if (self->tabs != NULL)
    {
        g_signal_handlers_disconnect_by_data (self->tabs, self);
        ptyxis_find_bar_set_terminal (self->search, NULL);
        for (int i = 0; i < adw_tab_view_get_n_pages (self->tabs); i++)
        {
            AdwTabPage *page = adw_tab_view_get_nth_page (self->tabs, i);

            ptyxis_tab_force_quit (PTYXIS_TAB (adw_tab_page_get_child (page)));
        }
        self->tabs = NULL;
    }
    g_clear_object (&self->location);
    g_clear_object (&self->session_location);
    g_clear_object (&self->terminal_actions);
    G_OBJECT_CLASS (nautilus_terminal_parent_class)->dispose (object);
}

static void
nautilus_terminal_class_init (NautilusTerminalClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = dispose;
    GTK_WIDGET_CLASS (klass)->grab_focus = grab_focus;
    gtk_widget_class_install_action (GTK_WIDGET_CLASS (klass), "win.search", NULL, terminal_action);
    gtk_widget_class_install_action (GTK_WIDGET_CLASS (klass), "win.set-title", NULL, terminal_action);
    gtk_widget_class_install_action (GTK_WIDGET_CLASS (klass), "win.set-profile", NULL, terminal_action);
}

static GtkWidget *
add_button (GtkBox     *box,
            const char *icon,
            const char *tooltip)
{
    GtkWidget *button = gtk_button_new_from_icon_name (icon);

    gtk_widget_set_tooltip_text (button, tooltip);
    gtk_box_append (box, button);
    return button;
}

static void
nautilus_terminal_init (NautilusTerminal *self)
{
    g_autoptr (GError) error = NULL;
    GtkWidget *header = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *bar = GTK_WIDGET (adw_tab_bar_new ());
    GtkWidget *search_button;
    GtkWidget *preferences_button;
    GtkWidget *hide;
    GtkWidget *overlay = adw_toast_overlay_new ();
    GtkEventController *controller = gtk_event_controller_key_new ();

    gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_size_request (GTK_WIDGET (self), -1, 180);
    self->tabs = adw_tab_view_new ();
    self->terminal_actions = g_simple_action_group_new ();
    gtk_widget_insert_action_group (GTK_WIDGET (self), "win", G_ACTION_GROUP (self->terminal_actions));
    self->stack = GTK_STACK (gtk_stack_new ());
    gtk_stack_set_hhomogeneous (self->stack, FALSE);
    gtk_stack_set_vhomogeneous (self->stack, FALSE);
    gtk_widget_set_vexpand (GTK_WIDGET (self->stack), TRUE);
    self->unavailable = gtk_label_new (_("The inline terminal is only available for local folders."));
    gtk_stack_add_named (self->stack, self->unavailable, "unavailable");
    gtk_stack_add_named (self->stack, GTK_WIDGET (self->tabs), "sessions");
    adw_tab_bar_set_view (ADW_TAB_BAR (bar), self->tabs);
    adw_tab_bar_set_autohide (ADW_TAB_BAR (bar), FALSE);
    gtk_widget_set_hexpand (bar, TRUE);
    gtk_box_append (GTK_BOX (header), bar);
    self->new_session_button = add_button (GTK_BOX (header), "tab-new-symbolic", _("New Ptyxis Session"));
    search_button = add_button (GTK_BOX (header), "edit-find-symbolic", _("Find in Terminal"));
    preferences_button = add_button (GTK_BOX (header), "preferences-system-symbolic", _("Ptyxis Preferences"));
    hide = add_button (GTK_BOX (header), "window-close-symbolic", _("Hide Terminal"));
    gtk_actionable_set_action_name (GTK_ACTIONABLE (hide), "slot.terminal-visible");
    gtk_box_append (GTK_BOX (self), header);
    self->search = g_object_new (PTYXIS_TYPE_FIND_BAR, NULL);
    self->search_revealer = GTK_REVEALER (gtk_revealer_new ());
    gtk_revealer_set_child (self->search_revealer, GTK_WIDGET (self->search));
    gtk_box_append (GTK_BOX (self), GTK_WIDGET (self->search_revealer));
    adw_toast_overlay_set_child (ADW_TOAST_OVERLAY (overlay), GTK_WIDGET (self->stack));
    gtk_box_append (GTK_BOX (self), overlay);
    self->status = GTK_LABEL (gtk_label_new (NULL));
    gtk_label_set_wrap (self->status, TRUE);
    gtk_widget_set_visible (GTK_WIDGET (self->status), FALSE);
    gtk_box_append (GTK_BOX (self), GTK_WIDGET (self->status));

    if (ptyxis_embed_initialize (&error))
    {
        GtkWidget *options = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
        GtkExpression *profile_expression = gtk_property_expression_new (PTYXIS_TYPE_PROFILE, NULL, "label");
        GtkExpression *container_expression = gtk_property_expression_new (PTYXIS_IPC_TYPE_CONTAINER, NULL, "display-name");
        g_autoptr (PtyxisProfile) default_profile = NULL;
        g_autoptr (GListModel) profiles = NULL;
        g_autoptr (GListModel) containers = NULL;

        self->application = ptyxis_embed_get_application ();
        g_signal_connect_object (self->application, "embed-error", G_CALLBACK (agent_failed), self, 0);
        profiles = ptyxis_application_list_profiles (self->application);
        self->profiles = GTK_DROP_DOWN (gtk_drop_down_new (g_object_ref (profiles), profile_expression));
        containers = ptyxis_application_list_containers (self->application);
        self->containers = GTK_DROP_DOWN (gtk_drop_down_new (g_object_ref (containers), container_expression));
        gtk_widget_set_tooltip_text (GTK_WIDGET (self->profiles), _("Profile for New Sessions"));
        gtk_widget_set_tooltip_text (GTK_WIDGET (self->containers), _("Container for New Sessions"));
        gtk_box_append (GTK_BOX (options), GTK_WIDGET (self->profiles));
        gtk_box_append (GTK_BOX (options), GTK_WIDGET (self->containers));
        gtk_box_insert_child_after (GTK_BOX (self), options, header);
        default_profile = ptyxis_application_dup_default_profile (self->application);
        for (guint i = 0; i < g_list_model_get_n_items (profiles); i++)
        {
            g_autoptr (PtyxisProfile) profile = g_list_model_get_item (profiles, i);

            if (profile == default_profile)
            {
                gtk_drop_down_set_selected (self->profiles, i);
                break;
            }
        }
        gtk_widget_insert_action_group (GTK_WIDGET (self), "app", G_ACTION_GROUP (self->application));
        g_signal_connect_swapped (self->profiles, "notify::selected-item", G_CALLBACK (profile_changed), self);
        profile_changed (self);
    }
    else
    {
        report_error (self, error);
        gtk_widget_set_sensitive (self->new_session_button, FALSE);
        gtk_widget_set_sensitive (preferences_button, FALSE);
    }

    g_signal_connect_swapped (self->new_session_button, "clicked", G_CALLBACK (start_session), self);
    g_signal_connect_swapped (preferences_button, "clicked", G_CALLBACK (show_preferences), self);
    g_signal_connect_swapped (search_button, "clicked", G_CALLBACK (show_search), self);
    g_signal_connect_swapped (self->tabs, "notify::selected-page", G_CALLBACK (selected_page_changed), self);
    g_signal_connect (self->tabs, "close-page", G_CALLBACK (close_page), self);
    gtk_event_controller_set_propagation_phase (controller, GTK_PHASE_CAPTURE);
    g_signal_connect (controller, "key-pressed", G_CALLBACK (key_pressed), self);
    gtk_widget_add_controller (GTK_WIDGET (self), controller);
}

GtkWidget *
nautilus_terminal_new (void)
{
    return g_object_new (NAUTILUS_TYPE_TERMINAL, NULL);
}
