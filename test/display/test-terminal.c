/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <nautilus-terminal.h>

#include <adwaita.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <vte/vte.h>

typedef struct
{
    GtkWindow *window;
    NautilusTerminal *panel;
    GtkDropDown *selector;
    GtkStack *stack;
    char *directory;
    char *child_directory;
} Fixture;

static void
setup (Fixture      *fixture,
       gconstpointer data)
{
    g_autoptr (GError) error = NULL;
    GtkWidget *header;

    fixture->directory = g_dir_make_tmp ("nautilus-terminal-XXXXXX", &error);
    g_assert_no_error (error);
    fixture->child_directory = g_build_filename (fixture->directory, "folder ' $HOME; literal", NULL);
    g_assert_cmpint (g_mkdir (fixture->child_directory, 0700), ==, 0);
    fixture->window = GTK_WINDOW (gtk_window_new ());
    fixture->panel = NAUTILUS_TERMINAL (nautilus_terminal_new ());
    gtk_window_set_default_size (fixture->window, 900, 300);
    gtk_window_set_child (fixture->window, GTK_WIDGET (fixture->panel));
    header = gtk_widget_get_first_child (GTK_WIDGET (fixture->panel));
    fixture->selector = GTK_DROP_DOWN (gtk_widget_get_first_child (header));
    fixture->stack = GTK_STACK (gtk_widget_get_next_sibling (header));
    gtk_window_present (fixture->window);
}

static void
teardown (Fixture      *fixture,
          gconstpointer data)
{
    gtk_window_destroy (fixture->window);
    g_assert_cmpint (g_rmdir (fixture->child_directory), ==, 0);
    g_assert_cmpint (g_rmdir (fixture->directory), ==, 0);
    g_free (fixture->child_directory);
    g_free (fixture->directory);
}

static guint
session_count (Fixture *fixture)
{
    return g_list_model_get_n_items (gtk_drop_down_get_model (fixture->selector));
}

static VteTerminal *
get_terminal (Fixture *fixture,
              guint    index)
{
    g_autofree char *name = g_strdup_printf ("%u", index);
    GtkWidget *page = gtk_stack_get_child_by_name (fixture->stack, name);

    g_assert_nonnull (page);
    return VTE_TERMINAL (gtk_widget_get_first_child (page));
}

static void
iterate (void)
{
    while (g_main_context_iteration (NULL, FALSE))
    {
    }
    g_usleep (1000);
}

static void
assert_working_directory (VteTerminal *terminal,
                          const char  *directory)
{
    gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
    g_autofree char *expected = g_strconcat ("__NAUTILUS_PWD__:", directory, NULL);

    while (vte_terminal_get_pty (terminal) == NULL && g_get_monotonic_time () < deadline)
    {
        iterate ();
    }
    g_assert_nonnull (vte_terminal_get_pty (terminal));
    vte_terminal_set_size (terminal, 200, 24);
    vte_terminal_feed_child (terminal, "printf '__NAUTILUS_PWD__:%s\\n' \"$PWD\"\n", -1);

    while (g_get_monotonic_time () < deadline)
    {
        g_autofree char *text = vte_terminal_get_text (terminal, NULL, NULL, NULL);

        if (strstr (text, expected) != NULL)
        {
            return;
        }
        iterate ();
    }

    g_error ("Terminal did not report the expected working directory: %s", directory);
}

