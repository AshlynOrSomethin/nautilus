/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <nautilus-terminal.h>

#include <adwaita.h>
#include <glib/gstdio.h>
#include <ptyxis-embed.h>
#include <ptyxis-find-bar.h>
#include <ptyxis-preferences-window.h>
#include <ptyxis-profile-dialog.h>
#include <ptyxis-tab.h>
#include <ptyxis-title-dialog.h>
#include <string.h>

typedef struct
{
    GtkWindow *window;
    NautilusTerminal *panel;
    AdwTabView *tabs;
    char *directory;
    char *child_directory;
} Fixture;

static GtkWidget *
find_widget (GtkWidget *parent,
             GType      type)
{
    if (G_TYPE_CHECK_INSTANCE_TYPE (parent, type))
    {
        return parent;
    }
    for (GtkWidget *child = gtk_widget_get_first_child (parent);
         child != NULL;
         child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *match = find_widget (child, type);

        if (match != NULL)
        {
            return match;
        }
    }
    return NULL;
}

static void
iterate (void)
{
    for (guint i = 0; i < 100 && g_main_context_iteration (NULL, FALSE); i++)
    {
    }
    g_usleep (1000);
}

static void
setup (Fixture      *fixture,
       gconstpointer data)
{
    g_autoptr (GError) error = NULL;

    fixture->directory = g_dir_make_tmp ("nautilus-ptyxis-XXXXXX", &error);
    g_assert_no_error (error);
    fixture->child_directory = g_build_filename (fixture->directory, "folder ' $HOME; literal", NULL);
    g_assert_cmpint (g_mkdir (fixture->child_directory, 0700), ==, 0);
    fixture->window = GTK_WINDOW (gtk_window_new ());
    fixture->panel = NAUTILUS_TERMINAL (nautilus_terminal_new ());
    gtk_window_set_default_size (fixture->window, 1000, 400);
    gtk_window_set_child (fixture->window, GTK_WIDGET (fixture->panel));
    fixture->tabs = ADW_TAB_VIEW (find_widget (GTK_WIDGET (fixture->panel), ADW_TYPE_TAB_VIEW));
    g_assert_nonnull (fixture->tabs);
    gtk_window_present (fixture->window);
}

static void
teardown (Fixture      *fixture,
          gconstpointer data)
{
    gtk_window_destroy (fixture->window);
    for (guint i = 0; i < 100; i++)
    {
        iterate ();
    }
    g_assert_cmpint (g_rmdir (fixture->child_directory), ==, 0);
    g_assert_cmpint (g_rmdir (fixture->directory), ==, 0);
    g_free (fixture->child_directory);
    g_free (fixture->directory);
}

static PtyxisTab *
get_tab (Fixture *fixture,
         guint    index)
{
    AdwTabPage *page = adw_tab_view_get_nth_page (fixture->tabs, index);

    g_assert_nonnull (page);
    g_assert_true (PTYXIS_IS_TAB (adw_tab_page_get_child (page)));
    return PTYXIS_TAB (adw_tab_page_get_child (page));
}

static void
assert_output (PtyxisTab  *tab,
               const char *command,
               const char *expected)
{
    VteTerminal *terminal = VTE_TERMINAL (ptyxis_tab_get_terminal (tab));
    gint64 deadline = g_get_monotonic_time () + 15 * G_TIME_SPAN_SECOND;

    while (ptyxis_tab_get_process (tab) == NULL && g_get_monotonic_time () < deadline)
    {
        iterate ();
    }
    g_assert_nonnull (ptyxis_tab_get_process (tab));
    vte_terminal_set_size (terminal, 300, 40);
    vte_terminal_feed_child (terminal, command, -1);
    while (g_get_monotonic_time () < deadline)
    {
        g_autofree char *text = vte_terminal_get_text (terminal, NULL, NULL, NULL);

        if (strstr (text, expected) != NULL)
        {
            return;
        }
        iterate ();
    }
    g_error ("Ptyxis terminal did not produce expected output: %s", expected);
}

static void
assert_directory (PtyxisTab  *tab,
                  const char *directory)
{
    g_autofree char *expected = g_strconcat ("__NAUTILUS_PWD__:", directory, NULL);

    assert_output (tab, "printf '__NAUTILUS_PWD__:%s\\n' \"$PWD\"\n", expected);
}

