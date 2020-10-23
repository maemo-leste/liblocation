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

#ifndef __LOCATION_DISTANCE_UTILS_H__
#define __LOCATION_DISTANCE_UTILS_H__

#include <glib.h>

G_BEGIN_DECLS

double location_distance_between (double latitude_s,
				  double longitude_s,
				  double latitude_f,
				  double longitude_f);

}

G_END_DECLS

#endif
