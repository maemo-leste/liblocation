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

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <gconf/gconf-client.h>
#include <glib.h>

#include "location-gpsd-control.h"

#define GCONF_LOC                  "/system/nokia/location"
#define GCONF_METHOD               GCONF_LOC"/method"
#define GCONF_GPS_DISABLED         GCONF_LOC"/gps-disabled"
#define GCONF_NET_DISABLED         GCONF_LOC"/network-disabled"
#define GCONF_DISCLAIMER_ACCEPTED  GCONF_LOC"/disclaimer-accepted"

typedef enum {
	METHOD = 1,
	INTERVAL,
	MAINCONTEXT,
	LAST_PROP
} ControlClassProperty;

enum {
	ERROR,
	ERROR_VERBOSE,
	GPSD_RUNNING,
	GPSD_STOPPED,
	LAST_SIGNAL
};

typedef struct _gypsy_dbus_proxy
{
	DBusGProxy *server;
	DBusGProxy *device;
	const char *path;
} gypsy_dbus_proxy;

struct _LocationGPSDControlPrivate
{
	GMainContext *ctx;
	GConfClient *gconf;
	gypsy_dbus_proxy gypsy;
	DBusGConnection *dbus;
	DBusGProxy *location_ui_proxy;
	DBusGProxy *cdr_method;
	gchar *device;
	gchar *device_car;
	guint client_id;
	gboolean gps_disabled;
	gboolean net_disabled;
	gboolean disclaimer_accepted;
	gboolean ui_open;
	gboolean is_running;
	int sel_method;
	int interval;
	gboolean field_48;
	gchar *mce_device_mode;
	GSource *gsource_mainloop;
	gboolean gpsd_running;
};

static guint signals[LAST_SIGNAL] = {};
static GParamSpec *obj_properties[LAST_PROP] = {};

G_DEFINE_TYPE_WITH_PRIVATE(LocationGPSDControl, location_gpsd_control, G_TYPE_OBJECT);

/* function declarations */
static gboolean gconf_get_bool(gboolean *, const GConfValue *);
static int get_selected_method(LocationGPSDControlPrivate *, int);
static int get_selected_method_wrap(LocationGPSDControlPrivate *);
static int main_loop(void);
static void device_mode_changed_cb(int, gchar *, GObject *);
static void enable_gps_and_disclaimer(int, int, GObject *);
static void enable_gps_and_supl(int, int, GObject *);
static void get_location_method(LocationGPSDControl *, const GConfValue *);
static void location_gpsd_control_class_dispose(GObject *);
static void location_gpsd_control_class_get_property(GObject *, guint, GValue *, GParamSpec *);
static void location_gpsd_control_class_set_property(GObject *, guint, const GValue *, GParamSpec *);
static void location_gpsd_control_class_init(LocationGPSDControlClass *);
static void location_gpsd_control_init(LocationGPSDControl *);
static void location_gpsd_control_prestart_internal(LocationGPSDControl *, int);
static void location_gpsd_control_start_internal(LocationGPSDControl *, int, GError **);
static void on_gconf_changed(GConfClient *, guint, GConfEntry *, GObject *);
static void on_positioning_activate_response(int, int, GObject *);
static void register_dbus_signal_callback(LocationGPSDControl *, const char *, void (*)(void), GError **);
static void toggle_gps(int, int, GObject *);
static void toggle_network(int, int, GObject *);
static void ui_proxy_close(LocationGPSDControlPrivate *);

void ui_proxy_close(LocationGPSDControlPrivate *priv)
{
	if (priv->location_ui_proxy) {
		if (priv->ui_open) {
			dbus_g_proxy_call(priv->location_ui_proxy, "close", NULL,
					G_TYPE_INVALID);
			priv->ui_open = FALSE;
		}
		g_object_unref(priv->location_ui_proxy);
		priv->location_ui_proxy = NULL;
	}
}

int get_selected_method(LocationGPSDControlPrivate *priv, int method)
{
	int v3, v4, v5, result;

	if (method) {
		v3 = LOCATION_METHOD_ACWP;
		v4 = LOCATION_METHOD_GNSS;
		v5 = LOCATION_METHOD_AGNSS;
		result = 1;
		goto out;
	}

	result = method & LOCATION_METHOD_CWP;

	if (method & LOCATION_METHOD_CWP) {
		v3 = method & LOCATION_METHOD_ACWP;
		v4 = method & LOCATION_METHOD_GNSS;
		v5 = method & LOCATION_METHOD_AGNSS;
		result = 1;
		goto out;
	}

	v3 = method & LOCATION_METHOD_ACWP;
	v4 = method & LOCATION_METHOD_GNSS;
	v5 = method & LOCATION_METHOD_AGNSS;

out:
	if (v3 && !priv->net_disabled)
		result |= LOCATION_METHOD_ACWP;
	if (v4 && !priv->gps_disabled)
		result |= LOCATION_METHOD_GNSS;
	if (v5 && !priv->net_disabled && !priv->gps_disabled)
		result |= LOCATION_METHOD_AGNSS;

	return result;
}

