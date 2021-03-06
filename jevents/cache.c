/* Caching layer to resolve events without re-reading them */

/*
 * Copyright (c) 2014, Intel Corporation
 * Author: Andi Kleen
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#include "jevents.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <linux/perf_event.h>

/**
 * DOC: Resolve named Intel performance events to perf
 *
 * This library allows to resolve named Intel performance counter events
 * (for example INST_RETIRED.ANY)
 * by name and turn them into perf_event_attr attributes. It also
 * supports listing all events and resolving numeric events back to names.
 *
 * The standard workflow is the user calling "event_download.py"
 * or "perf download" to download the current list, and then
 * these functions can resolve or walk names. Alternatively
 * a JSON event file from https://download.01.org/perfmon
 * can be specified through the EVENTMAP= environment variable.
 */
 
struct event {
	struct event *next;
	char *name;
	char *desc;
	char *event;
};

/* Could add a hash table, but right now expected accesses are not frequent */
static struct event *eventlist;

static int collect_events(void *data, char *name, char *event, char *desc)
{
	struct event *e = malloc(sizeof(struct event));
	if (!e)
		exit(ENOMEM);
	e->next = eventlist;
	eventlist = e;
	e->name = strdup(name);
	e->desc = strdup(desc);
	e->event = strdup(event);
	return 0;
}

static void free_events(void)
{
	struct event *e, *next;

	for (e = eventlist; e; e = next) {
		next = e->next;
		free(e);
	}
	eventlist = NULL;
}

/**
 * read_events - Read JSON performance counter event list
 * @fn: File name to read. NULL to chose default location.
 *
 * Read the JSON event list fn. The other functions in the library
 * automatically read the default event list for the current CPU,
 * but calling this explicitly is useful to chose a specific one.
*
 * Return: -1 on failure, otherwise 0.
 */
int read_events(char *fn)
{
	if (eventlist)
		free_events();
	return json_events(fn, collect_events, NULL);
}

/**
 * resolve_event - Resolve named performance counter event
 * @name: Name of performance counter event (case in-sensitive)
 * @attr: perf_event_attr to initialize with name.
 *
 * The attr structure is cleared initially.
 * The user typically has to set up attr->sample_type/read_format
 * _after_ this call.
 * Return: -1 on failure, otherwise 0.
 */

int resolve_event(char *name, struct perf_event_attr *attr)
{
	struct event *e;
	if (!eventlist) {
		if (read_events(NULL) < 0)
			return -1;
	}
	for (e = eventlist; e; e = e->next) {
		if (!strcasecmp(e->name, name)) {
			return jevent_name_to_attr(e->event, attr);
		}
	}
	return -1;
}

/**
 * walk_events - Walk all the available performance counter events
 * @func: Callback to call on each event.
 * @data: Abstract data pointer to pass to callback.
 *
 * The callback gets passed the data argument, the name of the 
 * event, the translated event in perf form (cpu/.../) and a 
 * description of the event.
 *
 * Return: -1 on failure, otherwise 0.
 */

int walk_events(int (*func)(void *data, char *name, char *event, char *desc),
		void *data)
{
	struct event *e;
	if (!eventlist) {
		if (read_events(NULL) < 0)
			return -1;
	}
	for (e = eventlist; e; e = e->next) {
		int ret = func(data, e->name, e->event, e->desc);
		if (ret)
			return ret;
	}
	return 0;
}

/**
 * rmap_event - Map numeric event back to name and description.
 * @event:  Event code (umask +
 * @name: Put pointer to event name into this. No need to free.
 * @desc: Put pointer to description into this. No need to free. Can be NULL.
 *
 * Offcore matrix events are not fully supported.
 * Ignores bits other than umask/event for now, so some events using cmask,inv
 * may be misidentified.
 * Return: -1 on failure, otherwise 0.
 */

int rmap_event(unsigned event, char **name, char **desc)
{
	struct event *e;
	if (!eventlist) {
		if (read_events(NULL) < 0)
			return -1;
	}
	for (e = eventlist; e; e = e->next) {
		// XXX should cache the numeric value
		char *s;
		unsigned event = 0, umask = 0;
		s = strstr(e->event, "event=");
		if (s)
			sscanf(s, "event=%x", &event);
		s = strstr(e->event, "umask=");
		if (s)
			sscanf(s, "umask=%x", &umask);
		if ((event | (umask << 8)) == (event & 0xffff)) {
			*name = e->name;
			if (desc)
				*desc = e->desc;
			return 0;
		}
	}
	return -1;

}