static void
test_navigation (Fixture      *fixture,
                 gconstpointer data)
{
    g_autoptr (GFile) first = g_file_new_for_path (fixture->directory);
    g_autoptr (GFile) second = g_file_new_for_path (fixture->child_directory);
    g_autoptr (GFile) remote = g_file_new_for_uri ("sftp://example.invalid/");
    PtyxisTab *original;

    nautilus_terminal_set_location (fixture->panel, first);
    g_assert_cmpint (adw_tab_view_get_n_pages (fixture->tabs), ==, 1);
    original = get_tab (fixture, 0);
    assert_directory (original, fixture->directory);
    nautilus_terminal_set_location (fixture->panel, first);
    g_assert_cmpint (adw_tab_view_get_n_pages (fixture->tabs), ==, 1);
    nautilus_terminal_set_location (fixture->panel, second);
    g_assert_cmpint (adw_tab_view_get_n_pages (fixture->tabs), ==, 2);
    assert_directory (get_tab (fixture, 1), fixture->child_directory);
    adw_tab_view_set_selected_page (fixture->tabs, adw_tab_view_get_nth_page (fixture->tabs, 0));
    assert_directory (original, fixture->directory);
    nautilus_terminal_set_location (fixture->panel, remote);
    g_assert_cmpint (adw_tab_view_get_n_pages (fixture->tabs), ==, 2);
    nautilus_terminal_set_location (fixture->panel, first);
    g_assert_cmpint (adw_tab_view_get_n_pages (fixture->tabs), ==, 3);
    assert_directory (get_tab (fixture, 2), fixture->directory);
}

static void
test_hidden (Fixture      *fixture,
             gconstpointer data)
{
    g_autoptr (GFile) location = g_file_new_for_path (fixture->directory);

    gtk_widget_set_visible (GTK_WIDGET (fixture->panel), FALSE);
    nautilus_terminal_set_location (fixture->panel, location);
    g_assert_cmpint (adw_tab_view_get_n_pages (fixture->tabs), ==, 0);
    gtk_widget_set_visible (GTK_WIDGET (fixture->panel), TRUE);
    nautilus_terminal_set_location (fixture->panel, location);
    assert_directory (get_tab (fixture, 0), fixture->directory);
}

static void
test_profile_and_agent (Fixture      *fixture,
                        gconstpointer data)
{
    g_autoptr (GFile) location = g_file_new_for_path (fixture->directory);
    g_autoptr (PtyxisProfile) profile = ptyxis_application_dup_default_profile (ptyxis_embed_get_application ());
    g_autofree char *fastfetch = g_find_program_in_path ("fastfetch");
    PtyxisTab *tab;
    GApplication *host = g_application_get_default ();
    GtkWidget *search;

    g_assert_false (PTYXIS_IS_APPLICATION (host));
    g_assert_cmpstr (g_application_get_application_id (host), ==, "org.gnome.Nautilus.TerminalTest");
    nautilus_terminal_set_location (fixture->panel, location);
    tab = get_tab (fixture, 0);
    g_assert_true (ptyxis_tab_get_profile (tab) == profile);
    g_assert_true (PTYXIS_IS_TERMINAL (ptyxis_tab_get_terminal (tab)));
    assert_directory (tab, fixture->directory);
    assert_output (tab, "printf '__HOST_GIO__:%s\\n' \"${GIO_MODULE_DIR-unset}\"\n", "__HOST_GIO__:unset");
    search = find_widget (GTK_WIDGET (fixture->panel), PTYXIS_TYPE_FIND_BAR);
    g_assert_nonnull (search);
    g_assert_true (ptyxis_find_bar_get_terminal (PTYXIS_FIND_BAR (search)) == ptyxis_tab_get_terminal (tab));

    if (fastfetch != NULL)
    {
        assert_output (tab, "fastfetch --structure Terminal --format json\n", "\"prettyName\": \"Ptyxis\"");
    }
}

static void
test_missing_directory (Fixture      *fixture,
                        gconstpointer data)
{
    g_autofree char *missing = g_build_filename (fixture->directory, "missing", NULL);
    g_autoptr (GFile) location = g_file_new_for_path (missing);

    g_test_expect_message (NULL, G_LOG_LEVEL_WARNING, "*Could not start inline Ptyxis:*");
    nautilus_terminal_set_location (fixture->panel, location);
    g_test_assert_expected_messages ();
    g_assert_cmpint (adw_tab_view_get_n_pages (fixture->tabs), ==, 0);
}

static void
test_close_pending (Fixture      *fixture,
                    gconstpointer data)
{
    g_autoptr (GFile) location = g_file_new_for_path (fixture->directory);
    PtyxisTab *tab;
    gint64 deadline = g_get_monotonic_time () + 15 * G_TIME_SPAN_SECOND;

    nautilus_terminal_set_location (fixture->panel, location);
    tab = get_tab (fixture, 0);
    g_object_add_weak_pointer (G_OBJECT (tab), (gpointer *) &tab);
    gtk_window_set_child (fixture->window, NULL);
    while (tab != NULL && g_get_monotonic_time () < deadline)
    {
        iterate ();
    }
    g_assert_null (tab);
}