static void
test_navigation (Fixture      *fixture,
                 gconstpointer data)
{
    g_autoptr (GFile) first = g_file_new_for_path (fixture->directory);
    g_autoptr (GFile) second = g_file_new_for_path (fixture->child_directory);
    VteTerminal *original;

    nautilus_terminal_set_location (fixture->panel, first);
    g_assert_cmpuint (session_count (fixture), ==, 1);
    original = get_terminal (fixture, 0);
    assert_working_directory (original, fixture->directory);

    nautilus_terminal_set_location (fixture->panel, first);
    g_assert_cmpuint (session_count (fixture), ==, 1);
    nautilus_terminal_set_location (fixture->panel, second);
    g_assert_cmpuint (session_count (fixture), ==, 2);
    assert_working_directory (get_terminal (fixture, 1), fixture->child_directory);

    gtk_drop_down_set_selected (fixture->selector, 0);
    g_assert_true (gtk_widget_get_first_child (gtk_stack_get_visible_child (fixture->stack)) ==
                   GTK_WIDGET (original));
    assert_working_directory (original, fixture->directory);

    nautilus_terminal_set_location (fixture->panel, first);
    g_assert_cmpuint (session_count (fixture), ==, 3);
    assert_working_directory (get_terminal (fixture, 2), fixture->directory);
}

static void
test_hidden (Fixture      *fixture,
             gconstpointer data)
{
    g_autoptr (GFile) first = g_file_new_for_path (fixture->directory);
    g_autoptr (GFile) second = g_file_new_for_path (fixture->child_directory);

    gtk_widget_set_visible (GTK_WIDGET (fixture->panel), FALSE);
    nautilus_terminal_set_location (fixture->panel, first);
    g_assert_cmpuint (session_count (fixture), ==, 0);
    gtk_widget_set_visible (GTK_WIDGET (fixture->panel), TRUE);
    nautilus_terminal_set_location (fixture->panel, first);
    assert_working_directory (get_terminal (fixture, 0), fixture->directory);

    gtk_widget_set_visible (GTK_WIDGET (fixture->panel), FALSE);
    nautilus_terminal_set_location (fixture->panel, second);
    g_assert_cmpuint (session_count (fixture), ==, 1);
    gtk_widget_set_visible (GTK_WIDGET (fixture->panel), TRUE);
    nautilus_terminal_set_location (fixture->panel, second);
    g_assert_cmpuint (session_count (fixture), ==, 2);
    assert_working_directory (get_terminal (fixture, 1), fixture->child_directory);
}

static void
test_nonlocal (Fixture      *fixture,
               gconstpointer data)
{
    const char *uris[] = { "sftp://example.invalid/folder", "trash:///", "recent:///", "starred:///" };
    g_autoptr (GFile) local = g_file_new_for_path (fixture->directory);

    nautilus_terminal_set_location (fixture->panel, NULL);
    g_assert_cmpuint (session_count (fixture), ==, 0);
    for (guint i = 0; i < G_N_ELEMENTS (uris); i++)
    {
        g_autoptr (GFile) location = g_file_new_for_uri (uris[i]);

        nautilus_terminal_set_location (fixture->panel, location);
        g_assert_cmpuint (session_count (fixture), ==, 0);
        g_assert_cmpstr (gtk_stack_get_visible_child_name (fixture->stack), ==, "unavailable");
    }

    nautilus_terminal_set_location (fixture->panel, local);
    assert_working_directory (get_terminal (fixture, 0), fixture->directory);
    g_autoptr (GFile) remote = g_file_new_for_uri (uris[0]);
    nautilus_terminal_set_location (fixture->panel, remote);
    g_assert_cmpuint (session_count (fixture), ==, 1);
    g_assert_cmpstr (gtk_stack_get_visible_child_name (fixture->stack), ==, "unavailable");
    nautilus_terminal_set_location (fixture->panel, local);
    g_assert_cmpuint (session_count (fixture), ==, 2);
}

static void
test_spawn_failure (Fixture      *fixture,
                    gconstpointer data)
{
    g_autofree char *missing = g_build_filename (fixture->directory, "missing", NULL);
    g_autoptr (GFile) location = g_file_new_for_path (missing);
    GtkWidget *status;
    gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;

    g_test_expect_message (NULL, G_LOG_LEVEL_WARNING, "*Could not start inline terminal:*");
    nautilus_terminal_set_location (fixture->panel, location);
    status = gtk_widget_get_next_sibling (GTK_WIDGET (get_terminal (fixture, 0)));
    while (g_str_has_prefix (gtk_label_get_text (GTK_LABEL (status)), "Starting") &&
           g_get_monotonic_time () < deadline)
    {
        iterate ();
    }
    g_test_assert_expected_messages ();
    g_assert_true (gtk_widget_get_visible (status));
    g_assert_true (g_str_has_prefix (gtk_label_get_text (GTK_LABEL (status)), "Could not start"));
}

