#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <gconf/gconf-client.h>

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

struct _LocationGPSDControlPrivate
{
	GMainContext **ctx;
	GConfClient *client;
	char *p2;
	char *foo3;
	char *p6;
	DBusGConnection *bus;
	DBusGProxy *proxy;
	char *foo;
	gchar *cdr_method;
	gchar *car_method;
	guint client_id;
	gboolean gps_disabled;
	gboolean net_disabled;
	gboolean disclaimer_accepted;
	char *p18;
	char *foo2;
	char *p16;
	char *p17;
	char *foo1;
	gchar *device_mode;
	GSource *timeout_source;
};

G_DEFINE_TYPE_WITH_PRIVATE(LocationGPSDControl, location_gpsd_control, G_TYPE_OBJECT);

static void class_set_property(LocationGPSDControl *control, unsigned int property,
		const GValue *value, LocationGPSDControlClass *klass)
{
	GType type;
	GTypeClass *class; // TODO: This declaration might be wrong?
	GSList *list;
	const gchar *klass_type, *parent_type;

	type = LOCATION_TYPE_GPSD_CONTROL;
	class = g_type_check_instance_cast(&control->parent.g_type_instance, type)[3].g_class;

	switch (property) {
		case 2:
			class[17].g_type = g_value_get_int(value);
			break;
		case 3:
			class->g_type = (GType)g_value_get_pointer(value);
			break;
		case 1:
			class[16].g_type = g_value_get_int(value);
			break;
		default:
			list = klass->parent_class.construct_properties;
			klass_type = g_type_name(klass->parent_class.g_type_class.g_type);
			parent_type = g_type_name(control->parent.g_type_instance.g_class->g_type);
			g_log("liblocation", G_LOG_LEVEL_WARNING,
					"%s: invalid property id %u for \"%s\" of type `%s' in `%s'",
					G_STRLOC, property, (char *)list, klass_type, parent_type);
			break;
	}
}

static void class_get_property(LocationGPSDControl *control, unsigned int property,
		GValue *value, LocationGPSDControlClass *klass)
{
	GType type;
	GTypeClass *class; // TODO: This declaration might be wrong?
	GSList *list;
	const gchar *klass_type, *parent_type;

	type = LOCATION_TYPE_GPSD_CONTROL;
	class = g_type_check_instance_cast(&control->parent.g_type_instance, type)[3].g_class;

	switch (property) {
		case 2:
			g_value_set_int(value, class[17].g_type);
			break;
		case 3:
			break;
		case 1:
			g_value_set_int(value, class[16].g_type);
			break;
		default:
			list = klass->parent_class.construct_properties;
			klass_type = g_type_name(klass->parent_class.g_type_class.g_type);
			parent_type = g_type_name(control->parent.g_type_instance.g_class->g_type);
			g_log("liblocation", G_LOG_LEVEL_WARNING,
					"%s: invalid property id %u for \"%s\" of type `%s' in `%s'",
					G_STRLOC, property, (char *)list, klass_type, parent_type);
			break;
	}
}

