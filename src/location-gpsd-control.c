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

enum {
	ERROR,
	ERROR_VERBOSE,
	GPSD_RUNNING,
	GPSD_STOPPED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {};

static void location_gpsd_control_start_internal(LocationGPSDControl *, int, GError **);
static void location_gpsd_control_prestart_internal(LocationGPSDControl *, int);
static void connection_changed_cb(int, int, GObject *);


typedef struct _gypsy_dbus_proxy
{
	DBusGProxy *server;
	DBusGProxy *device;
	const char *path;
} gypsy_dbus_proxy;

struct _LocationGPSDControlPrivate
{
	GMainContext **ctx;
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
	gboolean p17;
	gboolean is_running;
	int sel_method;
	int interval;
	gboolean field_48;
	gchar *mce_device_mode;
	GSource *gsource_mainloop;
};

G_DEFINE_TYPE_WITH_PRIVATE(LocationGPSDControl, location_gpsd_control, G_TYPE_OBJECT);

static void dbus_proxy_close(LocationGPSDControlPrivate *priv)
{
	DBusGProxy *proxy;

	proxy = priv->location_ui_proxy;
	if (proxy) {
		if (priv->p17) {
			dbus_g_proxy_call(proxy, "close", NULL, G_TYPE_INVALID);
			proxy = priv->location_ui_proxy;
			priv->p17 = FALSE;
		}
		g_object_unref(proxy);
		priv->location_ui_proxy = NULL;
	}
}

static void dbus_proxy_shutdown(gypsy_dbus_proxy *gdp)
{
	DBusGProxy *device, *server;
	const char *path;
	GType path_type;
	GError *error = NULL;

	device = gdp->device;
	if (device) {
		g_object_unref(device);
		gdp->device = NULL;
	}

	server = gdp->server;
	if (server) {
		path = gdp->path;
		if (path) {
			path_type = dbus_g_object_path_get_g_type();
			dbus_g_proxy_call(server, "Shutdown", &error, path_type,
					path, G_TYPE_INVALID, G_TYPE_INVALID);
			if (error) {
				g_log("liblocation", G_LOG_LEVEL_WARNING,
						error->message);
				g_error_free(error);
			}

			server = gdp->server;
		}
		g_object_unref(server);
		gdp->server = NULL;
	}

	g_free((gpointer)gdp->path);
	gdp->path = NULL;
}

static int dbus_proxy_setup(LocationGPSDControl *control, int method,
		int interval, GError **error)
{
	LocationGPSDControlPrivate *priv;
	DBusGProxy *server_proxy, *param_proxy, *device_proxy;
	gypsy_dbus_proxy *gypsy;
	GError *ierr = NULL;
	GQuark quark;
	GType type;
	int result;

	priv = control->priv;
	server_proxy = dbus_g_proxy_new_for_name(priv->dbus,
			"com.nokia.Location",
			"/org/freedesktop/Gypsy",
			"org.freedesktop.Gypsy.Server");
	priv->gypsy.server = server_proxy;

	type = dbus_g_object_path_get_g_type();
	gypsy = &priv->gypsy;

	if (!dbus_g_proxy_call(server_proxy, "Create", error,
				G_TYPE_STRING, "las",
				type, &priv->gypsy.path,
				G_TYPE_INVALID)) {
		dbus_proxy_shutdown(gypsy);
		return 0;
	}

	param_proxy = dbus_g_proxy_new_for_name(priv->dbus,
			"com.nokia.Location",
			"/com/nokia/location",
			"com.nokia.Location.Parameters");
	dbus_g_proxy_call(param_proxy, "set", &ierr, 0x18u, method, 0x18,
			interval, G_TYPE_INVALID, G_TYPE_INVALID);
	g_object_unref(param_proxy);

	if (ierr) {
		g_log("liblocation", G_LOG_LEVEL_WARNING,
				"Failed to send configuration to location service: %s",
				ierr->message);
		g_error_free(ierr);
		quark = g_quark_from_static_string("location-error-quark");
		g_set_error(error, quark, 3, "Illegal parameters");
		dbus_proxy_shutdown(gypsy);
		return 0;
	}

	device_proxy = dbus_g_proxy_new_for_name(priv->dbus,
			"com.nokia.Location",
			priv->gypsy.path,
			"org.freedesktop.Gypsy.Device");
	priv->gypsy.device = device_proxy;

	dbus_g_proxy_add_signal(device_proxy,
			"ConnectionStatusChanged", 0x14u, ierr);
	dbus_g_proxy_connect_signal(
			priv->gypsy.device,
			"ConnectionStatusChanged",
			(GCallback)connection_changed_cb,
			control,
			(GClosureNotify)ierr);

	if (dbus_g_proxy_call(priv->gypsy.device, "Start", error, (GType)ierr, ierr)) {
		result = 1;
		/* TODO: Check this field_48 set */
		priv->field_48 = FALSE;
		if (ierr)
			priv->field_48 = TRUE;
	} else {
		dbus_proxy_shutdown(gypsy);
		result = 0;
	}

	return result;
}

static void register_dbus_signal_callback(LocationGPSDControl *control,
		const char *path, void (*handler_func)(void), GError **err)
{
	LocationGPSDControlPrivate *priv;
	DBusGProxy *new_proxy;
	GQuark quark;
	const char *err_name;
	int response;
	GError *error_internal = NULL;

	priv = control->priv;
	if (!priv->location_ui_proxy) {
		new_proxy = dbus_g_proxy_new_for_name(priv->dbus,
				"com.nokia.Location.UI", path,
				"com.nokia.Location.UI.Dialog");
		priv->location_ui_proxy = new_proxy;

		dbus_g_proxy_add_signal(priv->location_ui_proxy,
				"response", 0x18u, G_TYPE_INVALID);
		dbus_g_proxy_connect_signal(control->priv->location_ui_proxy,
				"response", handler_func, control, NULL);
		dbus_g_proxy_call(control->priv->location_ui_proxy,
				"display", &error_internal, G_TYPE_INVALID);

		if (error_internal) {
			quark = dbus_g_error_quark();
			if (g_error_matches(error_internal, quark, 32)
					&& (err_name = dbus_g_error_get_name(error_internal),
						g_str_equal(err_name,
							"com.nokia.Location.UI.Error.InUse"))) {
				response = strtol(error_internal->message, NULL, 10);
				g_log("liblocation", G_LOG_LEVEL_MESSAGE,
						"%s already active, current response = %d",
						path, response);
				if (response >= 0) {
					((void (*)(DBusGProxy *, int, LocationGPSDControl *))handler_func)(
						control->priv->location_ui_proxy,
						response,
						control);
				}
				g_error_free(error_internal);
			} else {
				g_propagate_error(err, error_internal);
			}
		} else {
			control->priv->p17 = TRUE;
		}
	}
}

static int get_selected_method(LocationGPSDControlPrivate *priv, int method)
{
	/* TODO: Simplify */
	int v3, v4, v5, result;

	if (method) {
		v3 = LOCATION_METHOD_ACWP;
		v4 = LOCATION_METHOD_GNSS;
		v5 = LOCATION_METHOD_AGNSS;
lab17:
		result = 1;
		goto lab6;
	}

	result = method & LOCATION_METHOD_CWP;
	if (method & LOCATION_METHOD_CWP) {
		v5 = method & LOCATION_METHOD_AGNSS;
		v3 = method & LOCATION_METHOD_ACWP;
		v4 = method & LOCATION_METHOD_GNSS;
		goto lab17;
	}

	v5 = method & LOCATION_METHOD_AGNSS;
	v3 = method & LOCATION_METHOD_ACWP;
	v4 = method & LOCATION_METHOD_GNSS;
lab6:
	if (v3 && !priv->net_disabled)
		result |= LOCATION_METHOD_ACWP;
	if (v4 && !priv->gps_disabled)
		result |= LOCATION_METHOD_GNSS;
	if (v5 && !priv->net_disabled && !priv->gps_disabled)
		result |= LOCATION_METHOD_AGNSS;

	return result;
}

static int get_selected_method_wrap(LocationGPSDControlPrivate *priv)
{
	return get_selected_method(priv, priv->sel_method);
}

static void update_proxy_connection(int unused, int response, GObject *object)
{
	LocationGPSDControlPrivate *priv;
	GError *err;

	priv = G_TYPE_INSTANCE_GET_PRIVATE(LOCATION_GPSD_CONTROL(object),
			G_TYPE_OBJECT, LocationGPSDControlPrivate);

	if (response) {
		if (priv->gypsy.server) {
			dbus_proxy_shutdown(&priv->gypsy);
			goto out;
		}
	} else {
		err = NULL;
		if (priv->gypsy.server)
			dbus_proxy_shutdown(&priv->gypsy);

		location_gpsd_control_start_internal(
				LOCATION_GPSD_CONTROL(object), FALSE, &err);

		if (err) {
			g_log("liblocation", G_LOG_LEVEL_WARNING,
					"Failed to update connection: %s", err->message);
			g_error_free(err);
			goto out;
		}
		dbus_proxy_close(priv);
		return;
	}
out:
	g_signal_emit(LOCATION_GPSD_CONTROL(object),
			signals[ERROR], 0);
	g_signal_emit(LOCATION_GPSD_CONTROL(object),
			signals[ERROR_VERBOSE], 0, 2);
	g_signal_emit(LOCATION_GPSD_CONTROL(object),
			signals[GPSD_STOPPED], 0);

	dbus_proxy_close(priv);
}

static void connection_changed_cb(int unused, int a2, GObject *object)
{
	LocationGPSDControlPrivate *priv;
	gboolean field_48;
	GError *error = NULL;

	if (!LOCATION_IS_GPSD_CONTROL(object))
		g_assertion_message_expr("liblocation", __FILE__, __LINE__,
				G_STRFUNC, "LOCATION_IS_GPSD_CONTROL(control)");

	priv = G_TYPE_INSTANCE_GET_PRIVATE(LOCATION_GPSD_CONTROL(object),
			G_TYPE_OBJECT, LocationGPSDControlPrivate);

	field_48 = priv->field_48;
	if (priv->field_48)
		field_48 = TRUE;
	if (a2)
		field_48 = FALSE;

	if (field_48
			&& g_strcmp0(priv->mce_device_mode, "flight")
			&& g_strcmp0(priv->mce_device_mode, "offline")
			&& !priv->location_ui_proxy) {
		register_dbus_signal_callback(
				LOCATION_GPSD_CONTROL(object),
				"/com/nokia/location/ui/bt_disconnected",
				(void (*)(void))update_proxy_connection,
				&error);
	}
}

static void enable_gps_and_supl(int unused, int a2, GObject *object)
{
	LocationGPSDControlPrivate *priv;

	priv = G_TYPE_INSTANCE_GET_PRIVATE(LOCATION_GPSD_CONTROL(object),
			G_TYPE_OBJECT, LocationGPSDControlPrivate);

	if (!a2) {
		priv->gps_disabled = FALSE;
		priv->net_disabled = FALSE;
		gconf_client_set_bool(priv->gconf, GCONF_NET_DISABLED, FALSE, NULL);
		gconf_client_set_bool(priv->gconf, GCONF_NET_DISABLED, FALSE, NULL);
	}

	if (get_selected_method_wrap(priv)) {
		location_gpsd_control_prestart_internal(
				LOCATION_GPSD_CONTROL(object), FALSE);
	} else {
		g_signal_emit(LOCATION_GPSD_CONTROL(object),
				signals[ERROR], 0);
		g_signal_emit(LOCATION_GPSD_CONTROL(object),
				signals[ERROR], 0, 0);
	}

	dbus_proxy_close(priv);
}

static void enable_gps_and_disclaimer(int unused, int a2, GObject *object)
{
	gboolean accepted;
	LocationGPSDControlPrivate *priv;

	accepted = a2 == 0;

	priv = G_TYPE_INSTANCE_GET_PRIVATE(LOCATION_GPSD_CONTROL(object),
			G_TYPE_OBJECT, LocationGPSDControlPrivate);

	priv->disclaimer_accepted = accepted;
	gconf_client_set_bool(priv->gconf, GCONF_DISCLAIMER_ACCEPTED,
			accepted, NULL);

	if (priv->disclaimer_accepted) {
		if (priv->gps_disabled) {
			priv->gps_disabled = FALSE;
			gconf_client_set_bool(priv->gconf, GCONF_GPS_DISABLED,
					FALSE, NULL);
			if (priv->sel_method != LOCATION_METHOD_AGNSS) /* != &byte_8 */
				goto out;
		} else if (priv->sel_method != LOCATION_METHOD_AGNSS) {/* != &byte_8 */
out:
			location_gpsd_control_prestart_internal(
					LOCATION_GPSD_CONTROL(object), TRUE);
			dbus_proxy_close(priv);
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
			signals[ERROR_VERBOSE], 0, 0);
	dbus_proxy_close(priv);
}

static void on_positioning_activate_response(int unused, int a2, GObject *object)
{
	LocationGPSDControlPrivate *priv;

	priv = G_TYPE_INSTANCE_GET_PRIVATE(LOCATION_GPSD_CONTROL(object),
			G_TYPE_OBJECT, LocationGPSDControlPrivate);

	/* TODO: Simplify */

	if (a2 & 1) {
		g_log("liblocation", G_LOG_LEVEL_DEBUG, "%s: %d", G_STRFUNC, a2);
		priv->gps_disabled = FALSE;
		gconf_client_set_bool(priv->gconf, GCONF_GPS_DISABLED, FALSE, NULL);
		if (!(a2 & 2))
			goto lab3;
	} else if (!(a2 & 2))
		goto lab3;

	g_log("liblocation", G_LOG_LEVEL_DEBUG, "%s: %d", G_STRFUNC, a2);
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
				signals[ERROR_VERBOSE], 0, 0);
	}

