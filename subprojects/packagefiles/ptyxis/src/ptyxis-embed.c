/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "config.h"
#include "ptyxis-embed.h"
#include <sys/resource.h>

static PtyxisApplication *embedded_application;
static GError *embedded_error;

void
ptyxis_embed_set_error (const GError *error)
{
    g_clear_error (&embedded_error);
    embedded_error = g_error_copy (error);
    g_signal_emit_by_name (embedded_application, "embed-error", embedded_error);
}

#ifdef PTYXIS_EMBEDDED
gint64
ptyxis_application_get_default_rlimit_nofile (void)
{
    struct rlimit limit;

    if (getrlimit (RLIMIT_NOFILE, &limit) != 0)
    {
        g_warning ("Could not read the terminal file descriptor limit");
        return -1;
    }

    return limit.rlim_cur == RLIM_INFINITY ? -1 : (gint64) limit.rlim_cur;
}
#endif

PtyxisApplication *
ptyxis_embed_get_application (void)
{
    if (embedded_application != NULL)
    {
        return embedded_application;
    }

    return PTYXIS_APPLICATION (g_application_get_default ());
}

gboolean
ptyxis_embed_initialize (GError **error)
{
    const char *standalone_actions[] = { "new-window", "new-tab", "focus-tab-by-uuid" };
    GSettingsSchemaSource *source = g_settings_schema_source_get_default ();
    g_autoptr (GSettingsSchema) schema = source == NULL ? NULL :
        g_settings_schema_source_lookup (source, APP_SCHEMA_ID, TRUE);

    if (embedded_error != NULL)
    {
        g_propagate_error (error, g_error_copy (embedded_error));
        return FALSE;
    }
    if (embedded_application != NULL)
    {
        return TRUE;
    }
    if (schema == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                     "Ptyxis settings schema %s is not installed", APP_SCHEMA_ID);
        return FALSE;
    }

    embedded_application = ptyxis_application_new ("org.gnome.Nautilus.Ptyxis",
                                                  G_APPLICATION_NON_UNIQUE);
    if (g_signal_lookup ("embed-error", PTYXIS_TYPE_APPLICATION) == 0)
    {
        g_signal_new ("embed-error", PTYXIS_TYPE_APPLICATION, G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_ERROR);
    }
    if (!g_application_register (G_APPLICATION (embedded_application), NULL, error))
    {
        g_clear_object (&embedded_application);
        return FALSE;
    }
    if (embedded_error != NULL)
    {
        g_propagate_error (error, g_error_copy (embedded_error));
        return FALSE;
    }

    for (guint i = 0; i < G_N_ELEMENTS (standalone_actions); i++)
    {
        GAction *action = g_action_map_lookup_action (G_ACTION_MAP (embedded_application),
                                                     standalone_actions[i]);

        g_simple_action_set_enabled (G_SIMPLE_ACTION (action), FALSE);
    }
    return TRUE;
}
