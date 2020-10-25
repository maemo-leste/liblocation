#include <gmodule.h>

#include "location-misc.h"

void location_make_resident()
{
	GModule *module;
	const gchar *err;

	/* TODO: See if there is some define for the correct location in
	 * autoconf */
	module = g_module_open("/usr/lib/liblocation.so.0", 0);
	if (!module) {
		err = g_module_error();
		g_log("liblocation", G_LOG_LEVEL_CRITICAL,
				"%s: g_module_open() failed: %s",
				"location_make_resident", err);
	}

	g_module_make_resident(module);
}