int get_selected_method_wrap(LocationGPSDControlPrivate *priv)
{
	return get_selected_method(priv, priv->sel_method);
}

void on_positioning_activate_response(int unused, int a2, GObject *object)
{
	LocationGPSDControlPrivate *priv;

	if (!LOCATION_IS_GPSD_CONTROL(object))
		g_assert("LOCATION_IS_GPSD_CONTROL(object)");

	priv = location_gpsd_control_get_instance_private(LOCATION_GPSD_CONTROL(object));

	/* TODO: Simplify */

	if (a2 & 1) {
		g_debug("%s: %d", G_STRFUNC, a2);
		priv->gps_disabled = FALSE;
		gconf_client_set_bool(priv->gconf, GCONF_GPS_DISABLED, FALSE, NULL);
		if (!(a2 & 2))
			goto lab3;
	} else if (!(a2 & 2))
		goto lab3;

	g_debug("%s: %d", G_STRFUNC, a2);
	priv->net_disabled = FALSE;
	gconf_client_set_bool(priv->gconf, GCONF_NET_DISABLED, FALSE, NULL);
lab3:
	if (get_selected_method_wrap(priv)) {
		location_gpsd_control_prestart_internal(
				LOCATION_GPSD_CONTROL(object), FALSE);
	} else {
		g_signal_emit(LOCATION_GPSD_CONTROL(object),
				signals[ERROR], 0);
		g_signal_emit(LOCATION_GPSD_CONTROL(object),
				signals[ERROR_VERBOSE], 0,
				LOCATION_ERROR_USER_REJECTED_DIALOG);
	}

	ui_proxy_close(priv);
}

void register_dbus_signal_callback(LocationGPSDControl *control,
		const char *path, void (*handler_func)(void), GError **error)
{
	LocationGPSDControlPrivate *priv;
	DBusGProxy *new_proxy;
	GQuark quark;
	const char *err_name;
	int response;
	GError *ierr1 = NULL;

	priv = location_gpsd_control_get_instance_private(control);

	if (!priv->location_ui_proxy) {
		new_proxy = dbus_g_proxy_new_for_name(priv->dbus,
				"com.nokia.Location.UI", path,
				"com.nokia.Location.UI.Dialog");
		priv->location_ui_proxy = new_proxy;

		dbus_g_proxy_add_signal(priv->location_ui_proxy,
				"response", G_TYPE_INT, G_TYPE_INVALID);
		dbus_g_proxy_connect_signal(priv->location_ui_proxy,
				"response", handler_func, control, NULL);
		dbus_g_proxy_call(priv->location_ui_proxy,
				"display", &ierr1, G_TYPE_INVALID);

		if (ierr1) {
			quark = dbus_g_error_quark();
			if (g_error_matches(ierr1, quark, 32)
					&& (err_name = dbus_g_error_get_name(ierr1),
						g_str_equal(err_name,
							"com.nokia.Location.UI.Error.InUse"))) {
				response = strtol(ierr1->message, NULL, 10);
				g_message("%s already active, current response = %d",
						path, response);
				if (response >= 0) {
					((void (*)(DBusGProxy *, int, LocationGPSDControl*))handler_func)(
						priv->location_ui_proxy,
						response,
						control);
				}
				g_error_free(ierr1);
			} else {
				g_propagate_error(error, ierr1);
			}
		} else {
			priv->ui_open = TRUE;
		}
	}
}

void enable_gps_and_disclaimer(int unused, int a2, GObject *object)
{
	LocationGPSDControlPrivate *priv;

	if (!LOCATION_IS_GPSD_CONTROL(object))
		g_assert("LOCATION_IS_GPSD_CONTROL(object)");

	priv = location_gpsd_control_get_instance_private(LOCATION_GPSD_CONTROL(object));

	priv->disclaimer_accepted = a2 == 0;
	gconf_client_set_bool(priv->gconf, GCONF_DISCLAIMER_ACCEPTED,
			priv->disclaimer_accepted, NULL);

	if (priv->disclaimer_accepted) {
		if (priv->gps_disabled) {
			priv->gps_disabled = FALSE;
			gconf_client_set_bool(priv->gconf, GCONF_GPS_DISABLED,
					FALSE, NULL);
			if (priv->sel_method != LOCATION_METHOD_AGNSS) /* != &byte_8 */
				goto out;
		} else if (priv->sel_method != LOCATION_METHOD_AGNSS) { /* != &byte_8 */
out:
			location_gpsd_control_prestart_internal(
					LOCATION_GPSD_CONTROL(object), TRUE);
			ui_proxy_close(priv);
			return;
		}

		if (priv->net_disabled) {
			priv->net_disabled = FALSE;
			gconf_client_set_bool(priv->gconf, GCONF_NET_DISABLED,
					FALSE, NULL);
		}
		goto out;
	}
	g_signal_emit(LOCATION_GPSD_CONTROL(object),
			signals[ERROR], 0);
	g_signal_emit(LOCATION_GPSD_CONTROL(object),
			signals[ERROR_VERBOSE], 0,
			LOCATION_ERROR_USER_REJECTED_DIALOG);
	ui_proxy_close(priv);
}

