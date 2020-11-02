#include <math.h>
#include <stdio.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <gconf/gconf-client.h>
#include <glib.h>

#include "location-gps-device.h"

#define GCONF_LK       "/system/nokia/location/lastknown"
#define GCONF_LK_TIME  GCONF_LK"/time"
#define GCONF_LK_LAT   GCONF_LK"/latitude"
#define GCONF_LK_LON   GCONF_LK"/longitude"
#define GCONF_LK_ALT   GCONF_LK"/altitude"
#define GCONF_LK_TRK   GCONF_LK"/track"
#define GCONF_LK_SPD   GCONF_LK"/speed"
#define GCONF_LK_CLB   GCONF_LK"/climb"

enum {
	DEVICE_CHANGED,
	DEVICE_CONNECTED,
	DEVICE_DISCONNECTED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {};

struct _LocationGPSDevicePrivate
{
	DBusConnection *bus;
	char *foo;
	LocationGPSDeviceFix *fix;
	guint32 fields;
	double time;
	double ept;
	double latitude;
	double longitude;
	double eph;
	double altitude;
	double epv;
	double track;
	double epd;
	double speed;
	double eps;
	double climb;
	double epc;
	double pitch;
	double roll;
	double dip;
	//LocationCellInfo *cell_info;
	// TODO: make cellinfo_flags .. wcdma_ucid into an inline struct called
	// cell_info
	int cellinfo_flags;
	guint16 gsm_mcc;
	guint16 gsm_mnc;
	guint16 gsm_lac;
	guint16 gsm_cellid;
	guint16 wcdma_mcc;
	guint16 wcdma_mnc;
	guint32 wcdma_ucid;
	gboolean sig_pending;
};

G_DEFINE_TYPE_WITH_PRIVATE(LocationGPSDevice, location_gps_device, G_TYPE_OBJECT);

static int gconf_get_float(GConfClient *client, double *dest, const gchar *key)
{
	GConfValue *val;
	int ret = 0;

	val = gconf_client_get(client, key, NULL);
	if (!val)
		return ret;

	if (val->type == GCONF_VALUE_FLOAT) {
		ret = 1;
		*dest = gconf_value_get_float(val);
	}

	gconf_value_free(val);
	return ret;
}

static GPtrArray *free_satellites(LocationGPSDevice *device)
{
	GPtrArray *result;

	result = device->satellites;
	if (result) {
		g_ptr_array_foreach(result, (GFunc)&g_free, NULL);
		g_ptr_array_free(device->satellites, TRUE);
		device->satellites = NULL;
	}

	device->satellites_in_view = 0;
	device->satellites_in_use = 0;

	return result;
}

static void store_lastknown_in_gconf(LocationGPSDevice *device)
{
	LocationGPSDeviceFix *fix = device->fix;
	GConfClient *client = gconf_client_get_default();

	if (fix->fields & LOCATION_GPS_DEVICE_TIME_SET)
		gconf_client_set_float(client, GCONF_LK_TIME, fix->time, NULL);
	else
		gconf_client_unset(client, GCONF_LK_TIME, NULL);

	if (fix->fields & LOCATION_GPS_DEVICE_LATLONG_SET) {
		gconf_client_set_float(client, GCONF_LK_LAT, fix->latitude, NULL);
		gconf_client_set_float(client, GCONF_LK_LON, fix->longitude, NULL);
	} else {
		gconf_client_unset(client, GCONF_LK_LAT, NULL);
		gconf_client_unset(client, GCONF_LK_LON, NULL);
	}

	if (fix->fields & LOCATION_GPS_DEVICE_ALTITUDE_SET)
		gconf_client_set_float(client, GCONF_LK_ALT, fix->altitude, NULL);
	else
		gconf_client_unset(client, GCONF_LK_ALT, NULL);

	if (fix->fields & LOCATION_GPS_DEVICE_TRACK_SET)
		gconf_client_set_float(client, GCONF_LK_TRK, fix->track, NULL);
	else
		gconf_client_unset(client, GCONF_LK_TRK, NULL);

	if (fix->fields & LOCATION_GPS_DEVICE_SPEED_SET)
		gconf_client_set_float(client, GCONF_LK_SPD, fix->speed, NULL);
	else
		gconf_client_unset(client, GCONF_LK_SPD, NULL);

	if (fix->fields & LOCATION_GPS_DEVICE_CLIMB_SET)
		gconf_client_set_float(client, GCONF_LK_CLB, fix->climb, NULL);
	else
		gconf_client_unset(client, GCONF_LK_CLB, NULL);

	g_object_unref(client);
}

static int signal_changed(LocationGPSDevice *device)
{
	device->priv->sig_pending = FALSE;
	g_signal_emit(device, signals[DEVICE_CHANGED], 0);
	g_object_unref(device);
	return 0;
}

static void add_g_timeout_interval(LocationGPSDevice *device)
{
	if (!device->priv->sig_pending) {
		g_object_ref(device);
		g_timeout_add(300, (GSourceFunc)signal_changed, device);
		device->priv->sig_pending = TRUE;
	}
}

static dbus_bool_t set_fix_status(LocationGPSDevice *device, DBusMessage *msg)
{
	dbus_bool_t result;
	LocationGPSDeviceFix *fix;
	LocationGPSDeviceMode mode;

	result = dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &mode,
			DBUS_TYPE_INVALID);

