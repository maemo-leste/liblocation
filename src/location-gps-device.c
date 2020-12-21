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

#include <errno.h>
#include <math.h>

#include <gconf/gconf-client.h>
#include <glib.h>
#include <gps.h>

#include "location-gps-device.h"

#define GPSD_HOST  "localhost"
#define GPSD_PORT  "2947"

#define GC_LK       "/system/nokia/location/lastknown"
#define GC_LK_TIME  GC_LK"/time"
#define GC_LK_LAT   GC_LK"/latitude"
#define GC_LK_LON   GC_LK"/longitude"
#define GC_LK_ALT   GC_LK"/altitude"
#define GC_LK_TRK   GC_LK"/track"
#define GC_LK_SPD   GC_LK"/speed"
#define GC_LK_CLB   GC_LK"/climb"

#define TSTONS(ts) ((double)((ts)->tv_sec + ((ts)->tv_nsec / 1e9)))
#define nelem(x) (sizeof (x) / sizeof *(x))

enum {
	DEVICE_CHANGED,
	DEVICE_CONNECTED,
	DEVICE_DISCONNECTED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {};

struct _LocationGPSDevicePrivate
{
	struct gps_data_t gpsdata;
	gint interval;
	gboolean sig_pending;
};

G_DEFINE_TYPE_WITH_PRIVATE(LocationGPSDevice, location_gps_device, G_TYPE_OBJECT);

/* function declarations */
static int gconf_get_float(GConfClient *, double *, const gchar *);
static GPtrArray *free_satellites(LocationGPSDevice *);
static void set_satellites(LocationGPSDevice *);
static int signal_changed(LocationGPSDevice *);
static void add_g_timeout_interval(LocationGPSDevice *);
static void location_gps_device_finalize(GObject *);
static void location_gps_device_dispose(GObject *);
static void location_gps_device_class_init(LocationGPSDeviceClass *);
static void location_gps_device_init(LocationGPSDevice *);

int gconf_get_float(GConfClient *client, double *dest, const gchar *key)
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

GPtrArray *free_satellites(LocationGPSDevice *device)
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

void set_satellites(LocationGPSDevice *device)
{
	g_debug(G_STRFUNC);
	LocationGPSDevicePrivate *p = location_gps_device_get_instance_private(device);
	GPtrArray *satarray;
	LocationGPSDeviceSatellite *sat;
	int i;

	free_satellites(device);
	satarray = g_ptr_array_new();
	device->satellites = satarray;

	for (i = 0; i < nelem(p->gpsdata.skyview); i++) {
		sat = g_malloc(sizeof(LocationGPSDeviceSatellite));
		sat->prn = p->gpsdata.skyview[i].PRN;
		sat->in_use = p->gpsdata.skyview[i].used;
		sat->elevation = p->gpsdata.skyview[i].elevation;
		sat->azimuth = p->gpsdata.skyview[i].azimuth;
		sat->signal_strength = p->gpsdata.skyview[i].ss;
		g_ptr_array_add(device->satellites, sat);
		++device->satellites_in_view;
		if (sat->in_use)
			++device->satellites_in_use;
	}
}

int poll_gpsd_data(LocationGPSDevice *device)
{
	g_debug(G_STRFUNC);

	LocationGPSDevicePrivate *p = location_gps_device_get_instance_private(device);
	LocationGPSDeviceFix *fix = device->fix;

	if (gps_waiting(&p->gpsdata, 500)) {
		errno = 0;
		if (gps_read(&p->gpsdata, NULL, 0) == -1) {
			g_warning("error in gps_read");
		} else {
			g_debug("gps_read alright");
			device->online = TRUE;
			gboolean c = FALSE;

			fix->mode = p->gpsdata.fix.mode;

			if (p->gpsdata.satellites_visible > 0) {
				set_satellites(device);
				c = TRUE;
			}

			if (p->gpsdata.set & TIME_SET) {
				fix->time = TSTONS(&p->gpsdata.fix.time);
				fix->fields |= LOCATION_GPS_DEVICE_TIME_SET;
				c = TRUE;
			}

			if (isfinite(p->gpsdata.fix.latitude) && isfinite(p->gpsdata.fix.longitude)) {
				fix->latitude = p->gpsdata.fix.latitude;
				fix->longitude = p->gpsdata.fix.longitude;
				fix->fields |= LOCATION_GPS_DEVICE_LATLONG_SET;
				c = TRUE;
			}

			if (isfinite(p->gpsdata.fix.eph)) {
				fix->eph = p->gpsdata.fix.eph;
				c = TRUE;
			}

			if (isfinite(p->gpsdata.fix.altHAE)) {
				fix->altitude = p->gpsdata.fix.altHAE;
				fix->fields |= LOCATION_GPS_DEVICE_ALTITUDE_SET;
				c = TRUE;
			}

			if (isfinite(p->gpsdata.fix.epv)) {
				fix->epv = p->gpsdata.fix.epv;
				c = TRUE;
			}

			if (isfinite(p->gpsdata.fix.speed)) {
				fix->speed = p->gpsdata.fix.speed;
				fix->fields |= LOCATION_GPS_DEVICE_SPEED_SET;
				c = TRUE;
			}

			if (isfinite(p->gpsdata.fix.eps)) {
				fix->eps = p->gpsdata.fix.eps;
				c = TRUE;
			}

			if (isfinite(p->gpsdata.fix.track)) {
				fix->track = p->gpsdata.fix.track;
				fix->fields |= LOCATION_GPS_DEVICE_TRACK_SET;
				c = TRUE;
			}

			if (isfinite(p->gpsdata.fix.eps)) {
				fix->eps = p->gpsdata.fix.eps;
				c = TRUE;
			}

			if (isfinite(p->gpsdata.fix.climb)) {
				fix->climb = p->gpsdata.fix.climb;
				fix->fields |= LOCATION_GPS_DEVICE_CLIMB_SET;
				c = TRUE;
			}

			if (isfinite(p->gpsdata.fix.epc)) {
				fix->epc = p->gpsdata.fix.epc;
				c = TRUE;
			}

			if (c)
				add_g_timeout_interval(device);
		}
	}

	return 1;
}

void store_lastknown_in_gconf(LocationGPSDevice *device)
{
	LocationGPSDeviceFix *fix = device->fix;
	GConfClient *client = gconf_client_get_default();

	if (fix->fields & LOCATION_GPS_DEVICE_TIME_SET)
		gconf_client_set_float(client, GC_LK_TIME, fix->time, NULL);
	else
		gconf_client_unset(client, GC_LK_TIME, NULL);

	if (fix->fields & LOCATION_GPS_DEVICE_LATLONG_SET) {
		gconf_client_set_float(client, GC_LK_LAT, fix->latitude, NULL);
		gconf_client_set_float(client, GC_LK_LON, fix->longitude, NULL);
	} else {
		gconf_client_unset(client, GC_LK_LAT, NULL);
		gconf_client_unset(client, GC_LK_LON, NULL);
	}

	if (fix->fields & LOCATION_GPS_DEVICE_ALTITUDE_SET)
		gconf_client_set_float(client, GC_LK_ALT, fix->altitude, NULL);
	else
		gconf_client_unset(client, GC_LK_ALT, NULL);

	if (fix->fields & LOCATION_GPS_DEVICE_TRACK_SET)
		gconf_client_set_float(client, GC_LK_TRK, fix->track, NULL);
	else
		gconf_client_unset(client, GC_LK_TRK, NULL);

	if (fix->fields & LOCATION_GPS_DEVICE_SPEED_SET)
		gconf_client_set_float(client, GC_LK_SPD, fix->speed, NULL);
	else
		gconf_client_unset(client, GC_LK_SPD, NULL);

	if (fix->fields & LOCATION_GPS_DEVICE_CLIMB_SET)
		gconf_client_set_float(client, GC_LK_CLB, fix->climb, NULL);
	else
		gconf_client_unset(client, GC_LK_CLB, NULL);

	g_object_unref(client);
}

int signal_changed(LocationGPSDevice *device)
{
	LocationGPSDevicePrivate *p;
	p = location_gps_device_get_instance_private(device);

	p->sig_pending = FALSE;
	g_signal_emit(device, signals[DEVICE_CHANGED], 0);
	g_object_unref(device);
	return 0;
}

void add_g_timeout_interval(LocationGPSDevice *device)
{
	LocationGPSDevicePrivate *p;
	p = location_gps_device_get_instance_private(device);

	if (!p->sig_pending) {
		g_object_ref(device);
		g_timeout_add(300, (GSourceFunc)signal_changed, device);
		p->sig_pending = TRUE;
	}
}

void location_gps_device_reset_last_known(LocationGPSDevice *device)
{
	LocationGPSDeviceFix *fix = device->fix;
	GConfClient *client;

	if (!LOCATION_IS_GPS_DEVICE(device))
		g_assert("LOCATION_IS_GPS_DEVICE(device)");

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
	gconf_client_recursive_unset(client, GC_LK, 0, NULL);
	g_signal_emit(device, signals[DEVICE_CHANGED], 0);
	g_object_unref(client);
}

void location_gps_device_start(LocationGPSDevice *device)
{
	g_warning("You don't need to call %s, it does nothing anymore!",
			G_STRFUNC);
}

void location_gps_device_stop(LocationGPSDevice *device)
{
	g_warning("You don't need to call %s, it does nothing anymore!",
			G_STRFUNC);
}

void location_gps_device_finalize(GObject *object)
{
	free_satellites(LOCATION_GPS_DEVICE(object));
	store_lastknown_in_gconf(LOCATION_GPS_DEVICE(object));
}

void location_gps_device_dispose(GObject *object)
{
	LocationGPSDevicePrivate *p;
	p = location_gps_device_get_instance_private(LOCATION_GPS_DEVICE(object));

	(void) gps_stream(&p->gpsdata, WATCH_DISABLE, NULL);
	(void) gps_close(&p->gpsdata);

	g_signal_emit(LOCATION_GPS_DEVICE(object), signals[DEVICE_DISCONNECTED], 0);
}

void location_gps_device_class_init(LocationGPSDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = location_gps_device_finalize;
	object_class->dispose = location_gps_device_dispose;

	signals[DEVICE_CHANGED] = g_signal_new("changed",
			G_TYPE_FROM_CLASS(klass),
			G_SIGNAL_NO_RECURSE|G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET(LocationGPSDeviceClass, changed),
			0, NULL, g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);

	signals[DEVICE_CONNECTED] = g_signal_new("connected",
			G_TYPE_FROM_CLASS(klass),
			G_SIGNAL_NO_RECURSE|G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET(LocationGPSDeviceClass, connected),
			0, NULL, g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);

	signals[DEVICE_DISCONNECTED] = g_signal_new("disconnected",
			G_TYPE_FROM_CLASS(klass),
			G_SIGNAL_NO_RECURSE|G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET(LocationGPSDeviceClass, disconnected),
			0, NULL, g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);
}

void location_gps_device_init(LocationGPSDevice *device)
{
	LocationGPSDevicePrivate *p;
	LocationGPSDeviceFix *fix;
	GConfClient *client;

	p = location_gps_device_get_instance_private(device);

	if (gps_open(GPSD_HOST, GPSD_PORT, &p->gpsdata)) {
		g_critical("%s: Could not open gpsd socket: %d, %s",
				G_STRLOC, errno, gps_errstr(errno));
	}

	g_signal_emit(device, signals[DEVICE_CONNECTED], 0);

	(void) gps_stream(&p->gpsdata, WATCH_ENABLE, NULL);

	device->online = FALSE;
	device->status = LOCATION_GPS_DEVICE_STATUS_NO_FIX;
	device->fix = g_try_new0(LocationGPSDeviceFix, 1);
	fix = device->fix;

	fix->mode = LOCATION_GPS_DEVICE_MODE_NOT_SEEN;
	fix->fields = LOCATION_GPS_DEVICE_NONE_SET;
	fix->ept = LOCATION_GPS_DEVICE_NAN;
	fix->eph = LOCATION_GPS_DEVICE_NAN;
	fix->epv = LOCATION_GPS_DEVICE_NAN;
	fix->epd = LOCATION_GPS_DEVICE_NAN;
	fix->eps = LOCATION_GPS_DEVICE_NAN;
	fix->epc = LOCATION_GPS_DEVICE_NAN;
	fix->pitch = LOCATION_GPS_DEVICE_NAN;
	fix->roll = LOCATION_GPS_DEVICE_NAN;
	fix->dip = LOCATION_GPS_DEVICE_NAN;

	client = gconf_client_get_default();

	if (gconf_get_float(client, &fix->time, GC_LK_TIME))
		fix->fields |= LOCATION_GPS_DEVICE_TIME_SET;
	else
		fix->time = LOCATION_GPS_DEVICE_NAN;

	if (gconf_get_float(client, &fix->latitude, GC_LK_LAT)
			&& gconf_get_float(client, &fix->longitude, GC_LK_LON)) {
		fix->fields |= LOCATION_GPS_DEVICE_LATLONG_SET;
	} else {
		fix->latitude = LOCATION_GPS_DEVICE_NAN;
		fix->longitude = LOCATION_GPS_DEVICE_NAN;
	}

	if (gconf_get_float(client, &fix->altitude, GC_LK_ALT))
		fix->fields |= LOCATION_GPS_DEVICE_ALTITUDE_SET;
	else
		fix->altitude = LOCATION_GPS_DEVICE_NAN;

	if (gconf_get_float(client, &fix->track, GC_LK_TRK))
		fix->fields |= LOCATION_GPS_DEVICE_TRACK_SET;
	else
		fix->track = LOCATION_GPS_DEVICE_NAN;

	if (gconf_get_float(client, &fix->speed, GC_LK_SPD))
		fix->fields |= LOCATION_GPS_DEVICE_SPEED_SET;
	else
		fix->speed = LOCATION_GPS_DEVICE_NAN;

	if (gconf_get_float(client, &fix->climb, GC_LK_CLB))
		fix->fields |= LOCATION_GPS_DEVICE_CLIMB_SET;
	else
		fix->climb = LOCATION_GPS_DEVICE_NAN;

	/* TODO: Maybe don't hardcode the default */
	if (device->interval < 1000)
		device->interval = 1000;

	g_object_unref(client);

	g_timeout_add(device->interval, (GSourceFunc)poll_gpsd_data, device);

	/*
	if (dbus_bus_name_has_owner(p->bus, "com.nokia.Location", NULL)) {
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
			if (dbus_bus_name_has_owner(p->bus, car_retloc, NULL))
				get_values_from_gypsy(device, car_retloc, cdr_retloc);
		}

		g_object_unref(client);
		g_free(car_retloc);
		g_free(cdr_retloc);
	}
	*/
}
