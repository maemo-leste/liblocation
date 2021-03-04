/*
 * Copyright (c) 2020-2021 Ivan J. <parazyd@dyne.org>
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

#include <dbus/dbus-glib.h>
#include <gconf/gconf-client.h>
#include <glib.h>

#include "location-gpsd-control.h"

#define GC_LOC           "/system/nokia/location"
#define GC_METHOD        GC_LOC"/method"
#define GC_GPS_DISABLED  GC_LOC"/gps-disabled"
#define GC_NET_DISABLED  GC_LOC"/network-disabled"
#define GC_DIS_ACCEPTED  GC_LOC"/disclaimer-accepted"

#define LOCATION_DAEMON_SERVICE "org.maemo.LocationDaemon"
#define FLOCK_PATH "/run/lock/location-daemon.lock"

#define LOCATION_UI_SERVICE  "com.nokia.Location.UI"
#define LOCATION_UI_DIALOG   LOCATION_UI_SERVICE".Dialog"
#define LOCATION_UI_INUSE    LOCATION_UI_SERVICE".Error.InUse"

#define UI_METHOD_PATH        "/com/nokia/location/ui"
#define UI_DISCLAIMER         UI_METHOD_PATH"/disclaimer"
#define UI_ENABLE_POSITIONING UI_METHOD_PATH"/enable_positioning"
#define UI_BT_DISABLED        UI_METHOD_PATH"/bt_disabled"
#define UI_ENABLE_AGNSS       UI_METHOD_PATH"/enable_agnss"
#define UI_ENABLE_NETWORK     UI_METHOD_PATH"/enable_network"
#define UI_ENABLE_GPS         UI_METHOD_PATH"/enable_gps"

#define MCE_SERVICE         "com.nokia.mce"
#define MCE_REQUEST_METHOD  MCE_SERVICE".request"
#define MCE_REQUEST_PATH    "/com/nokia/mce/request"
#define MCE_SIGNAL_METHOD   MCE_SERVICE".signal"
#define MCE_SIGNAL_PATH     "/com/nokia/mce/signal"

#define BLUEZ_SERVICE  "org.bluez"
#define BLUEZ_ADAPTER  BLUEZ_SERVICE".Adapter"
#define BLUEZ_MANAGER  BLUEZ_SERVICE".Manager"

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

struct _LocationGPSDControlPrivate
{
	GMainContext *ctx;
	GSource *gsource_mainloop;
	GConfClient *gconf;
	DBusGConnection *dbus;
	DBusGProxy *location_ui_proxy;
	DBusGProxy *cdr_method;
	gchar *device;
	gchar *device_car;
	guint client_id;
	gboolean gps_disabled;
	gboolean net_disabled;
	gboolean dis_accepted;
	gboolean ui_open;
	gboolean is_running;
	gboolean gpsd_running;
	int sel_method;
	int interval;
	gboolean field_48;
	gchar *mce_device_mode;
};

static guint signals[LAST_SIGNAL] = {};
static GParamSpec *obj_properties[LAST_PROP] = {};

G_DEFINE_TYPE_WITH_PRIVATE(LocationGPSDControl, location_gpsd_control, G_TYPE_OBJECT);

/* function declarations */
static void gpsd_shutdown(LocationGPSDControl *);
static int gpsd_start(LocationGPSDControl *);
static void ui_proxy_close(LocationGPSDControl *);
static int get_selected_method(LocationGPSDControl *, int);
static int get_selected_method_wrap(LocationGPSDControl *);
static void on_positioning_activate_response(int, int, GObject *);
static void register_dbus_signal_callback(LocationGPSDControl *, const char *, void(*)(void), GError **);
static void toggle_gps_and_disclaimer(int, int, GObject *);
static void toggle_gps_and_supl(int, int, GObject *);
static void toggle_network(int, int, GObject *);
static void toggle_gps(int, int, GObject *);
static void location_gpsd_control_start_internal(LocationGPSDControl *, int, GError **);
static void location_gpsd_control_prestart_internal(LocationGPSDControl *, int);
static gboolean main_loop(void);
static void get_location_method(LocationGPSDControl *, const GConfValue *);
static void device_mode_changed_cb(int, gchar *, GObject *);
static gboolean gconf_get_bool(gboolean *, const GConfValue *);
static void on_gconf_changed(GConfClient *, guint, GConfEntry *, GObject *);
static void location_gpsd_control_class_dispose(GObject *);
static void location_gpsd_control_class_set_property(GObject *, guint, const GValue *, GParamSpec *);
static void location_gpsd_control_class_get_property(GObject *, guint, GValue *, GParamSpec *);
static void location_gpsd_control_class_init(LocationGPSDControlClass *);
static void location_gpsd_control_init(LocationGPSDControl *);

