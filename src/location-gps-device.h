/*
 * This file is part of liblocation
 *
 * Copyright (C) 2007 Nokia Corporation. All rights reserved.
 *
 * Contact: Quanyi Sun <Quanyi.Sun@nokia.com>
 *
 * This software, including documentation, is protected by copyright
 * controlled by Nokia Corporation. All rights are reserved.  Copying,
 * including reproducing, storing, adapting or translating, any or all
 * of this material requires the prior written consent of Nokia
 * Corporation. This material also contains confidential information
 * which may not be disclosed to others without the prior written
 * consent of Nokia.
 */

#ifndef __GPS_DEVICE_H__
#define __GPS_DEVICE_H__

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * LOCATION_TYPE_GPS_DEVICE:
 *
 * This macro is a part of the GObject system used by this library.
 */
#define LOCATION_TYPE_GPS_DEVICE (location_gps_device_get_type ())

/**
 * LOCATION_GPS_DEVICE:
 *
 * This macro is a part of the GObject system used by this library.
 */
#define LOCATION_GPS_DEVICE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LOCATION_TYPE_GPS_DEVICE, LocationGPSDevice))

/**
 * LOCATION_GPS_DEVICE_CLASS:
 *
 * This macro is a part of the GObject system used by this library.
 */
#define LOCATION_GPS_DEVICE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), LOCATION_TYPE_GPS_DEVICE, LocationGPSDeviceClass))

/**
 * LOCATION_GPS_DEVICE_CLASS:
 *
 * This macro is a part of the GObject system used by this library.
 */
#define LOCATION_IS_GPS_DEVICE(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LOCATION_TYPE_GPS_DEVICE))

/**
 * LOCATION_GPS_DEVICE_NAN:
 *
 * A define representing NAN.
 */
#define LOCATION_GPS_DEVICE_NAN (double) (0.0 / 0.0)

/**
 * LocationGPSDeviceStatus:
 * @LOCATION_GPS_DEVICE_STATUS_NO_FIX: The device does not have a fix.
 * @LOCATION_GPS_DEVICE_STATUS_FIX: The device has a fix.
 * @LOCATION_GPS_DEVICE_STATUS_DGPS_FIX: The device has a DGPS fix. Deprecated: this constant is not used anymore.
 *
 * Enum representing the various states that a device can be in.
 */
typedef enum {
	LOCATION_GPS_DEVICE_STATUS_NO_FIX,
	LOCATION_GPS_DEVICE_STATUS_FIX,
	LOCATION_GPS_DEVICE_STATUS_DGPS_FIX,
} LocationGPSDeviceStatus;

/**
 * LocationGPSDeviceMode:
 * @LOCATION_GPS_DEVICE_MODE_NOT_SEEN: The device has not seen a satellite yet.
 * @LOCATION_GPS_DEVICE_MODE_NO_FIX: The device has no fix.
 * @LOCATION_GPS_DEVICE_MODE_2D: The device has latitude and longitude fix.
 * @LOCATION_GPS_DEVICE_MODE_3D: The device has latitude, longitude, and altitude.
 *
 * Enum representing the modes that a device can operate in.
 *
 */
typedef enum {
	LOCATION_GPS_DEVICE_MODE_NOT_SEEN,
	LOCATION_GPS_DEVICE_MODE_NO_FIX,
	LOCATION_GPS_DEVICE_MODE_2D,
	LOCATION_GPS_DEVICE_MODE_3D
} LocationGPSDeviceMode;

/**
 * LocationGPSDeviceSatellite:
 * @prn: Satellite ID number.
 * @elevation: Elevation of the satellite.
 * @azimuth: Satellite azimuth.
 * @signal_strength: Signal/Noise ratio.
 * @in_use: Whether the satellite is being used to calculate the fix.
 *
 * Details about satellites.
 */
typedef struct {
	int prn;
	int elevation;
	int azimuth;
	int signal_strength;
	gboolean in_use;
} LocationGPSDeviceSatellite;


/**
 * _gsm_cell_info:
 * @mcc: Current Mobile Country Code.
 * @mnc: Current Mobile Network Code.
 * @lac: Location Area Code.
 * @cell_id: Current Cell ID.
 *
 * GSM Cell Info. To be used only when nested into the #LocationCellInfo structure.
 */
typedef struct {
	guint16 mcc;
	guint16 mnc;
	guint16 lac;
	guint16 cell_id;
} _gsm_cell_info;

/**
 * _wcdma_cell_info:
 * @mcc: Current Mobile Country Code.
 * @mnc: Current Mobile Network Code.
 * @ucid: Current UC ID.
 *
 * WCDMA Cell Info. To be used only when nested into the #LocationCellInfo structure.
 */
typedef struct {
	guint16 mcc;
	guint16 mnc;
	guint32 ucid;
} _wcdma_cell_info;

/**
 * LocationCellInfo:
 * @flags: Which cell infos are present.
 * @gsm_cell_info: GSM Cell Info structure.
 * See #_gsm_cell_info for the fields.
 * @wcdma_cell_info: WCDMA Cell Info.
 * See #_wcdma_cell_info for the fields.
 *
 * Details about network based position.
 */
typedef struct {
	int			flags;
	_gsm_cell_info		gsm_cell_info;
	_wcdma_cell_info	wcdma_cell_info;
} LocationCellInfo;