void enable_gps_and_supl(int unused, int a2, GObject *object)
{
	LocationGPSDControlPrivate *priv;

	if (!LOCATION_IS_GPSD_CONTROL(object))
		g_assert("LOCATION_IS_GPSD_CONTROL(object)");

	priv = location_gpsd_control_get_instance_private(LOCATION_GPSD_CONTROL(object));

	if (!a2) {
		priv->gps_disabled = FALSE;
		priv->net_disabled = FALSE;
		gconf_client_set_bool(priv->gconf, GCONF_GPS_DISABLED, FALSE, NULL);
		gconf_client_set_bool(priv->gconf, GCONF_NET_DISABLED, FALSE, NULL);
	}

	if (get_selected_method_wrap(priv)) {
		location_gpsd_control_prestart_internal(
				LOCATION_GPSD_CONTROL(object), FALSE);
	} else {
		g_signal_emit(LOCATION_GPSD_CONTROL(object),
				signals[ERROR], 0);
		g_signal_emit(LOCATION_GPSD_CONTROL(object),
				signals[ERROR_VERBOSE], 0,
				LOCATION_ERROR_USER_REJECTED_DIALOG);
	}

	ui_proxy_close(priv);
}

void toggle_network(int unused, int state, GObject *object)
{
	LocationGPSDControlPrivate *priv;

	if (!LOCATION_IS_GPSD_CONTROL(object))
		g_assert("LOCATION_IS_GPSD_CONTROL(object)");

	priv = location_gpsd_control_get_instance_private(LOCATION_GPSD_CONTROL(object));

	priv->net_disabled = state != 0;
	gconf_client_set_bool(priv->gconf, GCONF_NET_DISABLED,
			priv->net_disabled, NULL);

	if (priv->net_disabled && !get_selected_method_wrap(priv)) {
		g_signal_emit(LOCATION_GPSD_CONTROL(object),
				signals[ERROR], 0);
		g_signal_emit(LOCATION_GPSD_CONTROL(object),
				signals[ERROR_VERBOSE], 0,
				LOCATION_ERROR_USER_REJECTED_DIALOG);
	} else {
		location_gpsd_control_prestart_internal(
				LOCATION_GPSD_CONTROL(object), FALSE);
	}

	ui_proxy_close(priv);
}

void toggle_gps(int unused, int state, GObject *object)
{
	LocationGPSDControlPrivate *priv;

	if (!LOCATION_IS_GPSD_CONTROL(object))
		g_assert("LOCATION_IS_GPSD_CONTROL(object)");

	priv = location_gpsd_control_get_instance_private(LOCATION_GPSD_CONTROL(object));

	priv->gps_disabled = state != 0;
	gconf_client_set_bool(priv->gconf, GCONF_GPS_DISABLED,
			priv->gps_disabled, NULL);

	if (priv->gps_disabled && !get_selected_method_wrap(priv)) {
		g_signal_emit(LOCATION_GPSD_CONTROL(object),
				signals[ERROR], 0);
		g_signal_emit(LOCATION_GPSD_CONTROL(object),
				signals[ERROR_VERBOSE], 0,
				LOCATION_ERROR_USER_REJECTED_DIALOG);
	} else {
		location_gpsd_control_prestart_internal(
				LOCATION_GPSD_CONTROL(object), FALSE);
	}

	ui_proxy_close(priv);
}