	if (result) {
		fix = device->fix;
		device->status = mode > 1;
		fix->mode = mode;
		add_g_timeout_interval(device);
	}

	return result;
}

static dbus_bool_t set_position(LocationGPSDevice *device, DBusMessage *msg)
{
	dbus_bool_t result;
	LocationGPSDeviceFix *fix;
	double latitude, longitude, altitude;
	int mode, time;

	result = dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &mode,
			DBUS_TYPE_INT32, &time,
			DBUS_TYPE_DOUBLE, &latitude,
			DBUS_TYPE_DOUBLE, &longitude,
			DBUS_TYPE_DOUBLE, &altitude,
			DBUS_TYPE_INVALID);

	if (result) {
		if (mode & LOCATION_GPS_DEVICE_MODE_2D) { /* if ((mode & 6) == 6) { */
			fix = device->fix;
			fix->latitude = latitude;
			fix->longitude = longitude;
			fix->fields |= LOCATION_GPS_DEVICE_LATLONG_SET;
		} else {
			fix = device->fix;
			fix->fields &= LOCATION_GPS_DEVICE_LATLONG_SET;
		}

		if (mode & LOCATION_GPS_DEVICE_MODE_3D) { /* if ((mode & 8) == 8) { */
			fix->altitude = altitude;
			fix->fields |= LOCATION_GPS_DEVICE_ALTITUDE_SET;
		} else {
			fix->fields &= LOCATION_GPS_DEVICE_ALTITUDE_SET;
		}

		if (time) {
			fix->fields |= LOCATION_GPS_DEVICE_TIME_SET;
			fix->time = (double)time;
		} else {
			fix->fields &= LOCATION_GPS_DEVICE_TIME_SET;
		}

		add_g_timeout_interval(device);
	}

	return result;
}

static dbus_bool_t set_accuracy(LocationGPSDevice *device, DBusMessage *msg)
{
	dbus_bool_t result;
	LocationGPSDeviceFix *fix;
	double epv, eph, epd, epc, eps, unused;
	int mode;

	result = dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &mode,
		DBUS_TYPE_DOUBLE, &unused,
		DBUS_TYPE_DOUBLE, &eps,
		DBUS_TYPE_DOUBLE, &epc,
		DBUS_TYPE_INVALID);

	if (result) {
		/* TODO: Use enums? */
		if (mode & 2) {
			fix = device->fix;
			eph = eps;
			epd = eps * 10.0;
			fix->eps = eps * 10.0 / 10.0;
			fix->eph = eph * 500.0;
			fix->epd = atan(epd / 500.0);
			if (!(mode & 4))
				goto end;
		} else {
			fix = device->fix;
			if (!(mode & 4))
				goto end;
		}

		epv = epc * 5.0;
		fix->epc = epc * 10.0 / 10.0;
		fix->epv = epv;
end:
		fix->ept = 0.0;
		add_g_timeout_interval(device);
	}

	return result;
}

