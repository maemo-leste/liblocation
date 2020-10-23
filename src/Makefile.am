lib_LTLIBRARIES = liblocation.la

liblocation_la_SOURCES = \
	location-distance-utils.c \
	location-distance-utils.h \
	location-gpsd-control.c \
	location-gpsd-control.h \
	location-gps-device.c \
	location-gps-device.h \
	location-misc.c \
	location-misc.h \
	location-version.h

liblocation_la_CFLAGS = $(LIBLOCATION_CFLAGS) -Wall
liblocation_la_LDFLAGS = -lm -Wl,--as-needed

liblocationincludedir=$(includedir)
liblocationinclude_HEADERS = \
	location-distance-utils.h \
	location-gpsd-control.h \
	location-gps-device.h \
	location-misc.h \
	location-version.h
