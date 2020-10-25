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
