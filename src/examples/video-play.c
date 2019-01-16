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
#include <unistd.h>
#include <sys/mman.h>

#include <spa/param/video/format-utils.h>
#include <spa/param/props.h>
#include <spa/debug/format.h>

#include <pipewire/pipewire.h>

#define WIDTH   640
#define HEIGHT  480
#define BPP    3

#define MAX_BUFFERS	64

#include "sdl.h"

struct data {
	const char *path;

	SDL_Renderer *renderer;
	SDL_Window *window;
	SDL_Texture *texture;
	SDL_Texture *cursor;

	struct pw_main_loop *loop;

	struct pw_core *core;
	struct pw_remote *remote;
	struct spa_hook remote_listener;

	struct pw_stream *stream;
	struct spa_hook stream_listener;

	struct spa_video_info_raw format;
	int32_t stride;

	int counter;
	SDL_Rect rect;
	SDL_Rect cursor_rect;
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

static void
on_process(void *_data)
{
	struct data *data = _data;
	struct pw_stream *stream = data->stream;
	struct pw_buffer *b;
	struct spa_buffer *buf;
	void *sdata, *ddata;
	int sstride, dstride, ostride;
	struct spa_meta_region *mc;
	struct spa_meta_cursor *mcs;
	uint32_t i;
	uint8_t *src, *dst;
	bool render_cursor = false;

	b = pw_stream_dequeue_buffer(stream);
	if (b == NULL)
		return;

	buf = b->buffer;

	pw_log_trace("new buffer %p", buf);

	handle_events(data);

	if ((sdata = buf->datas[0].data) == NULL)
		return;

	if (SDL_LockTexture(data->texture, NULL, &ddata, &dstride) < 0) {
		fprintf(stderr, "Couldn't lock texture: %s\n", SDL_GetError());
		return;
	}

	if ((mc = spa_buffer_find_meta_data(buf, SPA_META_VideoCrop, sizeof(*mc))) &&
	    spa_meta_region_is_valid(mc)) {
		data->rect.x = mc->region.position.x;
		data->rect.y = mc->region.position.y;
		data->rect.w = mc->region.size.width;
		data->rect.h = mc->region.size.height;
	}
	if ((mcs = spa_buffer_find_meta_data(buf, SPA_META_Cursor, sizeof(*mcs))) &&
	    spa_meta_cursor_is_valid(mcs)) {
		struct spa_meta_bitmap *mb;
		void *cdata;
		int cstride;

		data->cursor_rect.x = mcs->position.x;
		data->cursor_rect.y = mcs->position.y;

		mb = SPA_MEMBER(mcs, mcs->bitmap_offset, struct spa_meta_bitmap);
		data->cursor_rect.w = mb->size.width;
		data->cursor_rect.h = mb->size.height;

		if (data->cursor == NULL) {
			data->cursor = SDL_CreateTexture(data->renderer,
						 id_to_sdl_format(mb->format),
						 SDL_TEXTUREACCESS_STREAMING,
						 mb->size.width, mb->size.height);
			SDL_SetTextureBlendMode(data->cursor, SDL_BLENDMODE_BLEND);
		}


		if (SDL_LockTexture(data->cursor, NULL, &cdata, &cstride) < 0) {
			fprintf(stderr, "Couldn't lock cursor texture: %s\n", SDL_GetError());
			goto done;
		}

		src = SPA_MEMBER(mb, mb->offset, uint8_t);
		dst = cdata;
		ostride = SPA_MIN(cstride, mb->stride);

		for (i = 0; i < mb->size.height; i++) {
			memcpy(dst, src, ostride);
			dst += cstride;
			src += mb->stride;
		}
		SDL_UnlockTexture(data->cursor);

		render_cursor = true;
	}

	sstride = buf->datas[0].chunk->stride;
	ostride = SPA_MIN(sstride, dstride);

	src = sdata;
	dst = ddata;
	for (i = 0; i < data->format.size.height; i++) {
		memcpy(dst, src, ostride);
		src += sstride;
		dst += dstride;
	}
	SDL_UnlockTexture(data->texture);

	SDL_RenderClear(data->renderer);
	SDL_RenderCopy(data->renderer, data->texture, &data->rect, NULL);
	if (render_cursor) {
		SDL_RenderCopy(data->renderer, data->cursor, NULL, &data->cursor_rect);
	}
	SDL_RenderPresent(data->renderer);

      done:
	pw_stream_queue_buffer(stream, b);
}

static void on_stream_state_changed(void *_data, enum pw_stream_state old,
				    enum pw_stream_state state, const char *error)
{
	struct data *data = _data;
	fprintf(stderr, "stream state: \"%s\"\n", pw_stream_state_as_string(state));
	switch (state) {
	case PW_STREAM_STATE_UNCONNECTED:
		pw_main_loop_quit(data->loop);
		break;
	case PW_STREAM_STATE_CONFIGURE:
		pw_stream_set_active(data->stream, true);
		break;
	default:
		break;
	}
}

static void
on_stream_format_changed(void *_data, const struct spa_pod *format)
{
	struct data *data = _data;
	struct pw_stream *stream = data->stream;
	uint8_t params_buffer[1024];
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(params_buffer, sizeof(params_buffer));
	const struct spa_pod *params[5];
	Uint32 sdl_format;
	void *d;

	if (format == NULL) {
		pw_stream_finish_format(stream, 0, NULL, 0);
		return;
	}

	fprintf(stderr, "got format:\n");
	spa_debug_format(2, NULL, format);

	spa_format_video_raw_parse(format, &data->format);

	sdl_format = id_to_sdl_format(data->format.format);
	if (sdl_format == SDL_PIXELFORMAT_UNKNOWN) {
		pw_stream_finish_format(stream, -EINVAL, NULL, 0);
		return;
	}

	data->texture = SDL_CreateTexture(data->renderer,
					  sdl_format,
					  SDL_TEXTUREACCESS_STREAMING,
					  data->format.size.width,
					  data->format.size.height);
	SDL_LockTexture(data->texture, NULL, &d, &data->stride);
	SDL_UnlockTexture(data->texture);

	data->rect.x = 0;
	data->rect.y = 0;
	data->rect.w = data->format.size.width;
	data->rect.h = data->format.size.height;

	params[0] = spa_pod_builder_add_object(&b,
		SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
		SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(8, 2, MAX_BUFFERS),
		SPA_PARAM_BUFFERS_blocks,  SPA_POD_Int(1),
		SPA_PARAM_BUFFERS_size,    SPA_POD_Int(data->stride * data->format.size.height),
		SPA_PARAM_BUFFERS_stride,  SPA_POD_Int(data->stride),
		SPA_PARAM_BUFFERS_align,   SPA_POD_Int(16));

	params[1] = spa_pod_builder_add_object(&b,
		SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
		SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Header),
		SPA_PARAM_META_size, SPA_POD_Int(sizeof(struct spa_meta_header)));
	params[2] = spa_pod_builder_add_object(&b,
		SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
		SPA_PARAM_META_type, SPA_POD_Id(SPA_META_VideoCrop),
		SPA_PARAM_META_size, SPA_POD_Int(sizeof(struct spa_meta_region)));
#define CURSOR_META_SIZE(w,h)	(sizeof(struct spa_meta_cursor) + \
				 sizeof(struct spa_meta_bitmap) + w * h * 4)
	params[3] = spa_pod_builder_add_object(&b,
		SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
		SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Cursor),
		SPA_PARAM_META_size, SPA_POD_CHOICE_RANGE_Int(
				CURSOR_META_SIZE(64,64),
				CURSOR_META_SIZE(1,1),
				CURSOR_META_SIZE(256,256)));

	pw_stream_finish_format(stream, 0, params, 4);
}

