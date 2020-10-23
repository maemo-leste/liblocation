#include "location-gpsd-control.h"

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