static void location_gpsd_control_class_init(LocationGPSDControlClass *klass)
{
	GParamSpec *method, *interval, *maincontext;

	// klass->parent_class.dispose = (void (*)(GObject *))&loc_6160;
	klass->parent_class.set_property = (void (*)(GObject *, guint,
				const GValue *, GParamSpec *))class_set_property;
	klass->parent_class.get_property = (void (*)(GObject *, guint,
				GValue *, GParamSpec *))class_get_property;

	signals[ERROR] = g_signal_new("error",
			klass->parent_class.g_type_class.g_type,
			G_SIGNAL_NO_RECURSE|G_SIGNAL_RUN_FIRST,
			0x44u,
			0,
			0,
			(GSignalCMarshaller)&g_cclosure_marshal_VOID__VOID,
			4u,
			0);

	signals[ERROR_VERBOSE] = g_signal_new("error-verbose",
			klass->parent_class.g_type_class.g_type,
			G_SIGNAL_NO_RECURSE|G_SIGNAL_RUN_FIRST,
			0x48u,
			0,
			0,
			(GSignalCMarshaller)&g_cclosure_marshal_VOID__INT,
			4u,
			1u,
			24);

	signals[GPSD_RUNNING] = g_signal_new("gpsd-running",
			klass->parent_class.g_type_class.g_type,
			G_SIGNAL_NO_RECURSE|G_SIGNAL_RUN_FIRST,
			0x4Cu,
			0,
			0,
			(GSignalCMarshaller)&g_cclosure_marshal_VOID__VOID,
			4u,
			0);

	signals[GPSD_STOPPED] = g_signal_new("gpsd-stopped",
			klass->parent_class.g_type_class.g_type,
			G_SIGNAL_NO_RECURSE|G_SIGNAL_RUN_FIRST,
			0x50u,
			0,
			0,
			(GSignalCMarshaller)&g_cclosure_marshal_VOID__VOID,
			4u,
			0);

	method = g_param_spec_int("preferred-method",
			"Preferred method",
			"The positioning method the application would like to use",
			0,
			15,
			0,
			G_PARAM_WRITABLE|G_PARAM_READABLE);
	g_object_class_install_property(&klass->parent_class, 1, method);

	interval = g_param_spec_int("preferred-interval",
			"Preferred interval",
			"The interval between fixes the application prefers. The actual rate may be different.",
			-1,
			0x7FFFFFFF,
			-1,
			G_PARAM_WRITABLE|G_PARAM_READABLE);
	g_object_class_install_property(&klass->parent_class, 2, interval);

	maincontext = g_param_spec_pointer("maincontext-pointer",
			"The pointer to the GMainContext instance",
			"Set the main context address",
			G_PARAM_WRITABLE);
	g_object_class_install_property(&klass->parent_class, 3, maincontext);
}

static void device_mode_change_cb(int a1, const gchar *sig, LocationGPSDControl *control)
{
	LocationGPSDControlPrivate *priv;
	gchar *device_mode, *_sig;

	priv = control->priv;
	device_mode = priv->device_mode;
	_sig = g_strdup(sig);
	priv = control->priv;
	priv->device_mode = _sig;

	if (priv->p2 && g_strcmp0(device_mode, priv->device_mode)) {
		dbus_proxy_shutdown((int)&control->priv->p2);
		location_gpsd_control_prestart_internal(control, TRUE);
	}

	g_free(device_mode);
}

static void location_gpsd_control_init(LocationGPSDControl *control)
{
	LocationGPSDControlPrivate *priv;
	DBusGConnection *dbus_conn;
	DBusGProxy *proxy;
	GConfValue *method;

	priv = G_TYPE_INSTANCE_GET_PRIVATE(control, G_TYPE_OBJECT, LocationGPSDControlPrivate);
	control->priv = priv;

	priv->p16 = NULL;
	priv->p17 = NULL;

	priv->client = gconf_client_get_default();
	dbus_conn = dbus_g_bus_get(DBUS_BUS_SYSTEM, NULL);
	priv->bus = dbus_g_connection_ref(dbus_conn);

	priv->gps_disabled = gconf_client_get_bool(priv->client, GCONF_GPS_DISABLED, NULL);
	priv->net_disabled = gconf_client_get_bool(priv->client, GCONF_NET_DISABLED, NULL);
	priv->disclaimer_accepted = gconf_client_get_bool(priv->client,
			GCONF_DISCLAIMER_ACCEPTED, NULL);

	method = gconf_client_get(priv->client, GCONF_METHOD, NULL);
	if (method) {
		get_location_method(control, method);
		gconf_value_free(method);
	}

	gconf_client_add_dir(priv->client, GCONF_LOC, GCONF_CLIENT_PRELOAD_NONE, NULL);

	priv->client_id = gconf_client_notify_add(priv->client, GCONF_LOC,
			NULL, // (GConfClientNotifyFunc)&loc_6380,
			control,
			NULL,
			NULL);

	proxy = dbus_g_proxy_new_for_name(priv->bus, "com.nokia.mce",
			"/com/nokia/mce/signal", "com.nokia.mce.signal");
	dbus_g_proxy_add_signal(proxy, "sig_device_mode_ind", 0x40u, NULL);
	dbus_g_proxy_connect_signal(proxy, "sig_device_mode_ind",
			(GCallback)device_mode_change_cb, control, NULL);
	priv->proxy = proxy;
}
