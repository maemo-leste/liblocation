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

#include <math.h>

#include "location-distance-utils.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// scaling factor from degrees to meters at equator
// == DEG_TO_RAD * radius of earth
static float LOCATION_SCALING_FACTOR = 111318.84502145034f;

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define D2R (M_PI / 180.0f)
// #define R2D (180.0f / M_PI)

// compensate for shrinking longitude towards poles
double lng_scale(double lat)
{
	double scale = cosf(lat * D2R);
	return MAX(scale, 0.01f);
}

/*
 * From https://github.com/maemo-leste/liblocation/pull/1
 *
 * The original code used Spherical law of cosines which gives good results down
 * to distances as small as few meters, what is used here is Equirectangular
 * approximation which is accurate for small distances only but performs well.
 * of course there's Haversine which is accurate for all king of distances but
 * considered relatively slow.  I could add Haversine calculations, what do you
 * think?  here's a link talks about all three laws:
 *
 * https://www.movable-type.co.uk/scripts/latlong.html
 *
 */
// return distance in meters between two locations
double location_distance_between(double latitude_s,
		double longitude_s,
		double latitude_f,
		double longitude_f)
{
	double dlat = (latitude_f - latitude_s);
	double dlng = (longitude_f - longitude_s) * lng_scale(latitude_s);
	return sqrtf(dlat * dlat + dlng * dlng) * LOCATION_SCALING_FACTOR;
}
