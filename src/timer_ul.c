/**
 * @file timer_ul.c
 * @brief Portable Scheduler Library (libpsched)
 *        A userland implementation of the timer_*() calls
 *
 * Date: 10-03-2015
 * 
 * Copyright 2014-2015 Pedro A. Hortas (pah@ucodev.org)
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>

#include "timer_ul.h"
#include "mm.h"
#include "timespec.h"

/* Globals */
static struct timer_ul *_timers = NULL;
static size_t _nr_timers = 0;
static pthread_mutex_t _mutex_timers = PTHREAD_MUTEX_INITIALIZER;

static void *_timer_process(void *arg) {
	struct timer_ul *timer = arg;
	struct timespec tcur, tsleep;

	pthread_mutex_init(&timer->t_mutex, NULL);
	pthread_cond_init(&timer->t_cond, NULL);

	pthread_mutex_lock(&timer->t_mutex);

	for (;;) {
		/* Process timer based on flags */
		if (timer->flags & TIMER_ABSTIME) {
			/* If absolute ... */
			memcpy(&tsleep, &timer->arm.it_value, sizeof(struct timeval));
			clock_gettime(timer->clockid, &tcur);
			timespec_sub(&tsleep, &tcur);
		} else {
			/* If relative ... */
			memcpy(&tsleep, &timer->arm.it_value, sizeof(struct timeval));
		}

		/* Wait until we're ready to notify */
		if (nanosleep(&tsleep, &timer->rem) < 0) {
			/* If interrupted, update the timer value with the remaining time */
			if ((errno == EINTR) && (timer->flags & TIMER_ABSTIME))
				memcpy(&timer->arm.it_value, &timer->rem, sizeof(struct timeval));
		}

		/* Interrupt flag means urgent stop of this thread */
		if (timer->t_flags & PSCHED_TIMER_UL_THREAD_INTR_FLAG)
			break;	/* Thread must exit */

		/* A read operation for the timer was requested. We must now wait for the read
		 * operation to complete and proceed when we're signaled.
		 */
		if (timer->t_flags & PSCHED_TIMER_UL_THREAD_READ_FLAG) {
			while (timer->t_flags & PSCHED_TIMER_UL_THREAD_READ_FLAG)
				pthread_cond_wait(&timer->t_cond, &timer->t_mutex);

			continue;
		}

		/* A write operation for the timer was requested. We must now wait for the write
		 * operation to complete and proceed when we're signaled.
		 */
		if (timer->t_flags & PSCHED_TIMER_UL_THREAD_WRITE_FLAG) {
			while (timer->t_flags & PSCHED_TIMER_UL_THREAD_WRITE_FLAG)
				pthread_cond_wait(&timer->t_cond, &timer->t_mutex);

			continue;
		}

		/* TODO: Do notify */

		/* TODO: Add the it_interval to it_value, or break the loop if no interval is set */
	}

	pthread_mutex_unlock(&timer->t_mutex);

	/* All good */
	pthread_exit(NULL);

	return NULL;
}