void location_gpsd_control_start_internal(
		LocationGPSDControl *control, int unsure_ui_related, GError **error)
{
	LocationGPSDControlPrivate *priv;
	gchar *mce_device_mode;
	// gchar *device;
	GHashTable *hash_table;
	DBusGProxy *proxy;
	GQuark quark;
	GError *ierr1 = NULL;
	GType type, hashtable_type;
	gboolean v21;
	const GValue *v27;
	GValue value;
	int method;
	//int interval;

	if (!LOCATION_IS_GPSD_CONTROL(control))
		g_assert("LOCATION_IS_GPSD_CONTROL(control)");

	priv = location_gpsd_control_get_instance_private(control);

	// gypsy running; then return
	if (priv->gpsd_running)
		return;

	if (!priv->disclaimer_accepted) {
		if (unsure_ui_related) {
			if (!priv->location_ui_proxy)
				register_dbus_signal_callback(control,
						"/com/nokia/location/ui/disclaimer",
						(void (*)(void))enable_gps_and_disclaimer,
						error);
			return;
		}

lab46:
		quark = g_quark_from_static_string("location-error-quark");
		g_set_error(error, quark, 2, "Use denied by settings");
		return;
	}

	mce_device_mode = priv->mce_device_mode;
	method = priv->sel_method;

	if (!mce_device_mode) {
		mce_device_mode = NULL;
		proxy = dbus_g_proxy_new_for_name(priv->dbus, "com.nokia.mce",
				"/com/nokia/mce/request", "com.nokia.mce.request");
		dbus_g_proxy_call(proxy, "get_device_mode",
				&ierr1, G_TYPE_INVALID,
				G_TYPE_STRING, &mce_device_mode,
				G_TYPE_INVALID);
		g_object_unref(proxy);

		if (ierr1) {
			g_warning("%s: %s", "update_mce_device_mode", ierr1->message);
			g_error_free(ierr1);
			g_signal_emit(control, signals[ERROR], 0);
			g_signal_emit(control, signals[ERROR_VERBOSE], 0,
					LOCATION_ERROR_SYSTEM);
			return;
		}

		if (priv->mce_device_mode) {
			g_free(priv->mce_device_mode);
			priv->mce_device_mode = NULL;
		}
		priv->mce_device_mode = mce_device_mode;
	}

	if (g_strcmp0(priv->mce_device_mode, "flight")
			&& g_strcmp0(priv->mce_device_mode, "offline")) {
		if (!method) {
			if (!priv->net_disabled) {
				/* TODO: Review if used defines are right */
				if (priv->gps_disabled)
					method = LOCATION_METHOD_ACWP; /* 2 */
				else
					method = LOCATION_METHOD_ACWP|LOCATION_METHOD_AGNSS; /* 10 */
				goto lab22;
			}

			if (!priv->gps_disabled) {
				method = LOCATION_METHOD_GNSS; /* 4 */
				goto lab22;
			}

			if (!unsure_ui_related) {
				method = LOCATION_METHOD_CWP; /* 1 */
				goto lab22;
			}
lab43:
			register_dbus_signal_callback(control,
					"/com/nokia/location/ui/enable_positioning",
					(void (*)(void))on_positioning_activate_response, error);
			return;
		}
	} else {
		g_debug("%s: We are in offline mode now!", G_STRLOC);

		if (g_strcmp0(priv->device, "las")) {
			register_dbus_signal_callback(control,
					"/com/nokia/location/ui/bt_disabled", NULL, NULL);
			g_warning("%s: Offline mode, external device, giving up.",
					G_STRLOC);
			g_signal_emit(control, signals[ERROR], 0);
			g_signal_emit(control, signals[ERROR_VERBOSE], 0,
					LOCATION_ERROR_METHOD_NOT_ALLOWED_IN_OFFLINE_MODE);
			return;
		}

		if (method && !(method & (LOCATION_METHOD_GNSS|LOCATION_METHOD_AGNSS))) { /* 0xC */
			g_warning("%s: The method requested is not appropriate for offline mode",
					G_STRLOC);
			g_signal_emit(control, signals[ERROR], 0);
			g_signal_emit(control, signals[ERROR_VERBOSE], 0,
					LOCATION_ERROR_METHOD_NOT_ALLOWED_IN_OFFLINE_MODE);
			return;
		}

		method = LOCATION_METHOD_GNSS; /* 4 */
	}

	g_debug("%s: application method set to 0x%x", G_STRLOC, method);

	if (unsure_ui_related) {
		if ((method & 6) == 6 && priv->net_disabled && priv->gps_disabled) {
			if (priv->location_ui_proxy)
				return;
			goto lab43;
		}

		if (!(method & 8))
			goto lab96;

		if (priv->net_disabled) {
			if (priv->gps_disabled) {
				if (priv->location_ui_proxy)
					return;

				if ((unsigned int)(method - 8) <= 1) {
					register_dbus_signal_callback(control,
							"/com/nokia/location/ui/enable_agnss",
							(void (*)(void))enable_gps_and_supl,
							error);
					return;
				}
				goto lab43;
			}
lab60:
			if (!priv->location_ui_proxy)
				register_dbus_signal_callback(control,
						"/com/nokia/location/ui/enable_network",
						(void (*)(void))toggle_network,
						error);
			return;
		}

		if (!priv->gps_disabled) {
lab96:
			if (!(method & 4) || !priv->gps_disabled) {
				if (!(method & 2) || !priv->net_disabled)
					goto lab22;
				goto lab60;
			}
		}

		if (!priv->location_ui_proxy)
			register_dbus_signal_callback(control,
					"/com/nokia/location/ui/enable_gps",
					(void (*)(void))toggle_gps,
					error);
		return;
	}

	method &= get_selected_method_wrap(priv);
	if (!method)
		goto lab46;

lab22:
	if (!LOCATION_IS_GPSD_CONTROL(control))
		g_assert("LOCATION_IS_GPSD_CONTROL(control)");

	// interval = priv->interval;

	if ((unsigned int)(method - 1) <= 2 || !g_strcmp0(priv->device, "las")) {
		// gypsy proxy here
		//if (!dbus_proxy_setup(control, method, interval, error))
			//return;
		priv->field_48 = FALSE;
		priv->gpsd_running = TRUE;
		goto lab48;
	}

	mce_device_mode = NULL;
	hash_table = NULL;

	proxy = dbus_g_proxy_new_for_name(priv->dbus, "org.blues", "/",
			"org.bluez.Manager");
	type = dbus_g_object_path_get_g_type();
	if (!dbus_g_proxy_call(proxy, "DefaultAdapter", &ierr1,
				G_TYPE_INVALID, type, &mce_device_mode,
				G_TYPE_INVALID)) {
		if (ierr1) {
			g_warning("Error getting Bluetooth adapter: %s", ierr1->message);
			g_error_free(ierr1);
		}

		g_object_unref(proxy);
lab31:
		register_dbus_signal_callback(control,
				"/com/nokia/location/ui/bt_disabled", NULL, NULL);
		quark = g_quark_from_static_string("locatioin-error-quark");
		g_set_error(error, quark, 4, "Bluetooth not available");
		return;
	}
	g_object_unref(proxy);

	proxy = dbus_g_proxy_new_for_name(priv->dbus, "org.bluez",
			mce_device_mode, "org.bluez.Adapter");
	type = g_value_get_type();
	hashtable_type = dbus_g_type_get_map("GHashTable", G_TYPE_STRING, type);
	v21 = dbus_g_proxy_call(proxy, "GetProperties", &ierr1,
			G_TYPE_INVALID, hashtable_type, &hash_table,
			G_TYPE_INVALID);
	if (!v21) {
		if (ierr1) {
lab75:
			g_warning("Error getting Bluetooth adapter properties: %s",
					ierr1->message);
			g_error_free(ierr1);
			goto lab76;
		}
		goto lab82;
	}

	if (!hash_table
			|| (v27 = (const GValue *)g_hash_table_lookup(hash_table, "Powered")) == 0
			|| (v21 = g_value_get_boolean(v27)) == 0) {
		g_debug("Bluetooth adapter status: %s", "not powered");

		if (g_strcmp0(priv->mce_device_mode, "flight")
				&& g_strcmp0(priv->mce_device_mode, "offline")) {
			/* TODO: Sort this out */
			value.g_type = 0;
			*(&value.g_type + 1) = 0;
			value.data[0].v_int64 = 0LL;
			value.data[1].v_int64 = 0LL;
			g_value_init(&value, 0x14u);
			g_value_set_boolean(&value, TRUE);

			v21 = dbus_g_proxy_call(proxy, "SetProperty", &ierr1,
					G_TYPE_STRING, "Powered",
					type, &value,
					G_TYPE_INVALID, G_TYPE_INVALID);
			if (v21) {
				v21 = TRUE;
				goto lab76;
			}

			if (ierr1)
				goto lab75;
		}
lab82:
		v21 = FALSE;
		goto lab76;
	}

	g_debug("Bluetooth adapter status: %s", "powered");

lab76:
	if (hash_table)
		g_hash_table_unref(hash_table);

	if (mce_device_mode)
		g_free(mce_device_mode);

	if (proxy)
		g_object_unref(proxy);

	if (!v21)
		goto lab31;

	// gypsy proxy here

	priv->field_48 = TRUE;

lab48:
	if (!priv->is_running) {
		g_signal_emit(control, signals[GPSD_RUNNING], 0);
		priv->is_running = TRUE;
	}
	return;
}