static void
test_controls (Fixture      *fixture,
               gconstpointer data)
{
    g_autoptr (GFile) location = g_file_new_for_path (fixture->directory);
    PtyxisTab *tab;
    GtkWidget *terminal;
    GtkWidget *search;
    PtyxisPreferencesWindow *preferences;
    gboolean read_only;
    GtkWidget *header = gtk_widget_get_first_child (GTK_WIDGET (fixture->panel));
    GtkWidget *button = gtk_widget_get_next_sibling (gtk_widget_get_first_child (header));

    nautilus_terminal_set_location (fixture->panel, location);
    tab = get_tab (fixture, 0);
    assert_directory (tab, fixture->directory);
    terminal = GTK_WIDGET (ptyxis_tab_get_terminal (tab));
    g_assert_true (gtk_widget_activate_action (terminal, "win.search", NULL));
    search = find_widget (GTK_WIDGET (fixture->panel), PTYXIS_TYPE_FIND_BAR);
    g_assert_true (gtk_revealer_get_reveal_child (GTK_REVEALER (gtk_widget_get_parent (search))));
    g_assert_true (gtk_widget_activate_action (terminal, "win.tab.read-only", NULL));
    g_object_get (tab, "read-only", &read_only, NULL);
    g_assert_true (read_only);
    g_assert_true (gtk_widget_activate_action (terminal, "win.tab.read-only", NULL));

    preferences = ptyxis_preferences_window_get_default ();
    g_assert_true (PTYXIS_IS_PREFERENCES_WINDOW (preferences));
    {
        g_autoptr (AdwDialog) title = g_object_ref_sink (g_object_new (PTYXIS_TYPE_TITLE_DIALOG, "tab", tab, NULL));
        g_autoptr (AdwDialog) profile = g_object_ref_sink (g_object_new (PTYXIS_TYPE_PROFILE_DIALOG, "tab", tab, NULL));

        g_assert_true (PTYXIS_IS_TITLE_DIALOG (title));
        g_assert_true (PTYXIS_IS_PROFILE_DIALOG (profile));
    }
    g_signal_emit_by_name (button, "clicked");
    g_assert_cmpint (adw_tab_view_get_n_pages (fixture->tabs), ==, 2);
    assert_directory (get_tab (fixture, 1), fixture->directory);
    adw_tab_view_close_page (fixture->tabs, adw_tab_view_get_selected_page (fixture->tabs));
    g_assert_cmpint (adw_tab_view_get_n_pages (fixture->tabs), ==, 1);
}

static void
test_agent_failure (Fixture      *fixture,
                    gconstpointer data)
{
    g_autoptr (GError) failure = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CLOSED, "Test agent disconnect");
    g_autoptr (GError) error = NULL;
    g_autoptr (GFile) location = g_file_new_for_path (fixture->directory);

    g_test_expect_message (NULL, G_LOG_LEVEL_WARNING, "*Could not start inline Ptyxis:*");
    ptyxis_embed_set_error (failure);
    g_test_assert_expected_messages ();
    g_assert_false (ptyxis_embed_initialize (&error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CLOSED);
    nautilus_terminal_set_location (fixture->panel, location);
    g_assert_cmpint (adw_tab_view_get_n_pages (fixture->tabs), ==, 0);
    g_assert_false (PTYXIS_IS_APPLICATION (g_application_get_default ()));
}

int
main (int   argc,
      char *argv[])
{
    g_autoptr (GtkApplication) host = gtk_application_new ("org.gnome.Nautilus.TerminalTest",
                                                          G_APPLICATION_NON_UNIQUE);
    g_autoptr (GError) error = NULL;

    gtk_test_init (&argc, &argv, NULL);
    adw_init ();
    g_assert_true (g_application_register (G_APPLICATION (host), NULL, &error));
    g_assert_no_error (error);
    g_test_add ("/ptyxis/navigation", Fixture, NULL, setup, test_navigation, teardown);
    g_test_add ("/ptyxis/hidden", Fixture, NULL, setup, test_hidden, teardown);
    g_test_add ("/ptyxis/profile-and-agent", Fixture, NULL, setup, test_profile_and_agent, teardown);
    g_test_add ("/ptyxis/missing-directory", Fixture, NULL, setup, test_missing_directory, teardown);
    g_test_add ("/ptyxis/close-pending", Fixture, NULL, setup, test_close_pending, teardown);
    g_test_add ("/ptyxis/controls", Fixture, NULL, setup, test_controls, teardown);
    g_test_add ("/ptyxis/zz-agent-failure", Fixture, NULL, setup, test_agent_failure, teardown);

    return g_test_run ();
}
