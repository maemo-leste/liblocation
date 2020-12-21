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

#ifndef __LOCATION_GPSD_CONTROL_H__
#define __LOCATION_GPSD_CONTROL_H__

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * LOCATION_TYPE_GPSD_CONTROL:
 *
 * This macro is a part of the GObject system used by this library.
 */
#define LOCATION_TYPE_GPSD_CONTROL (location_gpsd_control_get_type ())

/**
 * LOCATION_GPSD_CONTROL:
 *
 * This macro is a part of the GObject system used by this library.
 */
#define LOCATION_GPSD_CONTROL(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LOCATION_TYPE_GPSD_CONTROL, LocationGPSDControl))

/**
 * LOCATION_IS_GPSD_CONTROL:
 *
 * This macro is a part of the GObject system used by this library.
 */
#define LOCATION_IS_GPSD_CONTROL(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LOCATION_TYPE_GPSD_CONTROL))

/**
 * LocationGPSDControlMethod:
 *
 * One of #LOCATION_METHOD_AGNSS, #LOCATION_METHOD_GNSS, #LOCATION_METHOD_ACWP,
 * #LOCATION_METHOD_CWP, and #LOCATION_METHOD_USER_SELECTED.
 *
 * Please note that these values are in sync with the ones
 * understood by the lower layers.
 */
typedef gint LocationGPSDControlMethod;

/**
 * LOCATION_METHOD_USER_SELECTED:
 *
 * The method is based on the current device settings. This is the default.
 */
#define LOCATION_METHOD_USER_SELECTED 0x0

/**
 * LOCATION_METHOD_CWP:
 *
 * Use the Complimentary Wireless Position method.
 */
#define LOCATION_METHOD_CWP 0x1

/**
 * LOCATION_METHOD_ACWP:
 *
 * Use the Assisted Complimentary Wireless Position method.
 */
#define LOCATION_METHOD_ACWP 0x2

/**
 * LOCATION_METHOD_GNSS:
 *
 * Use the Global Navigation Satellite System method.
 */
#define LOCATION_METHOD_GNSS 0x4

/**
 * LOCATION_METHOD_AGNSS:
 *
 * Use the Assisted Global Navigation Satellite System method.
 */
#define LOCATION_METHOD_AGNSS 0x8

/**
 * LocationGPSDControlInterval:
 * @LOCATION_INTERVAL_DEFAULT: Default value for the system.
 * @LOCATION_INTERVAL_1S: 1 second between subsequent fixes.
 * @LOCATION_INTERVAL_2S: 2 seconds between subsequent fixes.
 * @LOCATION_INTERVAL_5S: 5 seconds between subsequent fixes.
 * @LOCATION_INTERVAL_10S: 10 seconds between subsequent fixes.
 * @LOCATION_INTERVAL_20S: 20 seconds between subsequent fixes.
 * @LOCATION_INTERVAL_30S: 30 seconds between subsequent fixes.
 * @LOCATION_INTERVAL_60S: 60 seconds between subsequent fixes.
 * @LOCATION_INTERVAL_120S: 120 seconds between subsequent fixes.
 *
 * Enum representing valid values for the intervals between fixes.
 * Note that only these constants can be specified for the preferred
 * interval value.
 */
typedef enum {
	LOCATION_INTERVAL_DEFAULT = 1000,
	LOCATION_INTERVAL_1S = 1000,
	LOCATION_INTERVAL_2S = 2000,
	LOCATION_INTERVAL_5S = 5000,
	LOCATION_INTERVAL_10S = 10000,
	LOCATION_INTERVAL_20S = 20000,
	LOCATION_INTERVAL_30S = 30000,
	LOCATION_INTERVAL_60S = 60000,
	LOCATION_INTERVAL_120S = 120000,
} LocationGPSDControlInterval;