void gpsd_shutdown(LocationGPSDControl *control)
{
	LocationGPSDControlPrivate *p;
	p = location_gpsd_control_get_instance_private(control);
	p->gpsd_running = FALSE;
}

int gpsd_start(LocationGPSDControl *control)
{
	LocationGPSDControlPrivate *p;
	p = location_gpsd_control_get_instance_private(control);
	p->gpsd_running = TRUE;
	return 0;
}

void ui_proxy_close(LocationGPSDControl *control)
{
	LocationGPSDControlPrivate *p = location_gpsd_control_get_instance_private(control);

	if (p->location_ui_proxy) {
		if (p->ui_open) {
			dbus_g_proxy_call(p->location_ui_proxy, "close", NULL, G_TYPE_INVALID);
			p->ui_open = FALSE;
		}
		g_object_unref(p->location_ui_proxy);
		p->location_ui_proxy = NULL;
	}
}

int get_selected_method(LocationGPSDControl *control, int method)
{
	int acwp, gnss, agnss, result;
	LocationGPSDControlPrivate *p = location_gpsd_control_get_instance_private(control);

	if (method) {
		acwp = LOCATION_METHOD_ACWP;
		gnss = LOCATION_METHOD_GNSS;
		agnss = LOCATION_METHOD_AGNSS;
		result = LOCATION_METHOD_CWP;
		goto out;
	}

	result = method & LOCATION_METHOD_CWP;

	if (method & LOCATION_METHOD_CWP)
		result = LOCATION_METHOD_CWP;

	acwp = method & LOCATION_METHOD_ACWP;
	gnss = method & LOCATION_METHOD_GNSS;
	agnss = method & LOCATION_METHOD_AGNSS;

out:
	if (acwp && !p->net_disabled)
		result |= LOCATION_METHOD_ACWP;
	if (gnss && !p->gps_disabled)
		result |= LOCATION_METHOD_GNSS;
	if (agnss && !p->net_disabled && !p->gps_disabled)
		result |= LOCATION_METHOD_AGNSS;

	return result;
}

int get_selected_method_wrap(LocationGPSDControl *control)
{
	LocationGPSDControlPrivate *p = location_gpsd_control_get_instance_private(control);
	return get_selected_method(control, p->sel_method);
}

void on_positioning_activate_response(int unused, int a2, GObject *object)
{
	LocationGPSDControlPrivate *p;

	g_assert(LOCATION_IS_GPSD_CONTROL(object));
	p = location_gpsd_control_get_instance_private(LOCATION_GPSD_CONTROL(object));

	/* TODO: Simplify */

	if (a2 & 1) {
		g_debug("%s: %d", G_STRFUNC, a2);
		p->gps_disabled = FALSE;
		gconf_client_set_bool(p->gconf, GC_GPS_DISABLED, FALSE, NULL);
		if (!(a2 & 2))
			goto lab3;
	} else if (!(a2 & 2))
		goto lab3;

	g_debug("%s: %d", G_STRFUNC, a2);
	p->net_disabled = FALSE;
	gconf_client_set_bool(p->gconf, GC_NET_DISABLED, FALSE, NULL);
lab3:
	if (get_selected_method_wrap(LOCATION_GPSD_CONTROL(object))) {
		location_gpsd_control_prestart_internal(LOCATION_GPSD_CONTROL(object), FALSE);
	} else {
		g_signal_emit(LOCATION_GPSD_CONTROL(object), signals[ERROR], 0);
		g_signal_emit(LOCATION_GPSD_CONTROL(object), signals[ERROR_VERBOSE], 0,
				LOCATION_ERROR_USER_REJECTED_DIALOG);
	}

	ui_proxy_close(LOCATION_GPSD_CONTROL(object));
}

