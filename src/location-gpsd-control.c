#include "location-gpsd-control.h"

GType location_gpsd_control_get_type(void)
{

}

LocationGPSDControl *location_gpsd_control_get_default(void)
{

}

void location_gpsd_control_start(LocationGPSDControl *control)
{

}

void location_gpsd_control_stop(LocationGPSDControl *control)
{
	return;
}

void location_gpsd_control_request_status(LocationGPSDControl *control)
{
	g_log("liblocation", G_LOG_LEVEL_WARNING,
			"You don't need to call %s, it does nothing anymore!",
			"location_gpsd_control_request_status");
}

gint location_gpsd_control_get_allowed_methods(LocationGPSDControl *control)
{
	return 0;
}