static const struct pw_stream_events stream_events = {
	PW_VERSION_STREAM_EVENTS,
	.state_changed = on_stream_state_changed,
	.format_changed = on_stream_format_changed,
	.process = on_process,
};

static int build_format(struct data *data, struct spa_pod_builder *b, const struct spa_pod **params)
{
	SDL_RendererInfo info;

	SDL_GetRendererInfo(data->renderer, &info);
	params[0] = sdl_build_formats(&info, b);

	fprintf(stderr, "supported formats:\n");
	spa_debug_format(2, NULL, params[0]);

	return 0;
}

int main(int argc, char *argv[])
{
	struct data data = { 0, };
	const struct spa_pod *params[1];
	uint8_t buffer[1024];
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

	pw_init(&argc, &argv);

	data.loop = pw_main_loop_new(NULL);

	data.stream = pw_stream_new_simple(
			pw_main_loop_get_loop(data.loop),
			"video-play",
			pw_properties_new(
				PW_NODE_PROP_MEDIA, "Video",
				PW_NODE_PROP_CATEGORY, "Capture",
				PW_NODE_PROP_ROLE, "Camera",
				NULL),
			&stream_events,
			&data);

	data.remote = pw_stream_get_remote(data.stream);
	data.core = pw_remote_get_core(data.remote);
	data.path = argc > 1 ? argv[1] : NULL;

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "can't initialize SDL: %s\n", SDL_GetError());
		return -1;
	}

	if (SDL_CreateWindowAndRenderer
	    (WIDTH, HEIGHT, SDL_WINDOW_RESIZABLE, &data.window, &data.renderer)) {
		fprintf(stderr, "can't create window: %s\n", SDL_GetError());
		return -1;
	}

	build_format(&data, &b, params);

	pw_stream_connect(data.stream,
			  PW_DIRECTION_INPUT,
			  data.path ? (uint32_t)atoi(data.path) : SPA_ID_INVALID,
			  PW_STREAM_FLAG_AUTOCONNECT |
			  PW_STREAM_FLAG_INACTIVE |
			  PW_STREAM_FLAG_EXCLUSIVE |
			  PW_STREAM_FLAG_MAP_BUFFERS,
			  params, 1);

	pw_main_loop_run(data.loop);

	pw_stream_destroy(data.stream);
	pw_main_loop_destroy(data.loop);

	SDL_DestroyTexture(data.texture);
	if (data.cursor)
		SDL_DestroyTexture(data.cursor);
	SDL_DestroyRenderer(data.renderer);
	SDL_DestroyWindow(data.window);

	return 0;
}