static void
test_close_pending (Fixture      *fixture,
                    gconstpointer data)
{
    g_autoptr (GFile) location = g_file_new_for_path (fixture->directory);

    nautilus_terminal_set_location (fixture->panel, location);
    gtk_window_set_child (fixture->window, NULL);
    for (guint i = 0; i < 100; i++)
    {
        iterate ();
    }
}

static void
test_close_running (Fixture      *fixture,
                    gconstpointer data)
{
    g_autoptr (GFile) location = g_file_new_for_path (fixture->directory);
    g_autoptr (GFile) child = g_file_new_for_path (fixture->child_directory);
    pid_t pids[2];
    gint64 deadline;

    nautilus_terminal_set_location (fixture->panel, location);
    assert_working_directory (get_terminal (fixture, 0), fixture->directory);
    nautilus_terminal_set_location (fixture->panel, child);
    assert_working_directory (get_terminal (fixture, 1), fixture->child_directory);
    for (guint i = 0; i < G_N_ELEMENTS (pids); i++)
    {
        VteTerminal *terminal = get_terminal (fixture, i);
        gboolean found = FALSE;

        vte_terminal_feed_child (terminal, "printf '__NAUTILUS_PID__:%s\\n' \"$$\"\n", -1);
        deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
        while (g_get_monotonic_time () < deadline)
        {
            g_autofree char *text = vte_terminal_get_text (terminal, NULL, NULL, NULL);
            const char *marker = strstr (text, "__NAUTILUS_PID__:");

            while (marker != NULL)
            {
                const char *value = marker + strlen ("__NAUTILUS_PID__:");

                if (g_ascii_isdigit (*value))
                {
                    pids[i] = strtol (value, NULL, 10);
                    found = TRUE;
                    break;
                }
                marker = strstr (value, "__NAUTILUS_PID__:");
            }
            if (found)
            {
                break;
            }
            iterate ();
        }
        g_assert_true (found);
        g_assert_cmpint (pids[i], >, 0);
        g_assert_cmpint (kill (pids[i], 0), ==, 0);
    }

    gtk_window_set_child (fixture->window, NULL);
    for (guint i = 0; i < G_N_ELEMENTS (pids); i++)
    {
        deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
        while (kill (pids[i], 0) == 0 && g_get_monotonic_time () < deadline)
        {
            iterate ();
        }
        g_assert_cmpint (kill (pids[i], 0), ==, -1);
        g_assert_cmpint (errno, ==, ESRCH);
    }
}

static void
test_new_session_and_exit (Fixture      *fixture,
                          gconstpointer data)
{
    g_autoptr (GFile) location = g_file_new_for_path (fixture->directory);
    GtkWidget *header = gtk_widget_get_first_child (GTK_WIDGET (fixture->panel));
    GtkWidget *button = gtk_widget_get_next_sibling (GTK_WIDGET (fixture->selector));
    GtkWidget *status;
    gint64 deadline;

    g_assert_true (gtk_widget_get_parent (button) == header);
    nautilus_terminal_set_location (fixture->panel, location);
    assert_working_directory (get_terminal (fixture, 0), fixture->directory);
    g_signal_emit_by_name (button, "clicked");
    g_assert_cmpuint (session_count (fixture), ==, 2);
    assert_working_directory (get_terminal (fixture, 1), fixture->directory);

    status = gtk_widget_get_next_sibling (GTK_WIDGET (get_terminal (fixture, 1)));
    g_assert_false (gtk_widget_get_visible (status));
    vte_terminal_feed_child (get_terminal (fixture, 1), "exit\n", -1);
    deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
    while (!gtk_widget_get_visible (status) && g_get_monotonic_time () < deadline)
    {
        iterate ();
    }
    g_assert_true (gtk_widget_get_visible (status));
    g_signal_emit_by_name (button, "clicked");
    g_assert_cmpuint (session_count (fixture), ==, 3);
    assert_working_directory (get_terminal (fixture, 2), fixture->directory);
}

