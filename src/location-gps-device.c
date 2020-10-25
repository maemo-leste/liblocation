#include <math.h>
#include <stdio.h>

#include <dbus/dbus.h>
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

struct _LocationGPSDevicePrivate {
	DBusConnection *bus;
	LocationGPSDeviceFix *fix;

};

G_DEFINE_TYPE_WITH_CODE (LocationGPSDevice,
		location_gps_device, G_TYPE_OBJECT,
		G_ADD_PRIVATE(LocationGPSDevice))

#define LOCATION_GPS_DEVICE_PRIVATE(device) \
	((LocationGPSDevicePrivate *)location_gps_device_get_instance_private(device))

GPtrArray *free_satellites(LocationGPSDevice *device)
{
	GPtrArray *result;
	result = device->satellites;
	if (result) {
		g_ptr_array_foreach(device->satellites, (GFunc)g_free, NULL);
		result = (GPtrArray *)g_ptr_array_free(device->satellites, TRUE);
		device->satellites = NULL;
	}

	device->satellites_in_view = 0;
	device->satellites_in_use = 0;

	return result;
}

static void store_lastknown_in_gconf(LocationGPSDevice *device)
{
	/* TODO: Maybe error handling? */
	LocationGPSDeviceFix *fix = device->fix;
	GConfClient *client = gconf_client_get_default();
	/* GError *err; */

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

static void free_satellites_and_save_gconf(LocationGPSDevice *device)
{
	free_satellites(device);
	store_lastknown_in_gconf(device);

	// TODO: Return something? maybe the device pointer?
}

/* TODO: Review if this is correct */
static signed int iterate_dbus_struct_by_type(DBusMessageIter *iter, int type, ...)
{
	void **value;
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

static int emit_some_signal(LocationGPSDevice *device)
{
#if 0
	(device->priv + 41) = 0;
	g_signal_emit(device, 666, 0); // 666 is dword_FD1C
	g_object_unref(device);
#endif
	return 0;
}

/* LocationGPSDevice *add_g_timeout_interval(LocationGPSDevice *device) */
static dbus_bool_t add_g_timeout_interval(LocationGPSDevice *device)
{
#if 0
	if (!device->priv + 41) {
		g_object_ref(device);
		device = (LocationGPSDevice *)g_timeout_add(300,
				(GSourceFunc)emit_some_signal, device);
		(device->priv + 41) = 1;
	}
	return device;
#endif
	return TRUE;
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
		result = add_g_timeout_interval(device);
	}

	return result;
}

static dbus_bool_t set_position(LocationGPSDevice *device, DBusMessage *msg)
{
	dbus_bool_t result;
	LocationGPSDeviceFix *fix;
	double latitude, longitude, altitude;
	int mode, time, v8;

	result = dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &mode,
			DBUS_TYPE_INT32, &time, /* TODO: Maybe this should also be a double? */
			DBUS_TYPE_DOUBLE, &latitude,
			DBUS_TYPE_DOUBLE, &longitude,
			DBUS_TYPE_DOUBLE, &altitude,
			DBUS_TYPE_INVALID);

	if (result) {
		/* TODO: Review and use the enums */
		if ((mode & 6) == 6) {
			fix = device->fix;
			fix->latitude = latitude;
			v8 = fix->fields | 0x10;
			fix->longitude = longitude;
			fix->fields = v8;
		} else {
			fix = device->fix;
			fix->fields &= 0xFFFFFFEF;
		}

		if (mode & 8) {
			v8 = fix->fields | 1;
			fix->altitude = altitude;
			fix->fields = v8;
		} else {
			fix->fields &= 0xFFFFFFFE;
		}

		if (time) {
			fix->fields |= 0x20u;
			fix->time = (double)time;
		} else {
			fix->fields &= 0xFFFFFFDF;
		}

		result = add_g_timeout_interval(device);
	}

	return result;
}

static dbus_bool_t set_accuracy(LocationGPSDevice *device, DBusMessage *msg)
{
	dbus_bool_t result;
	LocationGPSDeviceFix *fix;
	double epv, eph, epd, epc, eps, v11;
	int mode;

	result = dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &mode,
			DBUS_TYPE_DOUBLE, &v11, /* TODO: figure out var name */
			DBUS_TYPE_DOUBLE, &eps,
			DBUS_TYPE_DOUBLE, &epc,
			DBUS_TYPE_INVALID);

	if (result) {
		/* TODO: use enums */
		if (mode & 2) {
			eph = eps;
			fix = device->fix;
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
		result = add_g_timeout_interval(device);
	}

	return result;
}