/**
 * LocationGPSDControlError:
 * @LOCATION_ERROR_USER_REJECTED_DIALOG: User rejected location enabling dialog.
 * @LOCATION_ERROR_USER_REJECTED_SETTINGS: User changed settings which disabled locationing.
 * @LOCATION_ERROR_BT_GPS_NOT_AVAILABLE: Problems using BT GPS.
 * @LOCATION_ERROR_METHOD_NOT_ALLOWED_IN_OFFLINE_MODE: Method unavailable in offline mode.
 * @LOCATION_ERROR_SYSTEM: System error, dbus, hildon, etc. Also when trying to use nw based methods in simless state.
 *
 * Enum representing reason for error why locationing
 * is not available.
 */
typedef enum {
	LOCATION_ERROR_USER_REJECTED_DIALOG,
	LOCATION_ERROR_USER_REJECTED_SETTINGS,
	LOCATION_ERROR_BT_GPS_NOT_AVAILABLE,
	LOCATION_ERROR_METHOD_NOT_ALLOWED_IN_OFFLINE_MODE,
	LOCATION_ERROR_SYSTEM,
} LocationGPSDControlError;

typedef struct _LocationGPSDControlPrivate LocationGPSDControlPrivate;

/**
 * LocationGPSDControl:
 *
 * The object that controls the daemon processes. An application
 * obtains an instance of this object by calling the
 * location_gpsd_control_get_default() function.
 */
typedef struct _LocationGPSDControl {
	GObject parent;

	/*< private >*/
	LocationGPSDControlPrivate *priv;
} LocationGPSDControl;

/*
 * This typedef is a part of the library implementation. Not for the
 * application developers to use.
 */
typedef struct _LocationGPSDControlClass {
	GObjectClass parent_class;

	void (*error) (LocationGPSDControl *control);
	void (*error_verbose) (LocationGPSDControl *control, LocationGPSDControlError *error);
	void (*gpsd_running) (LocationGPSDControl *control);
	void (*gpsd_stopped) (LocationGPSDControl *control);
} LocationGPSDControlClass;

/**
 * location_gpsd_control_get_type:
 *
 * This function is a part of library implementation. Application
 * developers should prefer using the macro #LOCATION_TYPE_GPSD_CONTROL
 * over calling this function directly.
 */
GType location_gpsd_control_get_type (void);

/**
 * location_gpsd_control_get_default:
 *
 * This function is an alias for creating
 * a new #LocationGPSDControl object. This used to return
 * a single instance of object, but not anymore as object
 * properties cannot be shared between applications.
 *
 * Returns: A new #LocationGPSDControl object.
 */
LocationGPSDControl *location_gpsd_control_get_default (void);

/**
 * location_gpsd_control_start:
 * @control: The control context.
 *
 * Starts an active connection to Location server. In other words, by calling
 * this function the application informs that it wants to use the
 * location service.
 */
void location_gpsd_control_start (LocationGPSDControl *control);

/**
 * location_gpsd_control_stop:
 * @control: The control context.
 *
 * Informs the location framework that the application is no longer
 * interested about the current location. Location service is kept running
 * as long as there is at least one application using it.
 * Please note that LocationGPSDevice still sends data as long as the
 * location framework is running.
 */
void location_gpsd_control_stop (LocationGPSDControl *control);

/**
 * location_gpsd_control_request_status:
 * @control: The control context.
 *
 * Deprecated:
 */
void location_gpsd_control_request_status (LocationGPSDControl *control);

/**
 * location_gpsd_control_get_allowed_methods:
 * @control: The control context.
 *
 * This function reads current location settings and returns
 * a bitmask of allowed location methods of type #LocationGPSDControlMethod.
 * Please note that you can also use methods, that are not currently
 * allowed. Location Framework will in this case prompt user a dialog to enable
 * necessary settings when location is started.
 *
 * Returns: A bitmask of allowed location methods of type #LocationGPSDControlMethod.
 *
 * Since: 0.100
 **/
gint location_gpsd_control_get_allowed_methods (LocationGPSDControl *control);

G_END_DECLS

#endif