void location_gpsd_control_prestart_internal(
		LocationGPSDControl *control, int unsure_ui_related)
{
	GError *error = NULL;

	location_gpsd_control_start_internal(control, unsure_ui_related, &error);

	if (error) {
		g_warning("%s: %s", G_STRFUNC, error->message);
		g_error_free(error);
		g_signal_emit(control, signals[ERROR], 0);
		g_signal_emit(control, signals[ERROR_VERBOSE], 0,
				LOCATION_ERROR_SYSTEM);
	}
}

int main_loop()
{
	while (g_main_context_pending(NULL))
		g_main_context_iteration(NULL, FALSE);

	return 1;
}

void location_gpsd_control_start(LocationGPSDControl *control)
{
	LocationGPSDControlPrivate *priv;

	if (!LOCATION_IS_GPSD_CONTROL(control))
		g_assert("LOCATION_IS_GPSD_CONTROL(control)");

	priv = location_gpsd_control_get_instance_private(control);

	if (priv->ctx) {
		priv->gsource_mainloop = g_timeout_source_new(1000);
		g_source_set_callback(priv->gsource_mainloop, (GSourceFunc)main_loop,
				control, NULL);
		g_source_attach(priv->gsource_mainloop, priv->ctx);
	}

	priv->is_running = FALSE;
	location_gpsd_control_prestart_internal(control, TRUE);
}