static dbus_bool_t set_course(LocationGPSDevice *device, DBusMessage *msg)
{
	dbus_bool_t result;
	LocationGPSDeviceFix *fix;
	double climb, knots, track;
	int mode, v14;
	unsigned int fields;

	result = dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &mode,
			DBUS_TYPE_INT32, v14, /* TODO: figure out var name */
			DBUS_TYPE_DOUBLE, knots,
			DBUS_TYPE_DOUBLE, track,
			DBUS_TYPE_DOUBLE, climb,
			DBUS_TYPE_INVALID);

	if (result) {
		/* TODO: Use enums */
		fix = device->fix;
		fields = fix->fields & 0xFFFFFFF1;
		fix->fields = fields;

		if (!((mode & 2) == 0)) {
			fix->fields = fields | 2;
			fix->speed = knots * 1.852;
		}

		if (mode & 4) {
			fields = fix->fields | 4;
			fix->track = track;
			fix->fields = fields;
		}

		if (mode & 8) {
			fields = fix->fields | 8;
			fix->climb = climb;
			fix->fields = fields;
		}

		result = add_g_timeout_interval(device);
	}

	return result;
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
							DBUS_TYPE_UINT32,  &prn,
							DBUS_TYPE_BOOLEAN, &in_use,
							DBUS_TYPE_UINT32,  &elevation,
							DBUS_TYPE_UINT32,  &azimuth,
							DBUS_TYPE_UINT32,  &signal_strength,
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
							"Failed to parse satellite info from DBus-message!");
				}
				dbus_message_iter_next(&sub);
			}
			result = add_g_timeout_interval(device);
		}
	}

	return result;
}

static void get_values_from_gypsy(LocationGPSDevice *device, const char *bus_name, char *type)
{
	DBusMessage *cmd, *msg;
	gchar *str;

	g_log("liblocation", G_LOG_LEVEL_DEBUG, "Loading initial values from %s::%s",
			bus_name, type);

	cmd = dbus_message_new_method_call(bus_name, "org/freedesktop/Gypsy",
			"org.freedesktop.Gypsy.Server", "Create");
	dbus_message_append_args(cmd, DBUS_TYPE_STRING, &type, DBUS_TYPE_INVALID);
	msg = dbus_connection_send_with_reply_and_block(device->priv->bus, cmd,
			DBUS_TIMEOUT_USE_DEFAULT, NULL);
	dbus_message_unref(cmd);

	if (msg) {
		if (dbus_message_get_args(msg, NULL, DBUS_TYPE_OBJECT_PATH)) {
			str = g_strdup(str);
			dbus_message_unref(msg);
			g_log("liblocation", G_LOG_LEVEL_DEBUG, "Object path: %s", str);

			cmd = dbus_message_new_method_call(bus_name, str,
					"org.freedesktop.Gypsy.Device", "GetFixStatus");
			msg = dbus_connection_send_with_reply_and_block(device->priv->bus, cmd,
					DBUS_TIMEOUT_USE_DEFAULT, NULL);
			if (msg) {
				set_fix_status(device, msg);
				dbus_message_unref(msg);
			}
			dbus_message_unref(cmd);

			cmd = dbus_message_new_method_call(bus_name, str,
					"org.freedesktop.Gypsy.Position", "GetPosition");
			msg = dbus_connection_send_with_reply_and_block(device->priv->bus, cmd,
					DBUS_TIMEOUT_USE_DEFAULT, NULL);
			if (msg) {
				set_position(device, msg);
				dbus_message_unref(msg);
			}
			dbus_message_unref(cmd);

			cmd = dbus_message_new_method_call(bus_name, str,
					"org.freedesktop.Gypsy.Accuracy", "GetAccuracy");
			msg = dbus_connection_send_with_reply_and_block(device->priv->bus, cmd,
					DBUS_TIMEOUT_USE_DEFAULT, NULL);
			if (msg) {
				set_accuracy(device, msg);
				dbus_message_unref(msg);
			}
			dbus_message_unref(cmd);

			cmd = dbus_message_new_method_call(bus_name, str,
					"org.freedesktop.Gypsy.Course", "GetCourse");
			msg = dbus_connection_send_with_reply_and_block(device->priv->bus, cmd,
					DBUS_TIMEOUT_USE_DEFAULT, NULL);
			if (msg) {
				set_course(device, msg);
				dbus_message_unref(msg);
			}
			dbus_message_unref(cmd);

			cmd = dbus_message_new_method_call(bus_name, str,
					"org.freedesktop.Gypsy.Satellite", "GetSatellites");
			msg = dbus_connection_send_with_reply_and_block(device->priv->bus, cmd,
					DBUS_TIMEOUT_USE_DEFAULT, NULL);
			if (msg) {
				set_satellites(device, msg);
				dbus_message_unref(msg);
			}
			dbus_message_unref(cmd);

			cmd = dbus_message_new_method_call(bus_name, "/org/freedesktop/Gypsy",
					"org.freedesktop.Gypsy.Server", "Shutdown");
			dbus_message_append_args(cmd, DBUS_TYPE_OBJECT_PATH, &str,
					DBUS_TYPE_INVALID);
			msg = dbus_connection_send_with_reply_and_block(device->priv->bus, cmd,
					DBUS_TIMEOUT_USE_DEFAULT, NULL);
			dbus_message_unref(cmd);
			if (msg)
				dbus_message_unref(msg);
			g_free(str);
		} else {
			dbus_message_unref(msg);
		}
	}
}

