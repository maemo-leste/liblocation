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
