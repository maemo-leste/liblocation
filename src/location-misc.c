/*
 * Copyright (c) 2020 Ivan J. <parazyd@dyne.org>
 *
 * This file is part of liblocation
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <gmodule.h>

#include "location-misc.h"

void location_make_resident()
{
	gchar *module_path;
	const gchar *err;

	module_path = g_module_build_path(NULL, "liblocation");
	if (!module_path)
		return;

	GModule *module = g_module_open(module_path, 0);
	if (!module) {
		err = g_module_error();
		g_log("liblocation", G_LOG_LEVEL_CRITICAL,
				"%s: g_module_open() failed: %s",
				"location_make_resident", err);
	}

	g_module_make_resident(module);
}
