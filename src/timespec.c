/**
 * @file timespec.c
 * @brief Portable Scheduler Library (libpsched)
 *        Timespec interface
 *
 * Date: 28-05-2014
 * 
 * Copyright 2014 Pedro A. Hortas (pah@ucodev.org)
 *
 * This file is part of libpsched.
 *
 * libpsched is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libpsched is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libpsched.  If not, see <http://www.gnu.org/licenses/>.
 *
 */


#include <time.h>

void timespec_add(struct timespec *dest, const struct timespec *src) {
	long tmp = src->tv_nsec + dest->tv_nsec;

	dest->tv_sec += src->tv_sec + (tmp > 999999999);
	dest->tv_nsec = (tmp > 999999999) ? tmp - 1000000000 : tmp;
}