static dbus_bool_t set_course(LocationGPSDevice *device, DBusMessage *msg)
{
	dbus_bool_t result;
	LocationGPSDeviceFix *fix;
	double climb, knots, track;
	int mode, unused;
	unsigned int fields;

	result = dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &mode,
			DBUS_TYPE_INT32, &unused,
			DBUS_TYPE_DOUBLE, &knots,
			DBUS_TYPE_DOUBLE, &track,
			DBUS_TYPE_DOUBLE, &climb,
			DBUS_TYPE_INVALID);

	if (result) {
		fix = device->fix;
		fields = fix->fields & 0xFFFFFFF1;
		fix->fields = fields;

		if (!((mode & LOCATION_GPS_DEVICE_SPEED_SET) == 0)) {
			fix->fields = (fields|LOCATION_GPS_DEVICE_SPEED_SET);
			fix->speed = knots * 1.852;
		}

		if (mode & LOCATION_GPS_DEVICE_TRACK_SET) {
			fix->fields |= LOCATION_GPS_DEVICE_TRACK_SET;
			fix->track = track;
		}

		if (mode & LOCATION_GPS_DEVICE_CLIMB_SET) {
			fix->fields |= LOCATION_GPS_DEVICE_CLIMB_SET;
			fix->climb = climb;
		}

		add_g_timeout_interval(device);
	}

	return result;
}

static int iterate_dbus_struct_by_type(DBusMessageIter *iter, int type, ...)
{
	/* TODO: Review if this function is actually correct.
	 * It does not represent exactly what was found */
	void **value = NULL;
	va_list va, va_args;

	va_start(va_args, type);
	va_copy(va, va_args);

	if (!type)
		return 1;

	while (type == dbus_message_iter_get_arg_type(iter)) {
		dbus_message_iter_get_basic(iter, *value);
		dbus_message_iter_next(iter);
		if (!type)
			return 1;
	}

	va_end(va_args);

	/* g_log("liblocation", G_LOG_LEVEL_WARNING, "Got %d, expected %d"); */
	return 0;
}

static dbus_bool_t set_satellites(LocationGPSDevice *device, DBusMessage *msg)
{
	dbus_bool_t result;
	GPtrArray *satarray;
	DBusMessageIter iter, sub, subsub;
	int prn, in_use, elevation, azimuth, signal_strength;
	LocationGPSDeviceSatellite *sat;

	result = dbus_message_iter_init(msg, &iter);

	if (result) {
		result = dbus_message_iter_get_arg_type(&iter);
		if (result == DBUS_TYPE_ARRAY) {
			free_satellites(device);
			satarray = g_ptr_array_new();
			device->cell_info = NULL;
			device->satellites = satarray;
			dbus_message_iter_recurse(&iter, &sub);

			while (dbus_message_iter_get_arg_type(&sub) == DBUS_TYPE_STRUCT) {
				dbus_message_iter_recurse(&sub, &subsub);
				if (iterate_dbus_struct_by_type(&subsub,
							DBUS_TYPE_UINT32, &prn,
							DBUS_TYPE_BOOLEAN, &in_use,
							DBUS_TYPE_UINT32, &elevation,
							DBUS_TYPE_UINT32, &azimuth,
							DBUS_TYPE_UINT32, &signal_strength,
							DBUS_TYPE_INVALID)) {
					sat = g_malloc(sizeof(LocationGPSDeviceSatellite));
					sat->prn = prn;
					sat->in_use = in_use;
					sat->elevation = elevation;
					sat->azimuth = azimuth;
					sat->signal_strength = signal_strength;
					g_ptr_array_add(device->satellites, sat);
					++device->satellites_in_view;
					if (in_use)
						++device->satellites_in_use;
				} else {
					g_log("liblocation", G_LOG_LEVEL_WARNING,
							"Failed to parse sat info from dbus message!");
				}

				dbus_message_iter_next(&sub);
			}

			add_g_timeout_interval(device);
		}
	}

	return result;
}

