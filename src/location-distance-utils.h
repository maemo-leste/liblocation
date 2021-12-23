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

#ifndef __LOCATION_DISTANCE_UTILS_H__
#define __LOCATION_DISTANCE_UTILS_H__

#include <glib.h>

G_BEGIN_DECLS

double lng_scale(double lat);

double location_distance_between (double latitude_s,
		double longitude_s,
		double latitude_f,
		double longitude_f);

G_END_DECLS

#endif