/* API */
int timer_create_ul(clockid_t clockid, struct sigevent *sevp, timer_t *timerid) {
	int i = 0, errsv = 0, slot = -1;

	/* Acquire timers critical region lock */
	pthread_mutex_lock(&_mutex_timers);

	/* Check if there's a free control slot */
	for (i = 0; _timers && (i < _nr_timers); i ++) {
		if (!_timers[i].id) {
			slot = i;
			break;
		}
	}

	/* Check if we've found a free control slot */
	if (slot == -1) {
		/* No free slots... allocate a new one */
		if (!(_timers = mm_realloc(_timers, _nr_timers + 1))) {
			pthread_mutex_unlock(&_mutex_timers);

			/* Everything will be screwed from now on... */
			abort();
		}

		/* Update our slot index */
		slot = _nr_timers;

		/* Update number of allocated timer control slots */
		_nr_timers ++;
	}

	/* Reset timer memory */
	memset(&_timers[slot], 0, sizeof(struct timer_ul));

	/* Validate clockid value */
	switch (clockid) {
		case CLOCK_REALTIME: {
		} break;
		case CLOCK_MONOTONIC: {
		} break;
		case CLOCK_PROCESS_CPUTIME_ID: {
		} break;
		case CLOCK_THREAD_CPUTIME_ID: {
		} break;
		default: {
			errsv = EINVAL;
			goto _create_failure;
		}
	}

	/* Set the clock id */
	_timers[slot].clockid = clockid;

	/* Validate sigevent */
	if (!sevp) {
		errsv = EINVAL;
		goto _create_failure;
	}

	/* Store sigevent data */
	memcpy(&_timers[slot].sevp, sevp, sizeof(struct sigevent));

	/* Grant that ID is never 0 */
	*timerid = _timers[slot].id = (timer_t) (uintptr_t) (slot + 1); /* Grant that ID is never 0 */

	/* Release timers critical region lock */
	pthread_mutex_unlock(&_mutex_timers);

	/* All good */
	return 0;

_create_failure:
	memset(&_timers[slot], 0, sizeof(struct timer_ul));

	pthread_mutex_unlock(&_mutex_timers);

	errno = errsv;

	return -1;
}

int timer_delete_ul(timer_t timerid) {
	errno = ENOSYS;
	return -1;
}

int timer_settime_ul(
	timer_t timerid,
	int flags,
	const struct itimerspec *new_value,
	struct itimerspec *old_value)
{
	int errsv = 0;
	uintptr_t slot = (uintptr_t) timerid;

	/* TODO: Disarm the timer if new_value is zeroed */

	/* TODO: Cancel the timer if it is armed (killing the thread) and setup (arm) it again */

	/* Acquire timers critical region lock */
	pthread_mutex_lock(&_mutex_timers);

	/* Set the init time */
	if (clock_gettime(_timers[slot].clockid, &_timers[slot].init_time) < 0) {
		errsv = errno;
		goto _settime_failure;
	}

	/* Copy the timer values */
	memcpy(&_timers[slot].arm, new_value, sizeof(struct itimerspec));

	/* Create a thread to process this timer */
	if (pthread_create(&_timers[slot].t_id, NULL, &_timer_process, &_timers[slot])) {
		errsv = errno;
		goto _settime_failure;
	}

	/* Mark the timmer as armed */
	_timers[slot].t_flags |= PSCHED_TIMER_UL_THREAD_ARMED_FLAG;

	/* Release timers critical region lock */
	pthread_mutex_unlock(&_mutex_timers);

	/* All good */
	return 0;

_settime_failure:
	/* Release timers critical region lock */
	pthread_mutex_unlock(&_mutex_timers);

	errno = errsv;

	return -1;
}

int timer_gettime_ul(timer_t timerid, struct itimerspec *curr_value) {
	uintptr_t slot = (uintptr_t) timerid;

	/* Acquire timers critical region lock */
	pthread_mutex_lock(&_mutex_timers);

	/* This is a read operation */
	_timers[slot].t_flags |= PSCHED_TIMER_UL_THREAD_READ_FLAG;

	/* Interrupt thread if it's sleeping */
	pthread_cancel(_timers[slot].t_id);

	/* Acquire the target timer thread mutex, granting that a condition wait occurred */
	pthread_mutex_lock(&_timers[slot].t_mutex);

	/* Populate the curr_value */
	memcpy(&curr_value->it_interval, &_timers[slot].arm.it_interval, sizeof(struct timespec));
	memcpy(&curr_value->it_value, &_timers[slot].rem, sizeof(struct timespec));

	/* We're done with reads */
	_timers[slot].t_flags &= ~PSCHED_TIMER_UL_THREAD_READ_FLAG;

	/* Set the thread free */
	pthread_cond_signal(&_timers[slot].t_cond);

	/* Release the target timer thread mutex */
	pthread_mutex_unlock(&_timers[slot].t_mutex);

	/* Release timers critical region lock */
	pthread_mutex_unlock(&_mutex_timers);

	return 0;
}

int timer_getoverrun_ul(timer_t timerid) {
	uintptr_t slot = (uintptr_t) timerid;

	return _timers[slot].overruns;
}