void register_dbus_signal_callback(LocationGPSDControl *control,
		const char *path, void (*handler_func)(void), GError **err)
{
	LocationGPSDControlPrivate *p;
	DBusGProxy *new_proxy;
	GQuark quark;
	const char *err_name;
	int response;
	GError *ierr = NULL;

	p = location_gpsd_control_get_instance_private(control);

	if (!p->location_ui_proxy) {
		new_proxy = dbus_g_proxy_new_for_name(p->dbus,
				LOCATION_UI_SERVICE,
				path,
				LOCATION_UI_DIALOG);
		p->location_ui_proxy = new_proxy;

		dbus_g_proxy_add_signal(p->location_ui_proxy, "response",
				G_TYPE_INT, G_TYPE_INVALID);
		dbus_g_proxy_connect_signal(p->location_ui_proxy, "response",
				handler_func, control, NULL);
		dbus_g_proxy_call(p->location_ui_proxy, "display",
				&ierr, G_TYPE_INVALID);

		if (ierr) {
			quark = dbus_g_error_quark();
			if (g_error_matches(ierr, quark, 32)
					&& (err_name = dbus_g_error_get_name(ierr),
						g_str_equal(err_name, LOCATION_UI_INUSE))) {
				response = strtol(ierr->message, NULL, 10);
				g_message("%s already active, current response = %d",
						path, response);
				if (response >= 0) {
					((void (*)(DBusGProxy *, int, LocationGPSDControl*))handler_func)(
						p->location_ui_proxy,
						response,
						control);
				}
				g_error_free(ierr);
			} else {
				g_propagate_error(err, ierr);
			}
		} else {
			p->ui_open = TRUE;
		}
	}
}

void toggle_gps_and_disclaimer(int unused, int a2, GObject *object)
{
	LocationGPSDControlPrivate *p;

	g_assert(LOCATION_IS_GPSD_CONTROL(object));
	p = location_gpsd_control_get_instance_private(LOCATION_GPSD_CONTROL(object));

	p->dis_accepted = a2 == 0;
	gconf_client_set_bool(p->gconf, GC_DIS_ACCEPTED, p->dis_accepted, NULL);

	if (p->dis_accepted) {
		if (p->gps_disabled) {
			p->gps_disabled = FALSE;
			gconf_client_set_bool(p->gconf, GC_GPS_DISABLED, FALSE, NULL);
			if (p->sel_method != LOCATION_METHOD_AGNSS) /* != &byte_8 */
				goto out;
		} else if (p->sel_method != LOCATION_METHOD_AGNSS) { /* != &byte_8 */
out:
			location_gpsd_control_prestart_internal(LOCATION_GPSD_CONTROL(object), TRUE);
			ui_proxy_close(LOCATION_GPSD_CONTROL(object));
			return;
		}

		if (p->net_disabled) {
			p->net_disabled = FALSE;
			gconf_client_set_bool(p->gconf, GC_NET_DISABLED, FALSE, NULL);
		}
		goto out;
	}

	g_signal_emit(LOCATION_GPSD_CONTROL(object), signals[ERROR], 0);
	g_signal_emit(LOCATION_GPSD_CONTROL(object), signals[ERROR_VERBOSE], 0,
			LOCATION_ERROR_USER_REJECTED_DIALOG);
	ui_proxy_close(LOCATION_GPSD_CONTROL(object));
}

