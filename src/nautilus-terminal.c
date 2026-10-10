/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "config.h"

#include "nautilus-terminal.h"

#include <adwaita.h>
#include <errno.h>
#include <fcntl.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <vte/vte.h>

struct _NautilusTerminal
{
    GtkBox parent_instance;

    GFile *location;
    GFile *session_location;
    GtkStack *stack;
    GtkDropDown *selector;
    GtkStringList *names;
    GtkWidget *new_session_button;
    GtkWidget *unavailable;
    GPtrArray *cancellables;
};

G_DEFINE_FINAL_TYPE (NautilusTerminal, nautilus_terminal, GTK_TYPE_BOX)

static VteTerminal *
get_selected_terminal (NautilusTerminal *self)
{
    GtkWidget *page = gtk_stack_get_visible_child (self->stack);

    if (page == NULL || page == self->unavailable)
    {
        return NULL;
    }

    return VTE_TERMINAL (gtk_widget_get_first_child (page));
}

static void
update_colors (NautilusTerminal *self)
{
    gboolean dark = adw_style_manager_get_dark (adw_style_manager_get_default ());
    GdkRGBA foreground;
    GdkRGBA background;

    gdk_rgba_parse (&foreground, dark ? "#ffffff" : "#241f31");
    gdk_rgba_parse (&background, dark ? "#1e1e1e" : "#ffffff");

    for (GtkWidget *page = gtk_widget_get_first_child (GTK_WIDGET (self->stack));
         page != NULL;
         page = gtk_widget_get_next_sibling (page))
    {
        if (page != self->unavailable)
        {
            VteTerminal *terminal = VTE_TERMINAL (gtk_widget_get_first_child (page));

            vte_terminal_set_colors (terminal, &foreground, &background, NULL, 0);
        }
    }
}

static void
show_spawn_error (GtkLabel *status,
                  GError   *error)
{
    g_autofree char *message = g_strdup_printf (_("Could not start the terminal: %s"),
                                               error->message);

    g_warning ("Could not start inline terminal: %s", error->message);
    gtk_label_set_text (status, message);
    gtk_widget_set_visible (GTK_WIDGET (status), TRUE);
}

static void
spawn_finished (VteTerminal *terminal,
                GPid         pid,
                GError      *error,
                gpointer     user_data)
{
    g_autoptr (GtkLabel) status = user_data;

    if (terminal == NULL || g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
        return;
    }
    if (error != NULL)
    {
        show_spawn_error (status, error);
    }
    else
    {
        gtk_widget_set_visible (GTK_WIDGET (status), FALSE);
    }
}

static void
set_child_directory (gpointer user_data)
{
    int fd = GPOINTER_TO_INT (user_data);

    if (fchdir (fd) != 0)
    {
        const char message[] = "Could not enter the terminal working directory.\n";

        write (STDERR_FILENO, message, sizeof message - 1);
        _exit (127);
    }
    close (fd);
}

static void
child_exited (VteTerminal *terminal,
              int          status,
              GtkLabel    *label)
{
    gtk_label_set_text (label, _("Shell exited. Use New Session to start another terminal."));
    gtk_widget_set_visible (GTK_WIDGET (label), TRUE);
}

static void
select_session (NautilusTerminal *self)
{
    guint selected = gtk_drop_down_get_selected (self->selector);
    g_autofree char *name = g_strdup_printf ("%u", selected);

    if (selected != GTK_INVALID_LIST_POSITION)
    {
        gtk_stack_set_visible_child_name (self->stack, name);
    }
}