	dbus_proxy_close(priv);
}

static void toggle_network(int unused, int a2, GObject *object)
{
	LocationGPSDControlPrivate *priv;
	gboolean a2set;

	priv = G_TYPE_INSTANCE_GET_PRIVATE(LOCATION_GPSD_CONTROL(object),
			G_TYPE_OBJECT, LocationGPSDControlPrivate);

	a2set = a2 != 0;
	priv->net_disabled = a2set;
	gconf_client_set_bool(priv->gconf, GCONF_NET_DISABLED, a2set, NULL);
	if (priv->net_disabled && !get_selected_method_wrap(priv)) {
		g_signal_emit(LOCATION_GPSD_CONTROL(object),
				signals[ERROR], 0);
		g_signal_emit(LOCATION_GPSD_CONTROL(object),
				signals[ERROR_VERBOSE], 0, 0);
	} else {
		location_gpsd_control_prestart_internal(
				LOCATION_GPSD_CONTROL(object), FALSE);
	}

	dbus_proxy_close(priv);
}

static void toggle_gps(int unused, int a2, GObject *object)
{
	LocationGPSDControlPrivate *priv;
	gboolean a2set;

	priv = G_TYPE_INSTANCE_GET_PRIVATE(LOCATION_GPSD_CONTROL(object),
			G_TYPE_OBJECT, LocationGPSDControlPrivate);

	a2set = a2 != 0;
	priv->gps_disabled = a2set;
	gconf_client_set_bool(priv->gconf, GCONF_GPS_DISABLED, a2set, NULL);
	if (priv->net_disabled && !get_selected_method_wrap(priv)) {
		g_signal_emit(LOCATION_GPSD_CONTROL(object),
				signals[ERROR], 0);
		g_signal_emit(LOCATION_GPSD_CONTROL(object),
				signals[ERROR_VERBOSE], 0, 0);
	} else {
		location_gpsd_control_prestart_internal(
				LOCATION_GPSD_CONTROL(object), FALSE);
	}

	dbus_proxy_close(priv);
}