static void location_gps_device_class_init(LocationGPSDeviceClass *klass)
{
	/* GObjectClass *object_class = G_OBJECT_CLASS(klass); */

	/* Supposed to call free_satellites_and_save_gconf() here? */

	signals[DEVICE_CHANGED] = g_signal_new(
			"changed",
			LOCATION_TYPE_GPS_DEVICE,
			G_SIGNAL_NO_RECURSE|G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET(LocationGPSDeviceClass, changed),
			0, NULL, g_cclosure_marshal_VOID__VOID,
			G_TYPE_UCHAR, 0);

	signals[DEVICE_CONNECTED] = g_signal_new(
			"connected",
			LOCATION_TYPE_GPS_DEVICE,
			G_SIGNAL_NO_RECURSE|G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET(LocationGPSDeviceClass, connected),
			0, NULL, g_cclosure_marshal_VOID__VOID,
			G_TYPE_UCHAR, 0);

	signals[DEVICE_DISCONNECTED] = g_signal_new(
			"disconnected",
			LOCATION_TYPE_GPS_DEVICE,
			G_SIGNAL_NO_RECURSE|G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET(LocationGPSDeviceClass, disconnected),
			0, NULL, g_cclosure_marshal_VOID__VOID,
			G_TYPE_UCHAR, 0);
}

static void location_gps_device_init(LocationGPSDevice *device)
{

}


/*
GType location_gps_device_get_type(void)
{
	static volatile gsize g_define_type_id__volatile = 0;

	if (g_once_init_enter(&g_define_type_id__volatile)) {
		GType g_define_type_id = g_type_register_static_simple(
				0x50u, // TODO
				g_intern_static_string("LocationGPSDevice"),
				0x50u, // TODO
				(GClassInitFunc)location_gps_device_class_intern_init,
				0x2Cu, // TODO
				(GInstanceInitFunc)location_gps_device_init,
				(GTypeFlags) 0);
		g_once_init_leave(&g_define_type_id__volatile, g_define_type_id);
	}

	return g_define_type_id__volatile;
}
*/


void location_gps_device_reset_last_known(LocationGPSDevice *device)
{
	GConfClient *client;
	LocationGPSDeviceFix *fix;

	if (LOCATION_IS_GPS_DEVICE(device)) {
		client = gconf_client_get_default();
		device->status = 0;
		fix = device->fix;
		fix->mode = 0;
		fix->fields = 0;
		fix->dip = 0.0 / 0.0;
		fix->time = 0.0 / 0.0;
		fix->ept = 0.0 / 0.0;
		fix->latitude = 0.0 / 0.0;
		fix->longitude = 0.0 / 0.0;
		fix->eph = 0.0 / 0.0;
		fix->altitude = 0.0 / 0.0;
		fix->epv = 0.0 / 0.0;
		fix->track = 0.0 / 0.0;
		fix->epd = 0.0 / 0.0;
		fix->speed = 0.0 / 0.0;
		fix->eps = 0.0 / 0.0;
		fix->climb = 0.0 / 0.0;
		fix->epc = 0.0 / 0.0;
		fix->pitch = 0.0 / 0.0;
		fix->roll = 0.0 / 0.0;

		free_satellites(device);

		gconf_client_recursive_unset(client, GCONF_LK, 0, NULL);
		/* TODO: g_signal_emit(device, SOME_SIGNAL_ID, 0); */
		g_object_unref(client);
	} else {
		g_return_if_fail_warning("liblocation", G_STRFUNC,
				"LOCATION_IS_GPS_DEVICE(device)");
	}
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