static void
start_session (NautilusTerminal *self)
{
    g_autofree char *path = self->location == NULL ? NULL : g_file_get_path (self->location);
    g_autofree char *shell = vte_get_user_shell ();
    g_autofree char *name = NULL;
    g_autofree char *label = NULL;
    g_autofree char *session_number = NULL;
    g_autoptr (PangoFontDescription) font = pango_font_description_from_string ("Monospace 11");
    GtkWidget *page;
    GtkWidget *terminal;
    GtkWidget *status;
    GCancellable *cancellable;
    guint index;
    int directory_fd;
    int child_fd = 3;

    if (path == NULL)
    {
        gtk_stack_set_visible_child (self->stack, self->unavailable);
        return;
    }

    index = g_list_model_get_n_items (G_LIST_MODEL (self->names));
    name = g_strdup_printf ("%u", index);
    session_number = g_strdup_printf ("%u", index + 1);
    /* Translators: a terminal session number followed by its working directory. */
    label = g_strdup_printf (_("%s: %s"), session_number, path);
    page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    terminal = vte_terminal_new ();
    gtk_widget_set_hexpand (terminal, TRUE);
    gtk_widget_set_vexpand (terminal, TRUE);
    vte_terminal_set_font (VTE_TERMINAL (terminal), font);
    vte_terminal_set_scrollback_lines (VTE_TERMINAL (terminal), 10000);
    gtk_accessible_update_property (GTK_ACCESSIBLE (terminal),
                                    GTK_ACCESSIBLE_PROPERTY_LABEL, _("Terminal"),
                                    -1);
    gtk_box_append (GTK_BOX (page), terminal);

    status = gtk_label_new (_("Starting terminal…"));
    gtk_label_set_wrap (GTK_LABEL (status), TRUE);
    gtk_widget_set_margin_start (status, 12);
    gtk_widget_set_margin_end (status, 12);
    gtk_widget_set_margin_top (status, 6);
    gtk_widget_set_margin_bottom (status, 6);
    gtk_box_append (GTK_BOX (page), status);
    g_signal_connect_object (terminal, "child-exited", G_CALLBACK (child_exited), status, 0);

    gtk_stack_add_named (self->stack, page, name);
    gtk_string_list_append (self->names, label);
    gtk_drop_down_set_selected (self->selector, index);
    gtk_stack_set_visible_child (self->stack, page);
    g_set_object (&self->session_location, self->location);
    update_colors (self);

    cancellable = g_cancellable_new ();
    g_ptr_array_add (self->cancellables, cancellable);
    if (shell == NULL)
    {
        g_warning ("Could not determine the user's shell for the inline terminal");
        gtk_label_set_text (GTK_LABEL (status), _("Could not determine your shell."));
        return;
    }
    /* VTE falls back to home on chdir failure. A mapped directory descriptor
     * prevents that fallback and keeps the directory stable during spawning. */
    directory_fd = g_open (path, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0);
    if (directory_fd == -1)
    {
        g_autoptr (GError) error = g_error_new (G_IO_ERROR, g_io_error_from_errno (errno),
                                              _("Could not open %s: %s"), path, g_strerror (errno));

        show_spawn_error (GTK_LABEL (status), error);
        return;
    }

    const char *argv[] = { shell, NULL };
    vte_terminal_spawn_with_fds_async (VTE_TERMINAL (terminal),
                                      VTE_PTY_DEFAULT, path, argv, NULL,
                                      &directory_fd, 1, &child_fd, 1,
                                      G_SPAWN_DEFAULT, set_child_directory,
                                      GINT_TO_POINTER (child_fd), NULL,
                                      -1, cancellable, spawn_finished, g_object_ref (status));
}

void
nautilus_terminal_set_location (NautilusTerminal *self,
                               GFile            *location)
{
    g_autofree char *path = NULL;

    g_return_if_fail (NAUTILUS_IS_TERMINAL (self));

    g_set_object (&self->location, location);
    if (location != NULL)
    {
        path = g_file_get_path (location);
    }
    gtk_widget_set_sensitive (self->new_session_button, path != NULL);

    if (!gtk_widget_get_visible (GTK_WIDGET (self)))
    {
        return;
    }

    if (path == NULL)
    {
        gtk_stack_set_visible_child (self->stack, self->unavailable);
        g_clear_object (&self->session_location);
    }
    else if (self->session_location == NULL ||
             !g_file_equal (location, self->session_location))
    {
        start_session (self);
    }
}

static gboolean
terminal_key_pressed (GtkEventControllerKey *controller,
                      guint                  keyval,
                      guint                  keycode,
                      GdkModifierType        state,
                      NautilusTerminal      *self)
{
    VteTerminal *terminal = get_selected_terminal (self);
    GdkModifierType modifiers = state & gtk_accelerator_get_default_mod_mask ();

    if (keyval == GDK_KEY_F4 && modifiers == 0)
    {
        return gtk_widget_activate_action (GTK_WIDGET (self), "slot.terminal-visible", NULL);
    }

    if (terminal != NULL && modifiers == (GDK_CONTROL_MASK | GDK_SHIFT_MASK))
    {
        if (keyval == GDK_KEY_C || keyval == GDK_KEY_c)
        {
            vte_terminal_copy_clipboard_format (terminal, VTE_FORMAT_TEXT);
            return GDK_EVENT_STOP;
        }
        if (keyval == GDK_KEY_V || keyval == GDK_KEY_v)
        {
            vte_terminal_paste_clipboard (terminal);
            return GDK_EVENT_STOP;
        }
    }

    return GDK_EVENT_PROPAGATE;
}

