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

static double d2r(double deg)
{
	return (deg * M_PI / 180);
}

static double r2d(double rad)
{
	return (rad * 180 / M_PI);
}

double location_distance_between(double latitude_s,
		double longitude_s,
		double latitude_f,
		double longitude_f)
{
	double theta, dist;

	theta = longitude_s - longitude_f;
	dist = sin(d2r(latitude_s)) * sin(d2r(latitude_f)) \
		+ cos(d2r(latitude_s)) * cos(d2r(latitude_f)) \
		* cos(d2r(theta));
	dist = acos(dist);
	dist = r2d(dist);
	dist = dist * 60 * 1.1515;
	dist = dist * 1.609344; /* kilometers */

	return dist;
}