/**
 * LOCATION_CELL_INFO_GSM_CELL_INFO_SET:
 *
 * A define indicating that the gsm_cell_info field of #LocationCellInfo is valid.
 */
#define LOCATION_CELL_INFO_GSM_CELL_INFO_SET   0x01

/**
 * LOCATION_CELL_INFO_WCDMA_CELL_INFO_SET:
 *
 * A define indicating that the wcdma_cell_info field of #LocationCellInfo is valid.
 */
#define LOCATION_CELL_INFO_WCDMA_CELL_INFO_SET 0x02

/**
 * LOCATION_GPS_DEVICE_NONE_SET:
 *
 * A define indicating that none of the #LocationGPSDeviceFix fields are valid.
 */
#define LOCATION_GPS_DEVICE_NONE_SET 0

/**
 * LOCATION_GPS_DEVICE_ALTITUDE_SET:
 *
 * A define indicating that the altitude field of #LocationGPSDeviceFix is valid.
 */
#define LOCATION_GPS_DEVICE_ALTITUDE_SET (1 << 0)

/**
 * LOCATION_GPS_DEVICE_SPEED_SET:
 *
 * A define indicating that the speed field of #LocationGPSDeviceFix is valid.
 */
#define LOCATION_GPS_DEVICE_SPEED_SET (1 << 1)

/**
 * LOCATION_GPS_DEVICE_TRACK_SET:
 *
 * A define indicating that the track field of #LocationGPSDeviceFix is valid.
 */
#define LOCATION_GPS_DEVICE_TRACK_SET (1 << 2)

/**
 * LOCATION_GPS_DEVICE_CLIMB_SET:
 *
 * A define indicating that the climb field of #LocationGPSDeviceFix is valid.
 */
#define LOCATION_GPS_DEVICE_CLIMB_SET (1 << 3)

/**
 * LOCATION_GPS_DEVICE_LATLONG_SET:
 *
 * A define indicating that the latitude and longitude fields of
 * #LocationGPSDeviceFix are valid.
 */
#define LOCATION_GPS_DEVICE_LATLONG_SET (1 << 4)

/**
 * LOCATION_GPS_DEVICE_TIME_SET:
 *
 * A define indicating that the time field of #LocationGPSDeviceFix is valid.
 */
#define LOCATION_GPS_DEVICE_TIME_SET (1 << 5)

/**
 * LocationGPSDeviceFix:
 * @mode: The mode of the fix.
 * @fields: A bitfield representing what fields contain valid data.
 * @time: The timestamp of the update.
 * @ept: Estimated time uncertainty.
 * @latitude: Fix latitude (degrees).
 * @longitude: Fix longitude (degrees).
 * @eph: Horizontal position uncertainty (cm).
 * @altitude: Fix altitude (m).
 * @epv: Vertical position (altitude) uncertainty (m).
 * @track: The current direction of motion (in degrees between 0 and 359).
 * @epd: Track uncertainty (degrees).
 * @speed: Current speed (km/h).
 * @eps: Speed uncertainty (km/h).
 * @climb: Current rate of climb (m/s).
 * @epc: Climb uncertainty (m/s).
 *
 * The details of the fix.
 */
typedef struct {
	LocationGPSDeviceMode mode;
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

	/*< private >*/
	/* These aren't used yet */
	double pitch;
	double roll;
	double dip;
} LocationGPSDeviceFix;

typedef struct _LocationGPSDevicePrivate LocationGPSDevicePrivate;

/**
 * LocationGPSDevice:
 * @online: Whether there is a connection to positioning hardware.
 * @status: The status of the device.
 * @fix: The location fix.
 * @satellites_in_view: Number of satellites the GPS device can see.
 * @satellites_in_use: Number of satellites the GPS used in calculating @fix.
 * @satellites: Array containing #LocationGPSDeviceSatellite.
 * @cell_info: Current cell info, see #LocationCellInfo.
 *
 * If the GPS has not yet obtained a fix from the device, then @fix will hold
 * the last known location.
 */
typedef struct _LocationGPSDevice {
	GObject parent;

	/*< public >*/
	gboolean online;
	LocationGPSDeviceStatus status;
	LocationGPSDeviceFix *fix;
	int satellites_in_view;
	int satellites_in_use;
	GPtrArray *satellites;
	LocationCellInfo *cell_info;

	/*< private >*/
	LocationGPSDevicePrivate *priv;
} LocationGPSDevice;

typedef struct _LocationGPSDeviceClass {
	/*< private >*/
	GObjectClass parent_class;
	void (* changed) (LocationGPSDevice *device);
	void (* connected) (LocationGPSDevice *device);
	void (* disconnected) (LocationGPSDevice *device);
} LocationGPSDeviceClass;

GType location_gps_device_get_type (void);

void location_gps_device_reset_last_known (LocationGPSDevice *device);

/**
 * location_gps_device_start:
 * @device: the device to start.
 *
 * Deprecated: This function does nothing.
 */
void location_gps_device_start (LocationGPSDevice *device);

/**
 * location_gps_device_stop:
 * @device: the device to stop.
 *
 * Deprecated: This function does nothing.
 */
void location_gps_device_stop (LocationGPSDevice *device);

G_END_DECLS

#endif