static void get_values_from_gypsy(LocationGPSDevice *device,
		const gchar *bus_name, gchar *type)
{
	LocationGPSDevicePrivate *priv;
	DBusMessage *cmd, *msg;
	gchar *str;

	g_log("liblocation", G_LOG_LEVEL_DEBUG,
			"Loading initial values from %s::%s", bus_name, type);

	priv = device->priv;

	cmd = dbus_message_new_method_call(bus_name, "/org/freedesktop/Gypsy",
			"org.freedesktop.Gypsy.Server", "Create");
	dbus_message_append_args(cmd, DBUS_TYPE_STRING, &type, DBUS_TYPE_INVALID);
	msg = dbus_connection_send_with_reply_and_block(priv->bus, cmd,
			DBUS_TIMEOUT_USE_DEFAULT, NULL);
	dbus_message_unref(cmd);

	if (msg) {
		if (dbus_message_get_args(msg, NULL, DBUS_TYPE_OBJECT_PATH)) {
			str = g_strdup(str);
			dbus_message_unref(msg);
			g_log("liblocation", G_LOG_LEVEL_DEBUG,
					"Object path: %s", str);

			cmd = dbus_message_new_method_call(bus_name, str,
					"org.freedesktop.Gypsy.Device", "GetFixStatus");
			msg = dbus_connection_send_with_reply_and_block(priv->bus, cmd,
					DBUS_TIMEOUT_USE_DEFAULT, NULL);
			if (msg) {
				set_fix_status(device, msg);
				dbus_message_unref(msg);
			}
			dbus_message_unref(cmd);

			cmd = dbus_message_new_method_call(bus_name, str,
					"org.freedesktop.Gypsy.Position", "GetPosition");
			msg = dbus_connection_send_with_reply_and_block(priv->bus, cmd,
					DBUS_TIMEOUT_USE_DEFAULT, NULL);
			if (msg) {
				set_position(device, msg);
				dbus_message_unref(msg);
			}
			dbus_message_unref(cmd);

			cmd = dbus_message_new_method_call(bus_name, str,
					"org.freedesktop.Gypsy.Accuracy", "GetAccuracy");
			msg = dbus_connection_send_with_reply_and_block(priv->bus, cmd,
					DBUS_TIMEOUT_USE_DEFAULT, NULL);
			if (msg) {
				set_accuracy(device, msg);
				dbus_message_unref(msg);
			}
			dbus_message_unref(cmd);

			cmd = dbus_message_new_method_call(bus_name, str,
					"org.freedesktop.Gypsy.Course", "GetCourse");
			msg = dbus_connection_send_with_reply_and_block(priv->bus, cmd,
					DBUS_TIMEOUT_USE_DEFAULT, NULL);
			if (msg) {
				set_course(device, msg);
				dbus_message_unref(msg);
			}
			dbus_message_unref(cmd);

			cmd = dbus_message_new_method_call(bus_name, str,
					"org.freedesktop.Gypsy.Satellite", "GetSatellites");
			msg = dbus_connection_send_with_reply_and_block(priv->bus, cmd,
					DBUS_TIMEOUT_USE_DEFAULT, NULL);
			if (msg) {
				set_satellites(device, msg);
				dbus_message_unref(msg);
			}
			dbus_message_unref(cmd);

			cmd = dbus_message_new_method_call(bus_name,
					"/org/freedesktop/Gypsy", "org.freedesktop.Gypsy.Server",
					"Shutdown");
			dbus_message_append_args(cmd, DBUS_TYPE_OBJECT_PATH, &str,
					DBUS_TYPE_INVALID);
			msg = dbus_connection_send_with_reply_and_block(priv->bus, cmd,
					DBUS_TIMEOUT_USE_DEFAULT, NULL);
			if (msg)
				dbus_message_unref(msg);
			dbus_message_unref(cmd);
			g_free(str);
		} else {
			dbus_message_unref(msg);
		}
	}
}