void location_gpsd_control_stop(LocationGPSDControl *control)
{
	LocationGPSDControlPrivate *priv;

	if (!LOCATION_IS_GPSD_CONTROL(control))
		g_assert("LOCATION_IS_GPSD_CONTROL(control)");

	priv = location_gpsd_control_get_instance_private(control);

	if (priv->ctx) {
		if (!g_source_is_destroyed(priv->gsource_mainloop))
			g_source_destroy(priv->gsource_mainloop);
	}

	// gypsy proxy here
	if (priv->gpsd_running) {
		priv->gpsd_running = FALSE;
		g_signal_emit(control, signals[GPSD_STOPPED], 0);
	}

	ui_proxy_close(priv);
}

void location_gpsd_control_request_status(LocationGPSDControl *control)
{
	g_warning("You don't need to call %s, it does nothing anymore!",
			G_STRFUNC);
}

gint location_gpsd_control_get_allowed_methods(LocationGPSDControl *control)
{
	LocationGPSDControlPrivate *priv;

	if (!LOCATION_IS_GPSD_CONTROL(control))
		g_assert("LOCATION_IS_GPSD_CONTROL(control)");

	priv = location_gpsd_control_get_instance_private(control);

	return get_selected_method(priv, 0);
}

void get_location_method(LocationGPSDControl *control,
		const GConfValue *value)
{
	LocationGPSDControlPrivate *priv;
	GConfValue *car_ret, *cdr_ret;
	const gchar *cdr_str, *car_str;
	gboolean stopped = FALSE;

	priv = location_gpsd_control_get_instance_private(control);

	// gypsy proxy here
	if (priv->gpsd_running) {
		// dbus_proxy_shutdown
		priv->gpsd_running = FALSE;
		stopped = TRUE;
	}

	if (priv->device != NULL) {
		g_free(priv->device);
		priv->device = NULL;
	}

	if (priv->device_car) {
		g_free(priv->device_car);
		priv->device_car = NULL;
	}

	if (!value) {
		if (stopped)
			g_signal_emit(control, signals[GPSD_STOPPED], 0);
		return;
	}

	car_ret = gconf_value_get_car(value);
	cdr_ret = gconf_value_get_cdr(value);

	if (car_ret) {
		if (!cdr_ret || car_ret->type != GCONF_VALUE_STRING
				|| cdr_ret->type != GCONF_VALUE_STRING) {
			if (stopped)
				g_signal_emit(control, signals[GPSD_STOPPED], 0);
		} else {
			cdr_str = gconf_value_get_string(cdr_ret);
			g_debug("cdr_str: %s", cdr_str);
			priv->device = g_strdup(cdr_str);
			car_str = gconf_value_get_string(car_ret);
			g_debug("car_str: %s", car_str);
			priv->device_car = g_strdup(car_str);

			if (stopped)
				location_gpsd_control_prestart_internal(control, TRUE);
		}
	}
}

void device_mode_changed_cb(int unused, gchar *sig, GObject *object)
{
	LocationGPSDControlPrivate *priv;
	gchar *mce_device_mode, *tmp;

	if (!LOCATION_IS_GPSD_CONTROL(object))
		g_assert("LOCATION_IS_GPSD_CONTROL(object)");

	priv = location_gpsd_control_get_instance_private(LOCATION_GPSD_CONTROL(object));

	mce_device_mode = priv->mce_device_mode;
	g_debug("old mce_device_mode: %s", priv->mce_device_mode);
	tmp = g_strdup(sig);
	priv->mce_device_mode = tmp;
	g_debug("new mce_device_mode: %s", priv->mce_device_mode);

	// gypsy proxy here
	if (priv->gpsd_running && g_strcmp0(mce_device_mode, priv->mce_device_mode)) {
		// dbus_proxy_shutdown
		priv->gpsd_running = FALSE;
		location_gpsd_control_prestart_internal(LOCATION_GPSD_CONTROL(object),
				TRUE);
	}

	g_free(mce_device_mode);
}

gboolean gconf_get_bool(gboolean *dest, const GConfValue *value)
{
	gboolean cur, ret, unchanged;

	if (value)
		cur = gconf_value_get_bool(value);
	else
		cur = FALSE;

	ret = *dest;
	unchanged = cur == *dest;

	if (unchanged)
		ret = FALSE;
	else
		*dest = cur;

	if (!unchanged)
		ret = TRUE;

	return ret;
}

