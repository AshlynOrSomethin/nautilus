/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define NAUTILUS_TYPE_TERMINAL (nautilus_terminal_get_type ())
G_DECLARE_FINAL_TYPE (NautilusTerminal, nautilus_terminal, NAUTILUS, TERMINAL, GtkBox)

GtkWidget *nautilus_terminal_new (void);
void nautilus_terminal_set_location (NautilusTerminal *self,
                                    GFile            *location);

G_END_DECLS