static int on_gypsy_signal(int unused, DBusMessage *msg, gpointer obj)
{
	LocationGPSDevice *device;
	LocationGPSDevicePrivate *priv;
	DBusError error;
	gboolean online;
	unsigned int fix_fields;
	double ept, eph, epv, epd, eps, epc;
	LocationGPSDeviceFix *fix;
	int flags;
	guint16 *gsm_cellinfo_r;
	guint32 *wcdma_cellinfo_r;
	guint32 gsm_fields = 0;
	guint32 wcdma_fields = 0;

	if (LOCATION_IS_GPS_DEVICE(obj)) {
		if (dbus_message_is_signal(msg, "org.freedesktop.Gypsy.Position",
					"PositionChanged")) {
			set_position(LOCATION_GPS_DEVICE(obj), msg);
			return 1;
		}
	} else {
		g_assertion_message_expr("liblocation", __FILE__, __LINE__,
				G_STRFUNC, "LOCATION_IS_GPS_DEVICE(device)");
	}

	device = LOCATION_GPS_DEVICE(obj);

	fix = device->fix;

	if (dbus_message_is_signal(msg, "org.freedesktop.Gypsy.Course",
				"CourseChanged")) {
		set_course(device, msg);
		return 1;
	}

	if (dbus_message_is_signal(msg, "org.freedesktop.Gypsy.Satellite",
				"SatellitesChanged")) {
		set_satellites(device, msg);
		return 1;
	}

	if (!dbus_message_is_signal(msg, "org.freedesktop.Gypsy.Device",
				"ConnectionStatusChanged")) {
		if (dbus_message_is_signal(msg, "org.freedesktop.Gypsy.Device",
					"FixStatusChanged")) {
			set_fix_status(device, msg);
			return 1;
		}

		if (dbus_message_is_signal(msg, "org.freedesktop.Gypsy.Device",
					"AccuracyChanged")) {
			set_accuracy(device, msg);
			return 1;
		}

		if (dbus_message_is_signal(msg, "com.nokia.Location.Cell",
					"CellInfoChanged")) {
			dbus_error_init(&error);
			if (!dbus_message_get_args(msg, &error, DBUS_TYPE_INT32, &flags,
						DBUS_TYPE_ARRAY, DBUS_TYPE_UINT16, &gsm_cellinfo_r, &gsm_fields,
						DBUS_TYPE_ARRAY, DBUS_TYPE_UINT32, &wcdma_cellinfo_r, wcdma_fields,
						NULL)) {
				g_log("liblocation", G_LOG_LEVEL_WARNING,
						"Failed to parse CellInfo from DBus message: %s",
						error.message);
				dbus_error_free(&error);
				return 1;
			}

			priv = device->priv;
			device->cell_info = priv->cell_info; // XXX: when we change _LocationGPSDevicePrivate, change this to assign to the address of priv->cell_info instead

			priv->cell_info->flags = flags;
			if ((flags & LOCATION_CELL_INFO_GSM_CELL_INFO_SET) && (gsm_fields > 3)) {
				priv->gsm_mcc = gsm_cellinfo_r[0];
				priv->gsm_mnc = gsm_cellinfo_r[1];
				priv->gsm_lac = gsm_cellinfo_r[2];
				priv->gsm_cellid = gsm_cellinfo_r[3];
			}
			if ((flags & LOCATION_CELL_INFO_WCDMA_CELL_INFO_SET) && (wcdma_fields > 2)) {
				priv->wcdma_mcc = (guint16)wcdma_cellinfo_r[0];
				priv->wcdma_mnc = (guint16)wcdma_cellinfo_r[1];
				priv->wcdma_ucid = wcdma_cellinfo_r[2];
			}


		} else {
			if (!dbus_message_is_signal(msg, "com.nokia.Location.Uncertainty",
						"UncertaintyChanged")
					|| !dbus_message_get_args(msg, NULL,
						DBUS_TYPE_INT32, &fix_fields,
						DBUS_TYPE_DOUBLE, &ept,
						DBUS_TYPE_DOUBLE, &eph,
						DBUS_TYPE_DOUBLE, &epv,
						DBUS_TYPE_DOUBLE, &epd,
						DBUS_TYPE_DOUBLE, &eps,
						DBUS_TYPE_DOUBLE, &epc,
						DBUS_TYPE_INVALID)) {
				return 1;
			}
			if (fix_fields & LOCATION_GPS_DEVICE_ALTITUDE_SET)
				fix->ept = ept;
			if (fix_fields & LOCATION_GPS_DEVICE_SPEED_SET)
				fix->eph = eph;
			if (fix_fields & LOCATION_GPS_DEVICE_TRACK_SET)
				fix->epv = epv * 0.5;
			if (fix_fields & LOCATION_GPS_DEVICE_CLIMB_SET)
				fix->epd = epd * 0.01;
			if (fix_fields & LOCATION_GPS_DEVICE_LATLONG_SET)
				fix->eps = eps * 0.036;
			if (fix_fields & LOCATION_GPS_DEVICE_TIME_SET)
				fix->epc = epc * 0.01;
		}

		add_g_timeout_interval(LOCATION_GPS_DEVICE(obj));
		return 1;
	}

	if (dbus_message_get_args(msg, NULL, DBUS_TYPE_BOOLEAN, &online,
				DBUS_TYPE_INVALID)) {
		int sig;
		if (online)
			sig = DEVICE_CONNECTED;
		else
			sig = DEVICE_DISCONNECTED;

		LOCATION_GPS_DEVICE(obj)->online = online;
		LOCATION_GPS_DEVICE(obj)->status = LOCATION_GPS_DEVICE_STATUS_FIX;
		g_signal_emit(LOCATION_GPS_DEVICE(obj), signals[DEVICE_CHANGED+sig], 0);
		if (!online)
			store_lastknown_in_gconf(LOCATION_GPS_DEVICE(obj));
	}

	return 1;
}

