/* Spa
 *
 * Copyright © 2018 Wim Taymans
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <math.h>
#include <error.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>

#include <spa/support/loop.h>
#include <spa/support/log.h>
#include <spa/support/dbus.h>
#include <spa/monitor/monitor.h>
#include <spa/node/node.h>
#include <spa/node/io.h>
#include <spa/param/param.h>
#include <spa/param/props.h>
#include <spa/param/audio/format-utils.h>

#define M_PI_M2 ( M_PI + M_PI )

static struct spa_log *logger;

#define spa_debug(f,...) spa_log_trace(logger, f, __VA_ARGS__)

#include <spa/graph/graph.h>

#include <spa/debug/pod.h>
#include <spa/debug/types.h>

struct buffer {
	struct spa_buffer buffer;
	struct spa_meta metas[1];
	struct spa_meta_header header;
	struct spa_data datas[1];
	struct spa_chunk chunks[1];
};

struct data {
	struct spa_log *log;

	struct spa_loop *loop;
	struct spa_loop_control *loop_control;
	struct spa_loop_utils *loop_utils;
	bool running;

	struct spa_dbus *dbus;

	struct spa_support support[7];
	uint32_t n_support;

	struct spa_monitor *monitor;

	struct spa_graph graph;
	struct spa_graph_state graph_state;
	struct spa_graph_node source_node;
	struct spa_graph_port source_out;
	struct spa_graph_port sink_in;
	struct spa_graph_node sink_node;

	struct spa_node *sink;
	struct spa_node *source;

	struct spa_io_buffers source_sink_io[1];
	struct spa_buffer *source_buffers[1];
	struct buffer source_buffer[1];
};

static void inspect_item(struct data *data, struct spa_pod *item)
{
        spa_debug_pod(0, NULL, item);
}

static void monitor_event(void *_data, struct spa_event *event)
{
        struct data *data = _data;

        switch (SPA_MONITOR_EVENT_ID(event)) {
	case SPA_MONITOR_EVENT_Added:
                fprintf(stderr, "added:\n");
                inspect_item(data, SPA_POD_CONTENTS(struct spa_event, event));
		break;
	case SPA_MONITOR_EVENT_Removed:
                fprintf(stderr, "removed:\n");
                inspect_item(data, SPA_POD_CONTENTS(struct spa_event, event));
		break;
	case SPA_MONITOR_EVENT_Changed:
                fprintf(stderr, "changed:\n");
                inspect_item(data, SPA_POD_CONTENTS(struct spa_event, event));
		break;
        }
}

static struct spa_monitor_callbacks monitor_callbacks = {
	SPA_VERSION_MONITOR_CALLBACKS,
	.event = monitor_event,
};

static int get_handle(struct data *data,
		      struct spa_handle **handle,
		      const char *lib,
		      const char *name)
{
	int res;
	void *hnd;
	spa_handle_factory_enum_func_t enum_func;
	uint32_t i;

	if ((hnd = dlopen(lib, RTLD_NOW)) == NULL) {
		printf("can't load %s: %s\n", lib, dlerror());
		return -errno;
	}
	if ((enum_func = dlsym(hnd, SPA_HANDLE_FACTORY_ENUM_FUNC_NAME)) == NULL) {
		printf("can't find enum function\n");
		return -errno;
	}

	for (i = 0;;) {
		const struct spa_handle_factory *factory;

		if ((res = enum_func(&factory, &i)) <= 0) {
			if (res != 0)
				printf("can't enumerate factories: %s\n", spa_strerror(res));
			break;
		}
		if (strcmp(factory->name, name))
			continue;

		*handle = calloc(1, spa_handle_factory_get_size(factory, NULL));
		if ((res = spa_handle_factory_init(factory, *handle, NULL,
						   data->support,
						   data->n_support)) < 0) {
			printf("can't make factory instance: %d\n", res);
			free(*handle);
			return res;
		}
		return 0;
	}
	return -ENOENT;
}

int main(int argc, char *argv[])
{
	struct data data;
	int res;
	const char *str;
	struct spa_handle *handle;
	void *iface;

	spa_zero(data);
	if ((res = get_handle(&data, &handle,
			     "build/spa/plugins/support/libspa-support.so",
			     "logger")) < 0) {
		error(-1, res, "can't create logger");
	}

	if ((res = spa_handle_get_interface(handle,
					    SPA_TYPE_INTERFACE_Log,
					    &iface)) < 0)
		error(-1, res, "can't get log interface");

	data.log = iface;
	data.support[0] = SPA_SUPPORT_INIT(SPA_TYPE_INTERFACE_Log, data.log);
	data.n_support = 1;

	if ((str = getenv("SPA_DEBUG")))
		data.log->level = atoi(str);

	if ((res = get_handle(&data, &handle,
			     "build/spa/plugins/support/libspa-support.so",
			     "loop")) < 0) {
		error(-1, res, "can't create loop");
	}
	if ((res = spa_handle_get_interface(handle,
					    SPA_TYPE_INTERFACE_Loop,
					    &iface)) < 0)
		error(-1, res, "can't get loop interface");
	data.loop = iface;

	if ((res = spa_handle_get_interface(handle,
					    SPA_TYPE_INTERFACE_LoopControl,
					    &iface)) < 0)
		error(-1, res, "can't get loopcontrol interface");
	data.loop_control = iface;

	if ((res = spa_handle_get_interface(handle,
					    SPA_TYPE_INTERFACE_LoopUtils,
					    &iface)) < 0)
		error(-1, res, "can't get looputils interface");
	data.loop_utils = iface;

	data.support[1] = SPA_SUPPORT_INIT(SPA_TYPE_INTERFACE_DataLoop, data.loop);
	data.support[2] = SPA_SUPPORT_INIT(SPA_TYPE_INTERFACE_MainLoop, data.loop);
	data.support[3] = SPA_SUPPORT_INIT(SPA_TYPE_INTERFACE_LoopControl, data.loop_control);
	data.support[4] = SPA_SUPPORT_INIT(SPA_TYPE_INTERFACE_LoopUtils, data.loop_utils);
	data.n_support = 5;

	if ((res = get_handle(&data, &handle,
			     "build/spa/plugins/support/libspa-dbus.so",
			     "dbus")) < 0) {
		error(-1, res, "can't create dbus");
	}

	if ((res = spa_handle_get_interface(handle,
					    SPA_TYPE_INTERFACE_DBus,
					    &iface)) < 0)
		error(-1, res, "can't get dbus interface");

	data.dbus = iface;
	data.support[5] = SPA_SUPPORT_INIT(SPA_TYPE_INTERFACE_DBus, data.dbus);
	data.n_support = 6;

	if ((res = get_handle(&data, &handle,
			     "build/spa/plugins/bluez5/libspa-bluez5.so",
			     "bluez5-monitor")) < 0) {
		error(-1, res, "can't create bluez5-monitor");
	}

	if ((res = spa_handle_get_interface(handle,
					    SPA_TYPE_INTERFACE_Monitor,
					    &iface)) < 0)
		error(-1, res, "can't get monitor interface");

	data.monitor = iface;

	spa_graph_init(&data.graph, &data.graph_state);

	spa_monitor_set_callbacks(data.monitor, &monitor_callbacks, &data);

	data.running = true;
	spa_loop_control_enter(data.loop_control);
	while (data.running) {
		spa_loop_control_iterate(data.loop_control, -1);
	}
	spa_loop_control_leave(data.loop_control);

	return -1;
}