void on_gconf_changed(GConfClient *client, guint cnxn_id,
		GConfEntry *entry, GObject *object)
{
	LocationGPSDControlPrivate *priv;
	gboolean tmp;
	GError *err = NULL;
	GQuark quark;

	if (!entry || !entry->key)
		g_assert("entry && entry->key");

	if (!LOCATION_IS_GPSD_CONTROL(object))
		g_assert("LOCATION_IS_GPSD_CONTROL(object)");

	priv = location_gpsd_control_get_instance_private(LOCATION_GPSD_CONTROL(object));

	if (g_str_equal(entry->key, GCONF_METHOD)) {
		get_location_method(LOCATION_GPSD_CONTROL(object), entry->value);
		return;
	} else if (g_str_equal(entry->key, GCONF_GPS_DISABLED))
		tmp = gconf_get_bool(&priv->gps_disabled, entry->value);
	else if (g_str_equal(entry->key, GCONF_NET_DISABLED))
		tmp = gconf_get_bool(&priv->net_disabled, entry->value);
	else if (g_str_equal(entry->key, GCONF_DISCLAIMER_ACCEPTED))
		tmp = gconf_get_bool(&priv->disclaimer_accepted, entry->value);
	else
		return;

	if (tmp) {
		// gypsy proxy here
		if (priv->gpsd_running) {
			// dbus_proxy_shutdown
			priv->gpsd_running = FALSE;
			location_gpsd_control_start_internal(LOCATION_GPSD_CONTROL(object),
					FALSE, &err);

			if (err) {
				g_signal_emit(LOCATION_GPSD_CONTROL(object),
						signals[ERROR], 0);
				quark = g_quark_from_static_string("location-error-quark");

				// LocationGPSDControlError in g-error-matches?
				if (g_error_matches(err, quark, 2)) {
					g_signal_emit(LOCATION_GPSD_CONTROL(object),
							signals[ERROR_VERBOSE], 0,
							LOCATION_ERROR_USER_REJECTED_SETTINGS);
				} else {
					g_signal_emit(LOCATION_GPSD_CONTROL(object),
							signals[ERROR_VERBOSE], 0,
							LOCATION_ERROR_SYSTEM);
				}

				g_warning("Failed to update connection: %s", err->message);
				g_error_free(err);
				g_signal_emit(LOCATION_GPSD_CONTROL(object),
						signals[GPSD_STOPPED], 0);
		}
		}
	}
}

void location_gpsd_control_class_dispose(GObject *object)
{
	LocationGPSDControlPrivate *priv;

	if (!LOCATION_IS_GPSD_CONTROL(object))
		g_assert("LOCATION_IS_GPSD_CONTROL(object)");

	priv = location_gpsd_control_get_instance_private(LOCATION_GPSD_CONTROL(object));

	if (priv->gconf) {
		gconf_client_notify_remove(priv->gconf, priv->client_id);
		g_object_unref(priv->gconf);
		priv->gconf = NULL;
	}
	location_gpsd_control_stop(LOCATION_GPSD_CONTROL(object));

	if (priv->device) {
		g_free(priv->device);
		priv->device = NULL;
	}

	if (priv->device_car) {
		g_free(priv->device_car);
		priv->device_car = NULL;
	}

	if (priv->cdr_method) {
		dbus_g_proxy_disconnect_signal(priv->cdr_method, "sig_device_mode_ind",
				(GCallback)device_mode_changed_cb,
				LOCATION_GPSD_CONTROL(object));
		g_object_unref(priv->cdr_method);
		priv->cdr_method = NULL;
	}

	if (priv->dbus) {
		dbus_g_connection_unref(priv->dbus);
		priv->dbus = NULL;
	}

	if (priv->gconf)
		g_assert("priv->gconf == NULL");

	if (priv->dbus)
		g_assert("priv->dbus == NULL");

	if (priv->location_ui_proxy)
		g_assert("priv->location_ui_proxy == NULL");

	if (priv->device)
		g_assert("priv->device == NULL");
}

void location_gpsd_control_class_set_property(GObject *object,
		guint property_id, const GValue *value, GParamSpec *pspec)
{
	LocationGPSDControlPrivate  *priv;
	const gchar *list, *klass_type, *parent_type;

	if (!LOCATION_IS_GPSD_CONTROL(object))
		g_assert("LOCATION_IS_GPSD_CONTROL(object)");

	priv = location_gpsd_control_get_instance_private(LOCATION_GPSD_CONTROL(object));

	g_debug("SET_PROPERTY REACHED");

	switch ((ControlClassProperty)property_id) {
	case METHOD:
		priv->sel_method = g_value_get_int(value);
		break;
	case INTERVAL:
		priv->interval = g_value_get_int(value);
		break;
	case MAINCONTEXT:
		priv->ctx = g_value_get_pointer(value);
		break;
	default:
		list = pspec->name;
		klass_type = g_type_name(pspec->g_type_instance.g_class->g_type);
		parent_type = g_type_name(object->g_type_instance.g_class->g_type);
		g_warning("invalid property id %u for \"%s\" of type `%s' in `%s'",
				property_id, list, klass_type, parent_type);
		break;
	}
}