void location_gps_device_reset_last_known(LocationGPSDevice *device)
{
	LocationGPSDeviceFix *fix;
	GConfClient *client;

	if (!LOCATION_IS_GPS_DEVICE(device)) {
		g_log("liblocation", G_LOG_LEVEL_CRITICAL,
				"%s: assertion '%s' failed", G_STRFUNC,
				"LOCATION_IS_GPS_DEVICE(device)");
		return;
	}

	fix = device->fix;
	client = gconf_client_get_default();

	device->status = LOCATION_GPS_DEVICE_STATUS_NO_FIX;

	fix->mode = LOCATION_GPS_DEVICE_MODE_NOT_SEEN;
	fix->fields = LOCATION_GPS_DEVICE_NONE_SET;
	fix->dip = LOCATION_GPS_DEVICE_NAN;
	fix->time = LOCATION_GPS_DEVICE_NAN;
	fix->ept = LOCATION_GPS_DEVICE_NAN;
	fix->latitude = LOCATION_GPS_DEVICE_NAN;
	fix->longitude = LOCATION_GPS_DEVICE_NAN;
	fix->eph = LOCATION_GPS_DEVICE_NAN;
	fix->altitude = LOCATION_GPS_DEVICE_NAN;
	fix->epv = LOCATION_GPS_DEVICE_NAN;
	fix->track = LOCATION_GPS_DEVICE_NAN;
	fix->epd = LOCATION_GPS_DEVICE_NAN;
	fix->speed = LOCATION_GPS_DEVICE_NAN;
	fix->eps = LOCATION_GPS_DEVICE_NAN;
	fix->climb = LOCATION_GPS_DEVICE_NAN;
	fix->epc = LOCATION_GPS_DEVICE_NAN;
	fix->pitch = LOCATION_GPS_DEVICE_NAN;
	fix->roll = LOCATION_GPS_DEVICE_NAN;

	free_satellites(device);
	gconf_client_recursive_unset(client, GCONF_LK, 0, NULL);
	g_signal_emit(device, signals[DEVICE_CHANGED], 0);
	g_object_unref(client);
}

void location_gps_device_start(LocationGPSDevice *device)
{
	g_log("liblocation", G_LOG_LEVEL_WARNING,
			"You don't need to call %s, it does nothing anymore!",
			G_STRFUNC);
}

void location_gps_device_stop(LocationGPSDevice *device)
{
	g_log("liblocation", G_LOG_LEVEL_WARNING,
			"You don't need to call %s, it does nothing anymore!",
			G_STRFUNC);
}