static void location_gpsd_control_start_internal(
		LocationGPSDControl *control, int unsure_ui_related, GError **err)
{
	LocationGPSDControlPrivate *priv;
	gchar *mce_device_mode, *device;
	GHashTable *hash_table;
	DBusGConnection *bus;
	DBusGProxy *proxy, *server_proxy, *device_proxy;
	GQuark quark;
	GError *ierr1 = NULL;
	GType type, hashtable_type;
	gboolean v21;
	const GValue *v27;
	GValue value;
	int method;
	int interval;

	if (!LOCATION_IS_GPSD_CONTROL(control))
		g_assertion_message_expr("liblocation", __FILE__, __LINE__,
				G_STRFUNC, "LOCATION_IS_GPSD_CONTROL(control)");

	priv = control->priv;

	if (priv->gypsy.server)
		return;

	if (!priv->disclaimer_accepted) {
		if (unsure_ui_related) {
			if (!priv->location_ui_proxy)
				register_dbus_signal_callback(control,
						"/com/nokia/location/ui/disclaimer",
						(void (*)(void))enable_gps_and_disclaimer,
						err);
			return;
		}
lab46:
		quark = g_quark_from_static_string("location-error-quark");
		g_set_error(err, quark, 2, "Use denied by settings");
		return;
	}

	mce_device_mode = priv->mce_device_mode;
	method = priv->sel_method;
	if (!mce_device_mode) {
		bus = priv->dbus;
		mce_device_mode = NULL;
		proxy = dbus_g_proxy_new_for_name(bus, "com.nokia.mce",
				"/com/nokia/mce/request", "com.nokia.mce.request");
		dbus_g_proxy_call(proxy, "get_device_mode", &ierr1,
				G_TYPE_INVALID, 0x40, &mce_device_mode,
				G_TYPE_INVALID);
		g_object_unref(proxy);
		if (ierr1) {
			g_log("liblocation", G_LOG_LEVEL_WARNING,
					"%s: %s", "update_mce_device_mode",
					ierr1->message);
			g_error_free(ierr1);
			g_signal_emit(control, signals[ERROR], 0);
			g_signal_emit(control, signals[ERROR_VERBOSE], 0, 4);
			return;
		}
		g_free(priv->mce_device_mode);
		priv->mce_device_mode = mce_device_mode;
	}

	if (g_strcmp0(mce_device_mode, "flight")
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
					(void (*)(void))on_positioning_activate_response, err);
			return;
		}
	} else {
		g_log("liblocation", G_LOG_LEVEL_DEBUG,
				"%s:%d: We are in offline mode now!", G_STRFUNC, __LINE__);

		if (g_strcmp0(priv->device, "las")) {
			register_dbus_signal_callback(control,
					"/com/nokia/location/ui/bt_disabled", NULL, NULL);
			g_log("liblocation", G_LOG_LEVEL_WARNING,
					"%s:%d: Offline mode, external device, giving up.\n",
					G_STRFUNC, __LINE__);
			g_signal_emit(control, signals[ERROR], 0);
			g_signal_emit(control, signals[ERROR_VERBOSE], 0, 3);
			return;
		}

		if (method && !(method & (LOCATION_METHOD_GNSS|LOCATION_METHOD_AGNSS))) { /* 0xC */
			g_log("liblocation", G_LOG_LEVEL_WARNING,
					"%s:%d: The method requested is not appropriate for offline mode",
					G_STRFUNC, __LINE__);
			g_signal_emit(control, signals[ERROR], 0);
			g_signal_emit(control, signals[ERROR_VERBOSE], 0, 3);
			return;
		}

		method = LOCATION_METHOD_GNSS; /* 4 */
	}

	g_log("liblocation", G_LOG_LEVEL_DEBUG,
			"%s:%d: application method set to 0x%x", G_STRFUNC, __LINE__,
			method);

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
							err);
					return;
				}
				goto lab43;
			}