static gboolean
nautilus_terminal_grab_focus (GtkWidget *widget)
{
    VteTerminal *terminal = get_selected_terminal (NAUTILUS_TERMINAL (widget));

    return terminal != NULL && gtk_widget_grab_focus (GTK_WIDGET (terminal));
}

static void
nautilus_terminal_dispose (GObject *object)
{
    NautilusTerminal *self = NAUTILUS_TERMINAL (object);

    if (self->cancellables != NULL)
    {
        for (guint i = 0; i < self->cancellables->len; i++)
        {
            g_cancellable_cancel (g_ptr_array_index (self->cancellables, i));
        }
        g_clear_pointer (&self->cancellables, g_ptr_array_unref);
    }
    g_clear_object (&self->names);
    g_clear_object (&self->location);
    g_clear_object (&self->session_location);

    G_OBJECT_CLASS (nautilus_terminal_parent_class)->dispose (object);
}

static void
nautilus_terminal_class_init (NautilusTerminalClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = nautilus_terminal_dispose;
    GTK_WIDGET_CLASS (klass)->grab_focus = nautilus_terminal_grab_focus;
}

static void
nautilus_terminal_init (NautilusTerminal *self)
{
    GtkWidget *header = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *close = gtk_button_new_from_icon_name ("window-close-symbolic");
    GtkEventController *controller = gtk_event_controller_key_new ();

    gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_size_request (GTK_WIDGET (self), -1, 160);
    self->cancellables = g_ptr_array_new_with_free_func (g_object_unref);
    self->names = gtk_string_list_new (NULL);
    self->selector = GTK_DROP_DOWN (gtk_drop_down_new (G_LIST_MODEL (g_object_ref (self->names)), NULL));
    gtk_drop_down_set_enable_search (self->selector, TRUE);
    gtk_widget_set_hexpand (GTK_WIDGET (self->selector), TRUE);
    gtk_widget_set_tooltip_text (GTK_WIDGET (self->selector), _("Terminal Sessions"));
    self->new_session_button = gtk_button_new_from_icon_name ("tab-new-symbolic");
    gtk_widget_set_tooltip_text (self->new_session_button, _("New Session"));
    gtk_widget_set_tooltip_text (close, _("Hide Terminal"));
    gtk_actionable_set_action_name (GTK_ACTIONABLE (close), "slot.terminal-visible");
    gtk_box_append (GTK_BOX (header), GTK_WIDGET (self->selector));
    gtk_box_append (GTK_BOX (header), self->new_session_button);
    gtk_box_append (GTK_BOX (header), close);
    gtk_widget_add_css_class (header, "toolbar");
    gtk_box_append (GTK_BOX (self), header);

    self->stack = GTK_STACK (gtk_stack_new ());
    gtk_stack_set_hhomogeneous (self->stack, FALSE);
    gtk_stack_set_vhomogeneous (self->stack, FALSE);
    gtk_widget_set_vexpand (GTK_WIDGET (self->stack), TRUE);
    self->unavailable = gtk_label_new (_("The inline terminal is only available for local folders."));
    gtk_label_set_wrap (GTK_LABEL (self->unavailable), TRUE);
    gtk_stack_add_named (self->stack, self->unavailable, "unavailable");
    gtk_box_append (GTK_BOX (self), GTK_WIDGET (self->stack));
    g_signal_connect_swapped (self->selector, "notify::selected", G_CALLBACK (select_session), self);
    g_signal_connect_swapped (self->new_session_button, "clicked", G_CALLBACK (start_session), self);
    g_signal_connect_object (adw_style_manager_get_default (), "notify::dark",
                             G_CALLBACK (update_colors), self, G_CONNECT_SWAPPED);
    gtk_event_controller_set_propagation_phase (controller, GTK_PHASE_CAPTURE);
    g_signal_connect (controller, "key-pressed", G_CALLBACK (terminal_key_pressed), self);
    gtk_widget_add_controller (GTK_WIDGET (self), controller);
}

GtkWidget *
nautilus_terminal_new (void)
{
    return g_object_new (NAUTILUS_TYPE_TERMINAL, NULL);
}