static void location_gps_device_finalize(GObject *object)
{
	free_satellites(LOCATION_GPS_DEVICE(object));
	store_lastknown_in_gconf(LOCATION_GPS_DEVICE(object));
}

static void location_gps_device_dispose(GObject *object)
{
	LocationGPSDevicePrivate *priv;

	priv = G_TYPE_INSTANCE_GET_PRIVATE(LOCATION_GPS_DEVICE(object),
			G_TYPE_OBJECT, LocationGPSDevicePrivate);

	if (priv->bus) {
		dbus_bus_remove_match(priv->bus,
				"type='signal',interface='org.freedesktop.Gypsy.Device'",
				NULL);
		dbus_bus_remove_match(priv->bus,
				"type='signal',interface='org.freedesktop.Gypsy.Position'",
				NULL);
		dbus_bus_remove_match(priv->bus,
				"type='signal',interface='org.freedesktop.Gypsy.Course'",
				NULL);
		dbus_bus_remove_match(priv->bus,
				"type='signal',interface='org.freedesktop.Gypsy.Satellite'",
				NULL);
		dbus_bus_remove_match(priv->bus,
				"type='signal',interface='org.freedesktop.Gypsy.Accuracy'",
				NULL);
		dbus_bus_remove_match(priv->bus,
				"type='signal',interface='com.nokia.Location.Cell'",
				NULL);
		dbus_bus_remove_match(priv->bus,
				"type='signal',interface='com.nokia.Location.Uncertainty'",
				NULL);
		dbus_connection_remove_filter(priv->bus,
				(DBusHandleMessageFunction)on_gypsy_signal,
				LOCATION_GPS_DEVICE(object));
		dbus_connection_close(priv->bus);
		dbus_connection_unref(priv->bus);
		priv->bus = NULL;
	}
}

static void location_gps_device_class_init(LocationGPSDeviceClass *klass)
{
	klass->parent_class.finalize = (void (*)(GObject *))location_gps_device_finalize;
	klass->parent_class.dispose = (void (*)(GObject *))location_gps_device_dispose;

	signals[DEVICE_CHANGED] = g_signal_new("changed",
			LOCATION_TYPE_GPS_DEVICE,
			G_SIGNAL_NO_RECURSE|G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET(LocationGPSDeviceClass, changed),
			0, NULL, (GSignalCMarshaller)&g_cclosure_marshal_VOID__VOID,
			G_TYPE_UCHAR, 0);

	signals[DEVICE_CONNECTED] = g_signal_new("connected",
			LOCATION_TYPE_GPS_DEVICE,
			G_SIGNAL_NO_RECURSE|G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET(LocationGPSDeviceClass, connected),
			0, NULL, (GSignalCMarshaller)&g_cclosure_marshal_VOID__VOID,
			G_TYPE_UCHAR, 0);

	signals[DEVICE_DISCONNECTED] = g_signal_new("disconnected",
			LOCATION_TYPE_GPS_DEVICE,
			G_SIGNAL_NO_RECURSE|G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET(LocationGPSDeviceClass, disconnected),
			0, NULL, (GSignalCMarshaller)&g_cclosure_marshal_VOID__VOID,
			G_TYPE_UCHAR, 0);
}