void location_gpsd_control_class_get_property(GObject *object,
		guint property_id, GValue *value, GParamSpec *pspec)
{
	LocationGPSDControlPrivate *priv;
	const gchar *list, *klass_type, *parent_type;

	if (!LOCATION_IS_GPSD_CONTROL(object))
		g_assert("LOCATION_IS_GPSD_CONTROL(object)");

	priv = location_gpsd_control_get_instance_private(LOCATION_GPSD_CONTROL(object));

	switch ((ControlClassProperty)property_id) {
	case METHOD:
		g_value_set_int(value, priv->sel_method);
		break;
	case INTERVAL:
		g_value_set_int(value, priv->interval);
		break;
	case MAINCONTEXT:
		break;
	default:
		list = pspec->name;
		klass_type = g_type_name(pspec->g_type_instance.g_class->g_type);
		parent_type = g_type_name(object->g_type_instance.g_class->g_type);
		g_warning("invalid property id %u for \"%s\" of type `%s' in `%s'",
				property_id, list, klass_type, parent_type);
		break;
	}
}

void location_gpsd_control_class_init(LocationGPSDControlClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->dispose = location_gpsd_control_class_dispose;
	object_class->set_property = location_gpsd_control_class_set_property;
	object_class->get_property = location_gpsd_control_class_get_property;

	signals[ERROR] = g_signal_new("error",
			G_TYPE_FROM_CLASS(klass),
			G_SIGNAL_NO_RECURSE|G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET(LocationGPSDControlClass, error),
			0, NULL, g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);

	signals[ERROR_VERBOSE] = g_signal_new("error-verbose",
			G_TYPE_FROM_CLASS(klass),
			G_SIGNAL_NO_RECURSE|G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET(LocationGPSDControlClass, error_verbose),
			0, NULL, g_cclosure_marshal_VOID__INT,
			G_TYPE_NONE, 1, G_TYPE_INT);

	signals[GPSD_RUNNING] = g_signal_new("gpsd-running",
			G_TYPE_FROM_CLASS(klass),
			G_SIGNAL_NO_RECURSE|G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET(LocationGPSDControlClass, gpsd_running),
			0, NULL, g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);

	signals[GPSD_STOPPED] = g_signal_new("gpsd-stopped",
			G_TYPE_FROM_CLASS(klass),
			G_SIGNAL_NO_RECURSE|G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET(LocationGPSDControlClass, gpsd_stopped),
			0, NULL, g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);

	obj_properties[METHOD] = g_param_spec_int("preferred-method",
			"Preferred method",
			"The positioning method the application would like to use.",
			0, 15, 0, G_PARAM_WRITABLE|G_PARAM_READABLE);

	obj_properties[INTERVAL] = g_param_spec_int("preferred-interval",
			"Preferred interval",
			"The interval between fixes the application prefers.",
			-1, 0x7FFFFFFF, -1, G_PARAM_WRITABLE|G_PARAM_READABLE);

	obj_properties[MAINCONTEXT] = g_param_spec_pointer("maincontext-pointer",
			"The pointer to the GMainContext instance",
			"Set the main context address.",
			G_PARAM_WRITABLE);

	g_object_class_install_properties(&klass->parent_class,
			LAST_PROP, obj_properties);
}

void location_gpsd_control_init(LocationGPSDControl *control)
{
	LocationGPSDControlPrivate *priv;
	DBusGConnection *bus;
	GConfValue *method;

	priv = location_gpsd_control_get_instance_private(control);

	priv->sel_method = LOCATION_METHOD_USER_SELECTED;
	priv->interval = LOCATION_INTERVAL_DEFAULT;
	priv->gconf = gconf_client_get_default();

	bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, NULL);
	priv->dbus = dbus_g_connection_ref(bus);

	priv->gps_disabled = gconf_client_get_bool(priv->gconf,
			GCONF_GPS_DISABLED, NULL);
	priv->net_disabled = gconf_client_get_bool(priv->gconf,
			GCONF_NET_DISABLED, NULL);
	priv->disclaimer_accepted = gconf_client_get_bool(priv->gconf,
			GCONF_DISCLAIMER_ACCEPTED, NULL);

	method = gconf_client_get(priv->gconf, GCONF_METHOD, NULL);
	if (method) {
		get_location_method(control, method);
		gconf_value_free(method);
	}

	gconf_client_add_dir(priv->gconf, GCONF_LOC,
			GCONF_CLIENT_PRELOAD_NONE, NULL);

	priv->client_id = gconf_client_notify_add(priv->gconf,
			GCONF_LOC, (GConfClientNotifyFunc)on_gconf_changed,
			control, NULL, NULL);

	priv->cdr_method = dbus_g_proxy_new_for_name(priv->dbus, "com.nokia.mce",
			"/com/nokia/mce/signal", "com.nokia.mce.signal");
	dbus_g_proxy_add_signal(priv->cdr_method, "sig_device_mode_ind",
			G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal(priv->cdr_method, "sig_device_mode_ind",
			(GCallback)device_mode_changed_cb, control, NULL);
}

LocationGPSDControl *location_gpsd_control_get_default()
{
	return g_object_new(LOCATION_TYPE_GPSD_CONTROL, NULL);
}