static void
hide_terminal (GSimpleAction *action,
               GVariant      *state,
               gpointer       user_data)
{
    gtk_widget_set_visible (GTK_WIDGET (user_data), g_variant_get_boolean (state));
    g_simple_action_set_state (action, state);
}

static void
test_keys_and_theme (Fixture      *fixture,
                     gconstpointer data)
{
    g_autoptr (GFile) location = g_file_new_for_path (fixture->directory);
    g_autoptr (GSimpleActionGroup) actions = g_simple_action_group_new ();
    g_autoptr (GListModel) controllers = gtk_widget_observe_controllers (GTK_WIDGET (fixture->panel));
    g_autoptr (GtkEventController) key_controller = NULL;
    const GActionEntry entries[] = {
        { .name = "terminal-visible", .state = "true", .change_state = hide_terminal },
    };
    gboolean handled;
    AdwStyleManager *style = adw_style_manager_get_default ();

    g_action_map_add_action_entries (G_ACTION_MAP (actions), entries, G_N_ELEMENTS (entries), fixture->panel);
    gtk_widget_insert_action_group (GTK_WIDGET (fixture->panel), "slot", G_ACTION_GROUP (actions));
    nautilus_terminal_set_location (fixture->panel, location);
    assert_working_directory (get_terminal (fixture, 0), fixture->directory);
    for (guint i = 0; i < g_list_model_get_n_items (controllers); i++)
    {
        g_autoptr (GtkEventController) controller = g_list_model_get_item (controllers, i);

        if (GTK_IS_EVENT_CONTROLLER_KEY (controller))
        {
            g_set_object (&key_controller, controller);
            break;
        }
    }
    g_assert_nonnull (key_controller);
    g_signal_emit_by_name (key_controller, "key-pressed", GDK_KEY_c, 0, GDK_CONTROL_MASK, &handled);
    g_assert_false (handled);
    g_signal_emit_by_name (key_controller, "key-pressed", GDK_KEY_c, 0,
                          GDK_CONTROL_MASK | GDK_SHIFT_MASK, &handled);
    g_assert_true (handled);

    adw_style_manager_set_color_scheme (style, ADW_COLOR_SCHEME_FORCE_DARK);
    adw_style_manager_set_color_scheme (style, ADW_COLOR_SCHEME_FORCE_LIGHT);
    adw_style_manager_set_color_scheme (style, ADW_COLOR_SCHEME_DEFAULT);
    g_signal_emit_by_name (key_controller, "key-pressed", GDK_KEY_F4, 0, 0, &handled);
    g_assert_true (handled);
    g_assert_false (gtk_widget_get_visible (GTK_WIDGET (fixture->panel)));
    g_assert_cmpuint (session_count (fixture), ==, 1);
}

int
main (int   argc,
      char *argv[])
{
    gtk_test_init (&argc, &argv, NULL);
    adw_init ();
    g_test_add ("/terminal/navigation", Fixture, NULL, setup, test_navigation, teardown);
    g_test_add ("/terminal/hidden", Fixture, NULL, setup, test_hidden, teardown);
    g_test_add ("/terminal/nonlocal", Fixture, NULL, setup, test_nonlocal, teardown);
    g_test_add ("/terminal/spawn-failure", Fixture, NULL, setup, test_spawn_failure, teardown);
    g_test_add ("/terminal/close-pending", Fixture, NULL, setup, test_close_pending, teardown);
    g_test_add ("/terminal/close-running", Fixture, NULL, setup, test_close_running, teardown);
    g_test_add ("/terminal/new-session-and-exit", Fixture, NULL, setup, test_new_session_and_exit, teardown);
    g_test_add ("/terminal/keys-and-theme", Fixture, NULL, setup, test_keys_and_theme, teardown);

    return g_test_run ();
}