void toggle_gps_and_supl(int unused, int a2, GObject *object)
{
	LocationGPSDControlPrivate *p;

	g_assert(LOCATION_IS_GPSD_CONTROL(object));
	p = location_gpsd_control_get_instance_private(LOCATION_GPSD_CONTROL(object));

	if (!a2) {
		p->gps_disabled = FALSE;
		p->net_disabled = FALSE;
		gconf_client_set_bool(p->gconf, GC_GPS_DISABLED, FALSE, NULL);
		gconf_client_set_bool(p->gconf, GC_NET_DISABLED, FALSE, NULL);
	}

	if (get_selected_method_wrap(LOCATION_GPSD_CONTROL(object))) {
		location_gpsd_control_prestart_internal(LOCATION_GPSD_CONTROL(object), FALSE);
	} else {
		g_signal_emit(LOCATION_GPSD_CONTROL(object), signals[ERROR], 0);
		g_signal_emit(LOCATION_GPSD_CONTROL(object), signals[ERROR_VERBOSE], 0,
				LOCATION_ERROR_USER_REJECTED_DIALOG);
	}

	ui_proxy_close(LOCATION_GPSD_CONTROL(object));
}

void toggle_network(int unused, int state, GObject *object)
{
	LocationGPSDControlPrivate *p;

	g_assert(LOCATION_IS_GPSD_CONTROL(object));
	p = location_gpsd_control_get_instance_private(LOCATION_GPSD_CONTROL(object));

	p->net_disabled = state != 0;
	gconf_client_set_bool(p->gconf, GC_NET_DISABLED,
			p->net_disabled, NULL);

	if (p->net_disabled && !get_selected_method_wrap(LOCATION_GPSD_CONTROL(object))) {
		g_signal_emit(LOCATION_GPSD_CONTROL(object), signals[ERROR], 0);
		g_signal_emit(LOCATION_GPSD_CONTROL(object), signals[ERROR_VERBOSE], 0,
				LOCATION_ERROR_USER_REJECTED_DIALOG);
	} else {
		location_gpsd_control_prestart_internal(LOCATION_GPSD_CONTROL(object), FALSE);
	}

	ui_proxy_close(LOCATION_GPSD_CONTROL(object));
}

void toggle_gps(int unused, int state, GObject *object)
{
	LocationGPSDControlPrivate *p;

	g_assert(LOCATION_IS_GPSD_CONTROL(object));
	p = location_gpsd_control_get_instance_private(LOCATION_GPSD_CONTROL(object));

	p->gps_disabled = state != 0;
	gconf_client_set_bool(p->gconf, GC_GPS_DISABLED,
			p->gps_disabled, NULL);

	if (p->gps_disabled && !get_selected_method_wrap(LOCATION_GPSD_CONTROL(object))) {
		g_signal_emit(LOCATION_GPSD_CONTROL(object), signals[ERROR], 0);
		g_signal_emit(LOCATION_GPSD_CONTROL(object), signals[ERROR_VERBOSE], 0,
				LOCATION_ERROR_USER_REJECTED_DIALOG);
	} else {
		location_gpsd_control_prestart_internal(LOCATION_GPSD_CONTROL(object), FALSE);
	}

	ui_proxy_close(LOCATION_GPSD_CONTROL(object));
}

