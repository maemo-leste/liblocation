#include <gconf/gconf-client.h>
#include <glib.h>

#include "location-gps-device.h"

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

		if (device->satellites) {
			g_ptr_array_foreach(device->satellites, (GFunc)g_free, NULL);
			g_ptr_array_free(device->satellites, TRUE);
			device->satellites = NULL;
		}
		device->satellites_in_view = 0;
		device->satellites_in_use = 0;

		gconf_client_recursive_unset(client,
				"/system/nokia/location/lastknown", 0, NULL);
		/* TODO: g_signal_emit(device, SOME_SIGNAL_ID, 0); */
		g_object_unref(client);
	} else {
		g_return_if_fail_warning("liblocation",
				"location_gps_device_reset_last_known",
				"LOCATION_IS_GPS_DEVICE(device)");
	}
}

void location_gps_device_start(LocationGPSDevice *device)
{
	g_log("liblocation", G_LOG_LEVEL_WARNING,
			"You don't need to call %s, it does nothing anymore!",
			"location_gps_device_start");
}

void location_gps_device_stop(LocationGPSDevice *device)
{
	g_log("liblocation", G_LOG_LEVEL_WARNING,
			"You don't need to call %s, it does nothing anymore!",
			"location_gps_device_stop");
}
