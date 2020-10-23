/*
 * This file is part of liblocation
 *
 * Copyright (C) 2009 Nokia Corporation. All rights reserved.
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

/**
 * SECTION:location-version
 * @short_description: Version utility macros
 *
 * Macros for checking the version of liblocation to which the application was linked to.
 *
 * Available since 0.101
 */

#ifndef                                         __LOCATION_VERSION_H__
#define                                         __LOCATION_VERSION_H__

/**
 * LOCATION_MAJOR_VERSION:
 *
 * The major version of liblocation (1, if %LOCATION_VERSION is 1.2)
 */
#define LOCATION_MAJOR_VERSION (0)

/**
 * LOCATION_MINOR_VERSION:
 *
 * The minor version of liblocation (2, if %LOCATION_VERSION is 1.2)
 */
#define LOCATION_MINOR_VERSION (102)

/**
 * LOCATION_VERSION:
 *
 * The full version of liblocation (e.g. 1.2)
 */
#define LOCATION_VERSION (0.102)

/**
 * LOCATION_CHECK_VERSION:
 * @major: major version, like 1 in 1.2
 * @minor: minor version, like 2 in 1.2
 *
 * Evaluates to %TRUE if the version of liblocation is greater
 * than @major.@minor
 */
#define	LOCATION_CHECK_VERSION(major,minor)	\
    (LOCATION_MAJOR_VERSION > (major) || \
     (LOCATION_MAJOR_VERSION == (major) && LOCATION_MINOR_VERSION > (minor))

#endif                                          /* __LOCATION_VERSION_H__ */