void location_gpsd_control_start_internal(LocationGPSDControl *control,
		int unsure_ui_related, GError **err)
{
	LocationGPSDControlPrivate *p;
	gchar *mce_device_mode;
	GHashTable *hash_table;
	DBusGProxy *proxy;
	GQuark quark;
	GError *ierr = NULL;
	GType type, hashtable_type;
	gboolean v21;
	const GValue *v27;
	GValue value;
	int method;

	g_assert(LOCATION_IS_GPSD_CONTROL(control));
	p = location_gpsd_control_get_instance_private(control);

	if (p->gpsd_running)
		return;

	if (!p->dis_accepted) {
		if (unsure_ui_related) {
			if (!p->location_ui_proxy)
				register_dbus_signal_callback(control, UI_DISCLAIMER,
						(void (*)(void))toggle_gps_and_disclaimer,
						err);
			return;
		}

lab46:
		quark = g_quark_from_static_string("location-error-quark");
		g_set_error(err, quark, 2, "Use denied by settings");
		return;
	}

	mce_device_mode = p->mce_device_mode;
	method = p->sel_method;

	if (!mce_device_mode) {
		mce_device_mode = NULL;
		proxy = dbus_g_proxy_new_for_name(p->dbus,
				MCE_SERVICE, MCE_REQUEST_PATH, MCE_REQUEST_METHOD);
		dbus_g_proxy_call(proxy, "get_device_mode",
				&ierr, G_TYPE_INVALID,
				G_TYPE_STRING, &mce_device_mode,
				G_TYPE_INVALID);
		g_object_unref(proxy);

		if (ierr) {
			g_warning("%s: %s", "update_mce_device_mode", ierr->message);
			g_error_free(ierr);
			g_signal_emit(control, signals[ERROR], 0);
			g_signal_emit(control, signals[ERROR_VERBOSE], 0, LOCATION_ERROR_SYSTEM);
			return;
		}

		if (p->mce_device_mode) {
			g_free(p->mce_device_mode);
			p->mce_device_mode = NULL;
		}
		p->mce_device_mode = mce_device_mode;
	}

	if (g_strcmp0(p->mce_device_mode, "flight") && g_strcmp0(p->mce_device_mode, "offline")) {
		if (!method) {
			if (!p->net_disabled) {
				/* TODO: Review if used defines are right */
				if (p->gps_disabled)
					method = LOCATION_METHOD_ACWP; /* 2 */
				else
					method = LOCATION_METHOD_ACWP|LOCATION_METHOD_AGNSS; /* 10 */
				goto lab22;
			}

			if (!p->gps_disabled) {
				method = LOCATION_METHOD_GNSS; /* 4 */
				goto lab22;
			}

			if (!unsure_ui_related) {
				method = LOCATION_METHOD_CWP; /* 1 */
				goto lab22;
			}

lab43:
			register_dbus_signal_callback(control, UI_ENABLE_POSITIONING,
					(void (*)(void))on_positioning_activate_response, err);
			return;
		}
	} else {
		g_debug("%s: We are in offline mode now!", G_STRLOC);

		if (g_strcmp0(p->device, "las")) {
			register_dbus_signal_callback(control, UI_BT_DISABLED, NULL, NULL);
			g_warning("%s: Offline mode, external device, giving up.", G_STRLOC);
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
		if ((method & 6) == 6 && p->net_disabled && p->gps_disabled) {
			if (p->location_ui_proxy)
				return;
			goto lab43;
		}

		if (!(method & 8))
			goto lab96;

		if (p->net_disabled) {
			if (p->gps_disabled) {
				if (p->location_ui_proxy)
					return;

				if ((unsigned int)(method - 8) <= 1) {
					register_dbus_signal_callback(control, UI_ENABLE_AGNSS,
							(void (*)(void))toggle_gps_and_supl,
							err);
					return;
				}

				goto lab43;
			}
lab60:
			if (!p->location_ui_proxy)
				register_dbus_signal_callback(control, UI_ENABLE_NETWORK,
						(void (*)(void))toggle_gps_and_supl,
						err);
			return;
		}

		if (!p->gps_disabled) {
lab96:
			if (!(method & 4) || !p->gps_disabled) {
				if (!(method & 2) || !p->net_disabled)
					goto lab22;
				goto lab60;
			}
		}

		if (!p->location_ui_proxy)
			register_dbus_signal_callback(control, UI_ENABLE_GPS,
					(void (*)(void))toggle_gps,
					err);
		return;
	}

	method &= get_selected_method_wrap(control);
	if (!method)
		goto lab46;

lab22:
	g_assert(LOCATION_IS_GPSD_CONTROL(control));

	// interval = p->interval

	if ((unsigned int)(method - 1) <= 2 || !g_strcmp0(p->device, "las")) {
		/* TODO: What? */
		if (!gpsd_start(control))
			return;
		p->field_48 = FALSE;
		p->gpsd_running = TRUE;
		goto lab48;
	}

	mce_device_mode = NULL;
	hash_table = NULL;

	proxy = dbus_g_proxy_new_for_name(p->dbus, BLUEZ_SERVICE, "/", BLUEZ_MANAGER);
	type = dbus_g_object_path_get_g_type();
	if (!dbus_g_proxy_call(proxy, "DefaultAdapter", &ierr,
				G_TYPE_INVALID, type, &mce_device_mode,
				G_TYPE_INVALID)) {
		if (ierr) {
			g_warning("Error getting Bluetooth adapter: %s", ierr->message);
			g_error_free(ierr);
		}

		g_object_unref(proxy);
lab31:
		register_dbus_signal_callback(control, UI_BT_DISABLED, NULL, NULL);
		quark = g_quark_from_static_string("location-error-quark");
		g_set_error(err, quark, 4, "Bluetooth not available");
		return;
	}

	g_object_unref(proxy);

	proxy = dbus_g_proxy_new_for_name(p->dbus, BLUEZ_SERVICE,
			mce_device_mode, BLUEZ_ADAPTER);
	type = g_value_get_type();
	hashtable_type = dbus_g_type_get_map("GHashTable", G_TYPE_STRING, type);
	v21 = dbus_g_proxy_call(proxy, "GetProperties", &ierr,
			G_TYPE_INVALID, hashtable_type, &hash_table,
			G_TYPE_INVALID);
	if (!v21) {
		if (ierr) {
lab75:
			g_warning("Error getting Bluetooth adapter properties: %s",
					ierr->message);
			g_error_free(ierr);
			goto lab76;
		}
		goto lab82;
	}

	if (!hash_table
			|| (v27 = (const GValue *)g_hash_table_lookup(hash_table, "Powered")) == 0
			|| (v21 = g_value_get_boolean(v27)) == 0) {
		g_debug("Bluetooth adapter status: %s", "not powered");

		if (g_strcmp0(p->mce_device_mode, "flight") && g_strcmp0(p->mce_device_mode, "offline")) {
			/* TODO: sort this out */
			value.g_type = 0;
			*(&value.g_type + 1 ) = 0;
			value.data[0].v_int64 = 0LL;
			value.data[1].v_int64 = 0LL;
			g_value_init(&value, 0x14u);
			g_value_set_boolean(&value, TRUE);

			v21 = dbus_g_proxy_call(proxy, "SetProperty", &ierr,
					G_TYPE_STRING, "Powered",
					type, &value,
					G_TYPE_INVALID, G_TYPE_INVALID);
			if (v21) {
				v21 = TRUE;
				goto lab76;
			}

			if (ierr)
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
	p->field_48 = TRUE;

lab48:
	if (!p->is_running) {
		g_signal_emit(control, signals[GPSD_RUNNING], 0);
		p->is_running = TRUE;
	}

	return;
}

void location_gpsd_control_prestart_internal(LocationGPSDControl *control,
		int unsure_ui_related)
{
	GError *err = NULL;

	location_gpsd_control_start_internal(control, unsure_ui_related, &err);

	if (err) {
		g_warning("%s: %s", G_STRFUNC, err->message);
		g_error_free(err);
		g_signal_emit(control, signals[ERROR], 0);
		g_signal_emit(control, signals[ERROR_VERBOSE], 0, LOCATION_ERROR_SYSTEM);
	}
}

gboolean main_loop(void)
{
	while (g_main_context_pending(NULL))
		g_message("mainloop iter");
		g_main_context_iteration(NULL, FALSE);

	return TRUE;
}

void location_gpsd_control_start(LocationGPSDControl *control)
{
	LocationGPSDControlPrivate *p;

	g_assert(LOCATION_IS_GPSD_CONTROL(control));

	p = location_gpsd_control_get_instance_private(control);

	if (p->ctx) {
		p->gsource_mainloop = g_timeout_source_new(1000);
		g_source_set_callback(p->gsource_mainloop, (GSourceFunc)main_loop,
				control, NULL);
		g_source_attach(p->gsource_mainloop, p->ctx);
	}

	p->is_running = FALSE;
	location_gpsd_control_prestart_internal(control, TRUE);
}

void location_gpsd_control_stop(LocationGPSDControl *control)
{
	LocationGPSDControlPrivate *p;

	g_assert(LOCATION_IS_GPSD_CONTROL(control));
	p = location_gpsd_control_get_instance_private(control);

	if (p->ctx) {
		if (!g_source_is_destroyed(p->gsource_mainloop))
			g_source_destroy(p->gsource_mainloop);
	}

	if (p->gpsd_running) {
		gpsd_shutdown(control);
		g_signal_emit(control, signals[GPSD_STOPPED], 0);
	}

	ui_proxy_close(control);
}

void get_location_method(LocationGPSDControl *control, const GConfValue *value)
{
	LocationGPSDControlPrivate *p;
	GConfValue *car_ret, *cdr_ret;
	const gchar *cdr_str, *car_str;

	p = location_gpsd_control_get_instance_private(control);

	if (p->gpsd_running)
		gpsd_shutdown(control);

	if (p->device != NULL) {
		g_free(p->device);
		p->device = NULL;
	}

	if (p->device_car) {
		g_free(p->device_car);
		p->device_car = NULL;
	}

	if (!value) {
		if (!p->gpsd_running)
			g_signal_emit(control, signals[GPSD_STOPPED], 0);
		return;
	}

	car_ret = gconf_value_get_car(value);
	cdr_ret = gconf_value_get_cdr(value);

	if (car_ret) {
		if (!cdr_ret || car_ret->type != GCONF_VALUE_STRING
				|| cdr_ret->type != GCONF_VALUE_STRING) {
			if (!p->gpsd_running)
				g_signal_emit(control, signals[GPSD_STOPPED], 0);
		} else {
			cdr_str = gconf_value_get_string(cdr_ret);
			g_debug("cdr_str: %s", cdr_str);
			p->device = g_strdup(cdr_str);
			car_str = gconf_value_get_string(car_ret);
			g_debug("car_str: %s", car_str);
			p->device_car = g_strdup(car_str);

			if (!p->gpsd_running)
				location_gpsd_control_prestart_internal(control, TRUE);
		}
	}
}

void device_mode_changed_cb(int unused, gchar *sig, GObject *object)
{
	LocationGPSDControlPrivate *p;
	gchar *mce_device_mode, *tmp;

	g_assert(LOCATION_IS_GPSD_CONTROL(object));
	p = location_gpsd_control_get_instance_private(LOCATION_GPSD_CONTROL(object));

	mce_device_mode = p->mce_device_mode;
	g_debug("old mce_device_mode: %s", p->mce_device_mode);
	tmp = g_strdup(sig);
	p->mce_device_mode = tmp;
	g_debug("new mce_device_mode: %s", p->mce_device_mode);

	if (p->gpsd_running && g_strcmp0(mce_device_mode, p->mce_device_mode)) {
		gpsd_shutdown(LOCATION_GPSD_CONTROL(object));
		location_gpsd_control_prestart_internal(LOCATION_GPSD_CONTROL(object), TRUE);
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

void on_gconf_changed(GConfClient *client, guint cnxn_id, GConfEntry *entry,
		GObject *object)
{
	LocationGPSDControlPrivate *p;
	gboolean tmp;
	GQuark quark;
	GError *err = NULL;

	g_assert(entry && entry->key);
	g_assert(LOCATION_IS_GPSD_CONTROL(object));
	p = location_gpsd_control_get_instance_private(LOCATION_GPSD_CONTROL(object));

	if (g_str_equal(entry->key, GC_METHOD)) {
		get_location_method(LOCATION_GPSD_CONTROL(object), entry->value);
		return;
	} else if (g_str_equal(entry->key, GC_GPS_DISABLED))
		tmp = gconf_get_bool(&p->gps_disabled, entry->value);
	else if (g_str_equal(entry->key, GC_NET_DISABLED))
		tmp = gconf_get_bool(&p->net_disabled, entry->value);
	else if (g_str_equal(entry->key, GC_DIS_ACCEPTED))
		tmp = gconf_get_bool(&p->dis_accepted, entry->value);
	else
		return;

	if (tmp) {
		if (p->gpsd_running) {
			gpsd_shutdown(LOCATION_GPSD_CONTROL(object));
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
	LocationGPSDControlPrivate *p;

	g_assert(LOCATION_IS_GPSD_CONTROL(object));
	p = location_gpsd_control_get_instance_private(LOCATION_GPSD_CONTROL(object));

	if (p->gconf) {
		gconf_client_notify_remove(p->gconf, p->client_id);
		g_object_unref(p->gconf);
		p->gconf = NULL;
	}

	location_gpsd_control_stop(LOCATION_GPSD_CONTROL(object));

	if (p->device) {
		g_free(p->device);
		p->device = NULL;
	}

	if (p->device_car) {
		g_free(p->device_car);
		p->device_car = NULL;
	}

	if (p->cdr_method) {
		dbus_g_proxy_disconnect_signal(p->cdr_method, "sig_device_mode_ind",
				(GCallback)device_mode_changed_cb,
				LOCATION_GPSD_CONTROL(object));
		g_object_unref(p->cdr_method);
		p->cdr_method = NULL;
	}

	if (p->dbus) {
		dbus_g_connection_unref(p->dbus);
		p->dbus = NULL;
	}
}

void location_gpsd_control_class_set_property(GObject *object,
		guint property_id, const GValue *value, GParamSpec *pspec)
{
	LocationGPSDControlPrivate *p;
	const gchar *list, *klass_type, *parent_type;

	g_assert(LOCATION_IS_GPSD_CONTROL(object));
	p = location_gpsd_control_get_instance_private(LOCATION_GPSD_CONTROL(object));

	switch ((ControlClassProperty)property_id) {
	case METHOD:
		p->sel_method = g_value_get_int(value);
		break;
	case INTERVAL:
		p->interval = g_value_get_int(value);
		break;
	case MAINCONTEXT:
		p->ctx = g_value_get_pointer(value);
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
	LocationGPSDControlPrivate *p;
	const gchar *list, *klass_type, *parent_type;

	g_assert(LOCATION_IS_GPSD_CONTROL(object));
	p = location_gpsd_control_get_instance_private(LOCATION_GPSD_CONTROL(object));

	switch ((ControlClassProperty)property_id) {
	case METHOD:
		g_value_set_int(value, p->sel_method);
		break;
	case INTERVAL:
		g_value_set_int(value, p->interval);
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

	g_object_class_install_properties(&klass->parent_class, LAST_PROP,
			obj_properties);
}

void location_gpsd_control_init(LocationGPSDControl *control)
{
	LocationGPSDControlPrivate *p;
	DBusGConnection *bus;
	GConfValue *method;

	p = location_gpsd_control_get_instance_private(control);

	p->sel_method = LOCATION_METHOD_USER_SELECTED;
	p->interval = LOCATION_INTERVAL_DEFAULT;
	p->gconf = gconf_client_get_default();

	bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, NULL);
	p->dbus = dbus_g_connection_ref(bus);

	p->gps_disabled = gconf_client_get_bool(p->gconf, GC_GPS_DISABLED, NULL);
	p->net_disabled = gconf_client_get_bool(p->gconf, GC_NET_DISABLED, NULL);
	p->dis_accepted = gconf_client_get_bool(p->gconf, GC_DIS_ACCEPTED, NULL);

	method = gconf_client_get(p->gconf, GC_METHOD, NULL);
	if (method) {
		get_location_method(control, method);
		gconf_value_free(method);
	}

	gconf_client_add_dir(p->gconf, GC_LOC, GCONF_CLIENT_PRELOAD_NONE, NULL);

	p->client_id = gconf_client_notify_add(p->gconf, GC_LOC,
			(GConfClientNotifyFunc)on_gconf_changed, control, NULL, NULL);

	p->cdr_method = dbus_g_proxy_new_for_name(p->dbus, MCE_SERVICE,
			MCE_SIGNAL_PATH, MCE_SIGNAL_METHOD);
	dbus_g_proxy_add_signal(p->cdr_method, "sig_device_mode_ind",
			G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal(p->cdr_method, "sig_device_mode_ind",
			(GCallback)device_mode_changed_cb, control, NULL);
}

gint location_gpsd_control_get_allowed_methods(LocationGPSDControl *control)
{
	g_assert(LOCATION_IS_GPSD_CONTROL(control));
	return get_selected_method(control, 0);
}

void location_gpsd_control_request_status(LocationGPSDControl *control)
{
	g_warning("You don't need to call %s, it does nothing anymore!",
			G_STRFUNC);
}

LocationGPSDControl *location_gpsd_control_get_default()
{
	return g_object_new(LOCATION_TYPE_GPSD_CONTROL, NULL);
}