static void location_gps_device_init(LocationGPSDevice *device)
{
	LocationGPSDevicePrivate *priv;
	GConfClient *client;

	priv = G_TYPE_INSTANCE_GET_PRIVATE(device, G_TYPE_OBJECT,
			LocationGPSDevicePrivate);

	device->priv = priv;
	priv->bus = dbus_bus_get_private(DBUS_BUS_SYSTEM, NULL);
	dbus_connection_setup_with_g_main(priv->bus, NULL);

	if (priv->bus) {
		dbus_bus_add_match(priv->bus,
				"type='signal',interface='org.freedesktop.Gypsy.Device'",
				NULL);
		dbus_bus_add_match(priv->bus,
				"type='signal',interface='org.freedesktop.Gypsy.Position'",
				NULL);
		dbus_bus_add_match(priv->bus,
				"type='signal',interface='org.freedesktop.Gypsy.Course'",
				NULL);
		dbus_bus_add_match(priv->bus,
				"type='signal',interface='org.freedesktop.Gypsy.Satellite'",
				NULL);
		dbus_bus_add_match(priv->bus,
				"type='signal',interface='org.freedesktop.Gypsy.Accuracy'",
				NULL);
		dbus_bus_add_match(priv->bus,
				"type='signal',interface='com.nokia.Location.Cell'",
				NULL);
		dbus_bus_add_match(priv->bus,
				"type='signal',interface='com.nokia.Location.Uncertainty'",
				NULL);
		dbus_connection_add_filter(priv->bus,
				(DBusHandleMessageFunction)on_gypsy_signal,
				device,
				NULL);
	}

	device->online = FALSE;
	device->fix = priv->fix;
	priv->fields = LOCATION_GPS_DEVICE_NONE_SET;\
	priv->fix = LOCATION_GPS_DEVICE_STATUS_NO_FIX;
	priv->ept = LOCATION_GPS_DEVICE_NAN;
	priv->eph = LOCATION_GPS_DEVICE_NAN;
	priv->epv = LOCATION_GPS_DEVICE_NAN;
	priv->epd = LOCATION_GPS_DEVICE_NAN;
	priv->eps = LOCATION_GPS_DEVICE_NAN;
	priv->epc = LOCATION_GPS_DEVICE_NAN;
	priv->pitch = LOCATION_GPS_DEVICE_NAN;
	priv->roll = LOCATION_GPS_DEVICE_NAN;
	priv->dip = LOCATION_GPS_DEVICE_NAN;

	client = gconf_client_get_default();

	if (gconf_get_float(client, &priv->time, GCONF_LK_TIME))
		priv->fields |= LOCATION_GPS_DEVICE_TIME_SET;
	else
		priv->time = LOCATION_GPS_DEVICE_NAN;

	if (gconf_get_float(client, &priv->latitude, GCONF_LK_LAT)
			&& gconf_get_float(client, &priv->longitude, GCONF_LK_LON)) {
		priv->fields |= LOCATION_GPS_DEVICE_LATLONG_SET;
	} else {
		priv->latitude = LOCATION_GPS_DEVICE_NAN;
		priv->longitude = LOCATION_GPS_DEVICE_NAN;
	}

	if (gconf_get_float(client, &priv->altitude, GCONF_LK_ALT))
		priv->fields |= LOCATION_GPS_DEVICE_ALTITUDE_SET;
	else
		priv->altitude = LOCATION_GPS_DEVICE_NAN;

	if (gconf_get_float(client, &priv->track, GCONF_LK_TRK))
		priv->fields |= LOCATION_GPS_DEVICE_TRACK_SET;
	else
		priv->track = LOCATION_GPS_DEVICE_NAN;

	if (gconf_get_float(client, &priv->speed, GCONF_LK_SPD))
		priv->fields |= LOCATION_GPS_DEVICE_SPEED_SET;
	else
		priv->speed = LOCATION_GPS_DEVICE_NAN;

	if (gconf_get_float(client, &priv->climb, GCONF_LK_CLB))
		priv->fields |= LOCATION_GPS_DEVICE_CLIMB_SET;
	else
		priv->climb = LOCATION_GPS_DEVICE_NAN;

	g_object_unref(client);

	if (dbus_bus_name_has_owner(priv->bus, "com.nokia.Location", NULL)) {
		get_values_from_gypsy(device, "com.nokia.Location", "las");
	} else {
		gchar *car_retloc = NULL;
		gchar *cdr_retloc = NULL;
		client = gconf_client_get_default();
		if (gconf_client_get_pair(
					client,
					"/system/nokia/location/method",
					GCONF_VALUE_STRING,
					GCONF_VALUE_STRING,
					&car_retloc,
					&cdr_retloc,
					NULL)) {
			if (dbus_bus_name_has_owner(priv->bus, car_retloc, NULL))
				get_values_from_gypsy(device, car_retloc, cdr_retloc);
		}
		g_object_unref(client);
		g_free(car_retloc);
		g_free(cdr_retloc);
	}
}
