/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "ptyxis-application.h"

gboolean ptyxis_embed_initialize (GError **error);
void ptyxis_embed_set_error (const GError *error);
