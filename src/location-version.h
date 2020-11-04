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
