/* PipeWire
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

#include <stdio.h>
#include <sys/mman.h>

#define WIDTH   640
#define HEIGHT  480
#define BPP    3

#include "sdl.h"

#include <spa/param/video/format-utils.h>
#include <spa/param/props.h>
#include <spa/node/io.h>
#include <spa/debug/format.h>

#include <pipewire/pipewire.h>

struct data {
	SDL_Renderer *renderer;
	SDL_Window *window;
	SDL_Texture *texture;

	struct pw_main_loop *loop;
	struct spa_source *timer;

	struct pw_core *core;
	struct pw_node *node;
	struct spa_port_info port_info;

	struct pw_node *v4l2;

	struct pw_link *link;

	struct spa_node impl_node;
	struct spa_io_buffers *io;

	const struct spa_node_callbacks *callbacks;
	void *callbacks_data;

	struct spa_video_info_raw format;
	int32_t stride;

	struct spa_buffer *buffers[32];
	int n_buffers;
};

static void handle_events(struct data *data)
{
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		switch (event.type) {
		case SDL_QUIT:
			pw_main_loop_quit(data->loop);
			break;
		}
	}
}

static int impl_send_command(struct spa_node *node, const struct spa_command *command)
{
	return 0;
}

static int impl_set_callbacks(struct spa_node *node,
			      const struct spa_node_callbacks *callbacks, void *data)
{
	struct data *d = SPA_CONTAINER_OF(node, struct data, impl_node);
	d->callbacks = callbacks;
	d->callbacks_data = data;
	return 0;
}

static int impl_get_n_ports(struct spa_node *node,
			    uint32_t *n_input_ports,
			    uint32_t *max_input_ports,
			    uint32_t *n_output_ports,
			    uint32_t *max_output_ports)
{
	*n_input_ports = *max_input_ports = 1;
	*n_output_ports = *max_output_ports = 0;
	return 0;
}

static int impl_get_port_ids(struct spa_node *node,
                             uint32_t *input_ids,
                             uint32_t n_input_ids,
                             uint32_t *output_ids,
                             uint32_t n_output_ids)
{
	if (n_input_ids > 0)
                input_ids[0] = 0;
	return 0;
}

static int impl_port_set_io(struct spa_node *node, enum spa_direction direction, uint32_t port_id,
			    uint32_t id, void *data, size_t size)
{
	struct data *d = SPA_CONTAINER_OF(node, struct data, impl_node);

	if (id == SPA_IO_Buffers)
		d->io = data;
	else
		return -ENOENT;

	return 0;
}

static int impl_port_get_info(struct spa_node *node, enum spa_direction direction, uint32_t port_id,
			      const struct spa_port_info **info)
{
	struct data *d = SPA_CONTAINER_OF(node, struct data, impl_node);

	d->port_info.flags = SPA_PORT_INFO_FLAG_CAN_USE_BUFFERS;
	d->port_info.rate = 0;
	d->port_info.props = NULL;

	*info = &d->port_info;

	return 0;
}

static int port_enum_formats(struct spa_node *node,
			     enum spa_direction direction, uint32_t port_id,
			     uint32_t *index,
			     const struct spa_pod *filter,
			     struct spa_pod **result,
			     struct spa_pod_builder *builder)
{
	struct data *d = SPA_CONTAINER_OF(node, struct data, impl_node);
	SDL_RendererInfo info;

	if (*index != 0)
		return 0;

	SDL_GetRendererInfo(d->renderer, &info);
	*result = sdl_build_formats(&info, builder);

	(*index)++;

	return 1;
}

static int impl_port_enum_params(struct spa_node *node,
				 enum spa_direction direction, uint32_t port_id,
				 uint32_t id, uint32_t *index,
				 const struct spa_pod *filter,
				 struct spa_pod **result,
				 struct spa_pod_builder *builder)
{
	struct data *d = SPA_CONTAINER_OF(node, struct data, impl_node);

	switch (id) {
	case SPA_PARAM_EnumFormat:
		return port_enum_formats(node, direction, port_id, index, filter, result, builder);

	case SPA_PARAM_Buffers:
		if (*index > 0)
			return 0;

		*result = spa_pod_builder_object(builder,
			SPA_TYPE_OBJECT_ParamBuffers, id,
			SPA_PARAM_BUFFERS_buffers, &SPA_POD_CHOICE_RANGE_Int(2, 1, 32),
			SPA_PARAM_BUFFERS_blocks,  &SPA_POD_Int(1),
			SPA_PARAM_BUFFERS_size,    &SPA_POD_Int(d->stride * d->format.size.height),
			SPA_PARAM_BUFFERS_stride,  &SPA_POD_Int(d->stride),
			SPA_PARAM_BUFFERS_align,   &SPA_POD_Int(16),
			0);
		break;

	case SPA_PARAM_Meta:
		if (*index > 0)
			return 0;

		*result = spa_pod_builder_object(builder,
			SPA_TYPE_OBJECT_ParamMeta, id,
			SPA_PARAM_META_type, &SPA_POD_Id(SPA_META_Header),
			SPA_PARAM_META_size, &SPA_POD_Int(sizeof(struct spa_meta_header)),
			0);
		break;

	default:
		return -ENOENT;
	}

	(*index)++;
	return 1;
}

static int port_set_format(struct spa_node *node, enum spa_direction direction, uint32_t port_id,
			   uint32_t flags, const struct spa_pod *format)
{
	struct data *d = SPA_CONTAINER_OF(node, struct data, impl_node);
	Uint32 sdl_format;
	void *dest;

	if (format == NULL)
		return 0;

	spa_debug_format(0, NULL, format);

	spa_format_video_raw_parse(format, &d->format);

	sdl_format = id_to_sdl_format(d->format.format);
	if (sdl_format == SDL_PIXELFORMAT_UNKNOWN)
		return -EINVAL;

	d->texture = SDL_CreateTexture(d->renderer,
				       sdl_format,
				       SDL_TEXTUREACCESS_STREAMING,
				       d->format.size.width,
				       d->format.size.height);
	SDL_LockTexture(d->texture, NULL, &dest, &d->stride);
	SDL_UnlockTexture(d->texture);

	return 0;
}

static int impl_port_set_param(struct spa_node *node,
			       enum spa_direction direction, uint32_t port_id,
			       uint32_t id, uint32_t flags,
			       const struct spa_pod *param)
{
	if (id == SPA_PARAM_Format) {
		return port_set_format(node, direction, port_id, flags, param);
	}
	else
		return -ENOENT;
}

static int impl_port_use_buffers(struct spa_node *node, enum spa_direction direction, uint32_t port_id,
				 struct spa_buffer **buffers, uint32_t n_buffers)
{
	struct data *d = SPA_CONTAINER_OF(node, struct data, impl_node);
	uint32_t i;

	for (i = 0; i < n_buffers; i++)
		d->buffers[i] = buffers[i];
	d->n_buffers = n_buffers;
	return 0;
}

static int do_render(struct spa_loop *loop, bool async, uint32_t seq,
		     const void *_data, size_t size, void *user_data)
{
	struct data *d = user_data;
	struct spa_buffer *buf;
	uint8_t *map;
	void *sdata, *ddata;
	int sstride, dstride, ostride;
	uint32_t i;
	uint8_t *src, *dst;

	buf = d->buffers[d->io->buffer_id];

	if (buf->datas[0].type == SPA_DATA_MemFd ||
	    buf->datas[0].type == SPA_DATA_DmaBuf) {
		map = mmap(NULL, buf->datas[0].maxsize + buf->datas[0].mapoffset, PROT_READ,
			   MAP_PRIVATE, buf->datas[0].fd, 0);
		sdata = SPA_MEMBER(map, buf->datas[0].mapoffset, uint8_t);
	} else if (buf->datas[0].type == SPA_DATA_MemPtr) {
		map = NULL;
		sdata = buf->datas[0].data;
	} else
		return -EINVAL;

	if (SDL_LockTexture(d->texture, NULL, &ddata, &dstride) < 0) {
		fprintf(stderr, "Couldn't lock texture: %s\n", SDL_GetError());
		return -EIO;
	}
	sstride = buf->datas[0].chunk->stride;
	ostride = SPA_MIN(sstride, dstride);

	src = sdata;
	dst = ddata;
	for (i = 0; i < d->format.size.height; i++) {
		memcpy(dst, src, ostride);
		src += sstride;
		dst += dstride;
	}
	SDL_UnlockTexture(d->texture);

	SDL_RenderClear(d->renderer);
	SDL_RenderCopy(d->renderer, d->texture, NULL, NULL);
	SDL_RenderPresent(d->renderer);

	if (map)
		munmap(map, buf->datas[0].maxsize + buf->datas[0].mapoffset);

	return 0;
}

static int impl_node_process(struct spa_node *node)
{
	struct data *d = SPA_CONTAINER_OF(node, struct data, impl_node);
	int res;

	if ((res = pw_loop_invoke(pw_main_loop_get_loop(d->loop), do_render,
				  SPA_ID_INVALID, NULL, 0, true, d)) < 0)
		return res;

	handle_events(d);

	d->io->status = SPA_STATUS_NEED_BUFFER;

	return SPA_STATUS_NEED_BUFFER;
}

static const struct spa_node impl_node = {
	SPA_VERSION_NODE,
	NULL,
	.send_command = impl_send_command,
	.set_callbacks = impl_set_callbacks,
	.get_n_ports = impl_get_n_ports,
	.get_port_ids = impl_get_port_ids,
	.port_set_io = impl_port_set_io,
	.port_get_info = impl_port_get_info,
	.port_enum_params = impl_port_enum_params,
	.port_set_param = impl_port_set_param,
	.port_use_buffers = impl_port_use_buffers,
	.process = impl_node_process,
};

static void make_nodes(struct data *data)
{
	struct pw_factory *factory;
	struct pw_properties *props;

	data->node = pw_node_new(data->core, "SDL-sink", NULL, 0);
	data->impl_node = impl_node;
	pw_node_set_implementation(data->node, &data->impl_node);

	pw_node_register(data->node, NULL, NULL, NULL);

	factory = pw_core_find_factory(data->core, "spa-node-factory");
	props = pw_properties_new("spa.library.name", "v4l2/libspa-v4l2",
				  "spa.factory.name", "v4l2-source", NULL);
	data->v4l2 = pw_factory_create_object(factory,
					      NULL,
					      PW_TYPE_INTERFACE_Node,
					      PW_VERSION_NODE,
					      props,
					      SPA_ID_INVALID);
	data->link = pw_link_new(data->core,
				 pw_node_find_port(data->v4l2, PW_DIRECTION_OUTPUT, 0),
				 pw_node_find_port(data->node, PW_DIRECTION_INPUT, 0),
				 NULL,
				 NULL,
				 NULL,
				 0);
	pw_link_register(data->link, NULL, NULL, NULL);

	pw_node_set_active(data->node, true);
	pw_node_set_active(data->v4l2, true);
}

int main(int argc, char *argv[])
{
	struct data data = { 0, };

	pw_init(&argc, &argv);

	data.loop = pw_main_loop_new(NULL);
	data.core = pw_core_new(pw_main_loop_get_loop(data.loop), NULL, 0);

	pw_module_load(data.core, "libpipewire-module-spa-node-factory", NULL, NULL, NULL, NULL);

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		printf("can't initialize SDL: %s\n", SDL_GetError());
		return -1;
	}

	if (SDL_CreateWindowAndRenderer
	    (WIDTH, HEIGHT, SDL_WINDOW_RESIZABLE, &data.window, &data.renderer)) {
		printf("can't create window: %s\n", SDL_GetError());
		return -1;
	}

	make_nodes(&data);

	pw_main_loop_run(data.loop);

	pw_link_destroy(data.link);
	pw_node_destroy(data.node);
	pw_core_destroy(data.core);
	pw_main_loop_destroy(data.loop);

	return 0;
}