lab60:
			if (!priv->location_ui_proxy)
				register_dbus_signal_callback(control,
						"/com/nokia/location/ui/enable_network",
						(void (*)(void))toggle_network,
						err);
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
					err);
		return;
	}

	method &= get_selected_method_wrap(priv);
	if (!method)
		goto lab46;

lab22:
	if (!LOCATION_IS_GPSD_CONTROL(control))
		g_assertion_message_expr("liblocation", __FILE__, __LINE__,
				G_STRFUNC, "LOCATION_IS_GPSD_CONTROL(control)");

	interval = priv->interval;

	if ((unsigned int)(method - 1) <= 2
			|| !g_strcmp0(control->priv->device, "las")) {
		if (!dbus_proxy_setup(control, method, interval, err))
			return;
		goto lab48;
	}

	bus = control->priv->dbus;
	mce_device_mode = NULL;
	hash_table = NULL;

	proxy = dbus_g_proxy_new_for_name(bus, "org.bluez", "/",
			"org.bluez.Manager");
	type = dbus_g_object_path_get_g_type();
	if (!dbus_g_proxy_call(proxy, "DefaultAdapter", &ierr1,
				G_TYPE_INVALID, type, &mce_device_mode,
				G_TYPE_INVALID)) {
		if (ierr1) {
			g_log("liblocation", G_LOG_LEVEL_WARNING,
					"Error getting bluetooth adapter: %s",
					ierr1->message);
			g_error_free(ierr1);
		}
		g_object_unref(proxy);
lab31:
		register_dbus_signal_callback(control,
				"/com/nokia/location/ui/bt_disabled", NULL, NULL);
		quark = g_quark_from_static_string("location-error-quark");
		g_set_error(err, quark, 4, "Bluetooth not available");
		return;
	}
	g_object_unref(proxy);

	proxy = dbus_g_proxy_new_for_name(control->priv->dbus,
			"org.bluez", mce_device_mode, "org.bluez.Adapter");
	type = g_value_get_type();
	hashtable_type = dbus_g_type_get_map("GHashTable", 0x40u, type);
	v21 = dbus_g_proxy_call(proxy, "GetProperties", &ierr1,
			G_TYPE_INVALID, hashtable_type, &hash_table,
			G_TYPE_INVALID);
	if (!v21) {
		if (ierr1) {
lab75:
			g_log("liblocation", G_LOG_LEVEL_WARNING,
					"Error getting bluetooth adapter properties: %s",
					ierr1->message);
			g_error_free(ierr1);
			goto lab76;
		}
		goto lab82;
	}
	if (!hash_table
			|| (v27 = (const GValue *)g_hash_table_lookup(hash_table, "Powered")) == 0
			|| (v21 = g_value_get_boolean(v27)) == 0) {
		g_log("liblocation", G_LOG_LEVEL_DEBUG,
				"Bluetooth adapter status: %s", "not powered");

		if (g_strcmp0(control->priv->mce_device_mode, "flight")
				&& g_strcmp0(control->priv->mce_device_mode, "offline")) {
			/* TODO: Sort this out */
			value.g_type = 0;
			*(&value.g_type + 1) = 0;
			value.data[0].v_int64 = 0LL;
			value.data[1].v_int64 = 0LL;
			g_value_init(&value, 0x14u);
			g_value_set_boolean(&value, TRUE);

			v21 = dbus_g_proxy_call(proxy, "SetProperty", &ierr1,
					0x40u, "Powered", type, &value, G_TYPE_INVALID,
					G_TYPE_INVALID);
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

	g_log("liblocation", G_LOG_LEVEL_DEBUG,
			"Bluetooth adapter status: %s", "powered");
lab76:
	if (hash_table)
		g_hash_table_unref(hash_table);
	g_free(mce_device_mode);
	g_object_unref(proxy);

	if (!v21)
		goto lab31;

	priv = control->priv;

	server_proxy = dbus_g_proxy_new_for_name(
			priv->dbus,
			priv->device_car,
			"/org/freedesktop/Gypsy",
			"org.freedesktop.Gypsy.Server");

	device = control->priv->device;
	priv->gypsy.server = server_proxy;

	if (dbus_g_proxy_call(server_proxy, "Create", err, 0x40u, device,
				G_TYPE_INVALID, type, &priv->gypsy.path,
				G_TYPE_INVALID)) {
		device_proxy = dbus_g_proxy_new_for_name(
				control->priv->dbus,
				control->priv->device_car,
				priv->gypsy.path,
				"org.freedesktop.Gypsy.Device");
		priv->gypsy.device = device_proxy;
		dbus_g_proxy_add_signal(device_proxy,
				"ConnectionStatusChanged", 0x14u, NULL);
		dbus_g_proxy_connect_signal(priv->gypsy.device,
				"ConnectionStatusChanged",
				(GCallback)connection_changed_cb,
				control,
				NULL);

		if (dbus_g_proxy_call(priv->gypsy.device, "Start", err,
					G_TYPE_INVALID, G_TYPE_INVALID)) {
			control->priv->field_48 = TRUE; /* is_started ? */
lab48:
			if (!priv->is_running) {
				g_signal_emit(control, signals[GPSD_RUNNING], 0);
				priv->is_running = TRUE;
			}

			return;
		}
	}
}

static void location_gpsd_control_prestart_internal(
		LocationGPSDControl *control, int unsure_ui_related)
{
	GError *error = NULL;

	location_gpsd_control_start_internal(control, unsure_ui_related, &error);
	if (error) {
		g_log("liblocation", G_LOG_LEVEL_WARNING, "%s: %s", G_STRFUNC,
				error->message);
		g_error_free(error);
		g_signal_emit(control, signals[ERROR], 0);
		g_signal_emit(control, signals[ERROR_VERBOSE], 0, 4);
	}
}

static int main_loop()
{
	while (g_main_context_pending(NULL))
		g_main_context_iteration(NULL, FALSE);

	return 1;
}

void location_gpsd_control_start(LocationGPSDControl *control)
{
	LocationGPSDControlPrivate *priv;
	GSource *gsource_mainloop;

	if (!LOCATION_IS_GPSD_CONTROL(control)) {
		g_return_if_fail_warning("liblocation", G_STRFUNC,
				"LOCATION_IS_GPSD_CONTROL(control)");
		return;
	}

	priv = control->priv;
	if (priv->ctx) {
		gsource_mainloop = g_timeout_source_new(1000);
		g_source_set_callback(gsource_mainloop, (GSourceFunc)main_loop,
				control, NULL);
		g_source_attach(gsource_mainloop, (GMainContext *)control->priv->ctx);
		priv = control->priv;
		priv->gsource_mainloop = gsource_mainloop;
	}

	priv->is_running = FALSE;
	location_gpsd_control_prestart_internal(control, TRUE);
}

static void get_location_method(LocationGPSDControl *control,
		const GConfValue *value)
{
	LocationGPSDControlPrivate *priv;
	GConfValue *car_ret, *cdr_ret;
	const gchar *cdr_str, *car_str;

	gboolean stopped = FALSE;

	priv = control->priv;
	if (priv->gypsy.server) {
		dbus_proxy_shutdown(&priv->gypsy);
		priv = control->priv;
		stopped = TRUE;
	}

	if (priv->device)
		g_free(priv->device);
	control->priv->device = NULL;

	if (priv->device_car)
		g_free(priv->device_car);
	control->priv->device_car = NULL;

	if (!value)
		goto out;

	car_ret = gconf_value_get_car(value);
	cdr_ret = gconf_value_get_cdr(value);

	if (car_ret) {
		if (cdr_ret || car_ret->type != GCONF_VALUE_STRING
				|| cdr_ret->type != GCONF_VALUE_STRING) {
out:
			if (stopped)
				g_signal_emit(control, signals[GPSD_STOPPED], 0);
		} else {
			priv = control->priv;
			cdr_str = gconf_value_get_string(cdr_ret);
			priv->device = g_strdup(cdr_str);
			car_str = gconf_value_get_string(car_ret);
			priv->device_car = g_strdup(car_str);

			if (stopped)
				location_gpsd_control_prestart_internal(control, TRUE);
		}
	}
}

static void device_mode_changed_cb(int unused, gchar *sig,
		LocationGPSDControl *control)
{
	LocationGPSDControlPrivate *priv;
	gchar *mce_device_mode, *_sig;

	priv = control->priv;
	mce_device_mode = priv->mce_device_mode;
	_sig = g_strdup(sig);
	priv = control->priv;
	priv->mce_device_mode = _sig;

	if (priv->gypsy.server
			&& g_strcmp0(mce_device_mode, priv->mce_device_mode)) {
		dbus_proxy_shutdown(&control->priv->gypsy);
		location_gpsd_control_prestart_internal(control, TRUE);
	}

	g_free(mce_device_mode);
}

static gboolean gconf_get_bool(gboolean *dest, const GConfValue *value)
{
	gboolean cur, ret, unchanged;

	if (value)
		cur = gconf_value_get_bool(value);
	else
		cur = FALSE;

	ret = *dest;
	unchanged = cur == *dest;

	if (cur == *dest)
		ret = FALSE;
	else
		*dest = cur;

	if (!unchanged)
		ret = TRUE;

	return ret;
}

static void on_gconf_changed(GConfClient *client, guint cnxn_id,
		GConfEntry *entry, GObject *object)
{
	gboolean v9;
	LocationGPSDControlPrivate *priv;
	GError *err = NULL;
	GQuark quark;

	if (!entry || !entry->key)
		g_assertion_message_expr("liblocation", __FILE__, __LINE__,
				G_STRFUNC, "entry && entry->key");

	if (!LOCATION_IS_GPSD_CONTROL(object))
		g_assertion_message_expr("liblocation", __FILE__, __LINE__,
				G_STRFUNC, "LOCATION_IS_GPSD_CONTROL(obj)");

	priv = G_TYPE_INSTANCE_GET_PRIVATE(LOCATION_GPSD_CONTROL(object),
			G_TYPE_OBJECT, LocationGPSDControlPrivate);

	if (g_str_equal(entry->key, GCONF_METHOD)) {
		get_location_method(LOCATION_GPSD_CONTROL(object), entry->value);
		return;
	} else if (g_str_equal(entry->key, GCONF_GPS_DISABLED))
		v9 = gconf_get_bool(&priv->gps_disabled, entry->value);
	else if (g_str_equal(entry->key, GCONF_NET_DISABLED))
		v9 = gconf_get_bool(&priv->net_disabled, entry->value);
	else if (g_str_equal(entry->key, GCONF_DISCLAIMER_ACCEPTED))
		v9 = gconf_get_bool(&priv->disclaimer_accepted, entry->value);
	else
		return;

	if (v9) {
		if (priv->gypsy.server) {
			dbus_proxy_shutdown(&priv->gypsy);
			location_gpsd_control_start_internal(LOCATION_GPSD_CONTROL(object),
					FALSE, &err);
			if (err) {
				g_signal_emit(LOCATION_GPSD_CONTROL(object),
						signals[ERROR], 0);
				quark = g_quark_from_static_string("location-error-quark");

				// LocationGPSDControlError below?
				if (g_error_matches(err, quark, 2)) {
					g_signal_emit(LOCATION_GPSD_CONTROL(object),
							signals[ERROR_VERBOSE], 0, 1);
				} else {
					g_signal_emit(LOCATION_GPSD_CONTROL(object),
							signals[ERROR_VERBOSE], 0, 4);
				}

				g_log("liblocation", G_LOG_LEVEL_WARNING,
						"Failed to update connection: %s",
						err->message);
				g_error_free(err);
				g_signal_emit(LOCATION_GPSD_CONTROL(object),
						signals[GPSD_STOPPED], 0);
			}
		}
	}
}

void location_gpsd_control_stop(LocationGPSDControl *control)
{
	LocationGPSDControlPrivate *priv;

	if (!LOCATION_IS_GPSD_CONTROL(control)) {
		g_return_if_fail_warning("liblocation", G_STRFUNC,
				"LOCATION_IS_GPSD_CONTROL(control)");
		return;
	}

	priv = control->priv;
	if (priv->ctx) {
		if (!g_source_is_destroyed(priv->gsource_mainloop))
			g_source_destroy(priv->gsource_mainloop);
		priv = control->priv;
	}

	if (priv->gypsy.server) {
		dbus_proxy_shutdown(&priv->gypsy);
		g_signal_emit(control, signals[GPSD_STOPPED], 0);
		dbus_proxy_close(control->priv);
	} else {
		dbus_proxy_close(priv);
	}
}

void location_gpsd_control_request_status(LocationGPSDControl *control)
{
	g_log("liblocation", G_LOG_LEVEL_WARNING,
			"You don't need to call %s, it does nothing anymore!",
			G_STRFUNC);
}



gint location_gpsd_control_get_allowed_methods(LocationGPSDControl *control)
{
	LocationGPSDControlPrivate *priv;

	priv = G_TYPE_INSTANCE_GET_PRIVATE(LOCATION_GPSD_CONTROL(control),
			G_TYPE_OBJECT, LocationGPSDControlPrivate);
	return get_selected_method(priv, 0);
}

static void location_gpsd_control_class_dispose(GObject *object)
{
	LocationGPSDControlPrivate *priv;

	priv = G_TYPE_INSTANCE_GET_PRIVATE(LOCATION_GPSD_CONTROL(object),
			G_TYPE_OBJECT, LocationGPSDControlPrivate);

	if (priv->gconf) {
		gconf_client_notify_remove(priv->gconf, priv->client_id);
		g_object_unref(priv->gconf);
		priv->gconf = NULL;
	}

	location_gpsd_control_stop(LOCATION_GPSD_CONTROL(object));

	if (priv->device)
		g_free(priv->device);
	priv->device = NULL;

	if (priv->device_car)
		g_free(priv->device_car);
	priv->device_car = NULL;

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
		g_assertion_message_expr("liblocation", __FILE__, __LINE__,
				G_STRFUNC, "priv->gconf == NULL");

	if (priv->dbus)
		g_assertion_message_expr("liblocation", __FILE__, __LINE__,
				G_STRFUNC, "priv->dbus == NULL");

	if (priv->location_ui_proxy)
		g_assertion_message_expr("liblocation", __FILE__, __LINE__,
				G_STRFUNC, "priv->location_ui_proxy == NULL");

	if (priv->device)
		g_assertion_message_expr("liblocation", __FILE__, __LINE__,
				G_STRFUNC, "priv->device == NULL");
}

static void location_gpsd_control_class_set_property(GObject *object,
		guint property_id, const GValue *value, GParamSpec *pspec)
{
	GTypeClass *class;
	const gchar *list, *klass_type, *parent_type;

	class = g_type_check_instance_cast(&object->g_type_instance,
			LOCATION_TYPE_GPSD_CONTROL)[3].g_class;

	switch (property_id) {
		case 2u:
			class[17].g_type = g_value_get_int(value);
			break;
		case 3u:
			class->g_type = (GType)g_value_get_pointer(value);
			break;
		case 1u:
			class[16].g_type = g_value_get_int(value);
			break;
		default:
			list = pspec->name;
			klass_type = g_type_name(pspec->g_type_instance.g_class->g_type);
			parent_type = g_type_name(object->g_type_instance.g_class->g_type);
			g_log("liblocation", G_LOG_LEVEL_WARNING,
					"%s: invalid %s id %u for \"%s\" of type `%s' in `%s'",
					G_STRLOC, "property", property_id, list,
					klass_type, parent_type);
			break;
	}
}

static void location_gpsd_control_class_get_property(GObject *object,
		guint property_id, GValue *value, GParamSpec *pspec)
{
	GTypeClass *class;
	const gchar *list, *klass_type, *parent_type;

	class = g_type_check_instance_cast(&object->g_type_instance,
			LOCATION_TYPE_GPSD_CONTROL)[3].g_class;

	switch (property_id) {
		case 2u:
			g_value_set_int(value, class[17].g_type);
			break;
		case 3u:
			break;
		case 1u:
			g_value_set_int(value, class[16].g_type);
			break;
		default:
			list = pspec->name;
			klass_type = g_type_name(pspec->g_type_instance.g_class->g_type);
			parent_type = g_type_name(object->g_type_instance.g_class->g_type);
			g_log("liblocation", G_LOG_LEVEL_WARNING,
					"%s: invalid %s id %u for \"%s\" of type `%s' in `%s'",
					G_STRLOC, "property", property_id, list,
					klass_type, parent_type);
			break;
	}
}

static void location_gpsd_control_class_init(LocationGPSDControlClass *klass)
{
	GParamSpec *method, *interval, *maincontext;

	klass->parent_class.dispose = location_gpsd_control_class_dispose;
	klass->parent_class.set_property = location_gpsd_control_class_set_property;
	klass->parent_class.get_property = location_gpsd_control_class_get_property;

	signals[ERROR] = g_signal_new("error",
			klass->parent_class.g_type_class.g_type,
			G_SIGNAL_NO_RECURSE|G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET(LocationGPSDControlClass, error),
			0, NULL, (GSignalCMarshaller)&g_cclosure_marshal_VOID__VOID,
			G_TYPE_UCHAR, 0);

	signals[ERROR_VERBOSE] = g_signal_new("error-verbose",
			klass->parent_class.g_type_class.g_type,
			G_SIGNAL_NO_RECURSE|G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET(LocationGPSDControlClass, error_verbose),
			0, NULL, (GSignalCMarshaller)&g_cclosure_marshal_VOID__INT,
			G_TYPE_UCHAR, 1, 24);

	signals[GPSD_RUNNING] = g_signal_new("gpsd-running",
			klass->parent_class.g_type_class.g_type,
			G_SIGNAL_NO_RECURSE|G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET(LocationGPSDControlClass, gpsd_running),
			0, NULL, (GSignalCMarshaller)&g_cclosure_marshal_VOID__VOID,
			G_TYPE_UCHAR, 0);

	signals[GPSD_STOPPED] = g_signal_new("gpsd-stopped",
			klass->parent_class.g_type_class.g_type,
			G_SIGNAL_NO_RECURSE|G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET(LocationGPSDControlClass, gpsd_stopped),
			0, NULL, (GSignalCMarshaller)&g_cclosure_marshal_VOID__VOID,
			G_TYPE_UCHAR, 0);

	method = g_param_spec_int("preferred-method", "Preferred method",
			"The positioning method the application would like to use.",
			0, 15, 0, G_PARAM_WRITABLE|G_PARAM_READABLE);
	g_object_class_install_property(&klass->parent_class, 1u, method);

	interval = g_param_spec_int("preferred-interval", "Preferred interval",
			"The interval between fixes the application prefers.",
			-1, 0x7FFFFFFF, -1, G_PARAM_WRITABLE|G_PARAM_READABLE);
	g_object_class_install_property(&klass->parent_class, 2u, interval);

	maincontext = g_param_spec_pointer("maincontext-pointer",
			"The pointer to the GMainContext instance",
			"Set the main context adddress.", G_PARAM_WRITABLE);
	g_object_class_install_property(&klass->parent_class, 3u, maincontext);
}

static void location_gpsd_control_init(LocationGPSDControl *control)
{
	LocationGPSDControlPrivate *priv;
	DBusGConnection *bus;
	DBusGProxy *proxy;
	GConfValue *method;

	priv = G_TYPE_INSTANCE_GET_PRIVATE(control, G_TYPE_OBJECT,
			LocationGPSDControlPrivate);

	control->priv = priv;
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

	proxy = dbus_g_proxy_new_for_name(priv->dbus, "com.nokia.mce",
			"/com/nokia/mce/signal", "com.nokia.mce.signal");
	dbus_g_proxy_add_signal(proxy, "sig_device_mode_ind", 0x40u, 0);
	dbus_g_proxy_connect_signal(proxy, "sig_device_mode_ind",
			(GCallback)device_mode_changed_cb, control, NULL);
	priv->cdr_method = proxy;
}
