/* Simple Plugin API
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

#ifndef __SPA_PARAM_AUDIO_FORMAT_UTILS_H__
#define __SPA_PARAM_AUDIO_FORMAT_UTILS_H__

#ifdef __cplusplus
extern "C" {
#endif


#include <spa/pod/parser.h>
#include <spa/pod/builder.h>
#include <spa/param/audio/format.h>
#include <spa/param/format-utils.h>

static inline int
spa_format_audio_raw_parse(const struct spa_pod *format, struct spa_audio_info_raw *info)
{
	struct spa_pod *position = NULL;
	int res;
	res = spa_pod_object_parse(format,
		":", SPA_FORMAT_AUDIO_format,		"I", &info->format,
		":", SPA_FORMAT_AUDIO_rate,		"i", &info->rate,
		":", SPA_FORMAT_AUDIO_channels,		"i", &info->channels,
		":", SPA_FORMAT_AUDIO_flags,		"?i", &info->flags,
		":", SPA_FORMAT_AUDIO_position,		"?P", &position, NULL);
	if (position && position->type == SPA_TYPE_Array &&
			SPA_POD_ARRAY_TYPE(position) == SPA_TYPE_Id) {
		uint32_t *values = (uint32_t*)SPA_POD_ARRAY_VALUES(position);
		uint32_t n_values = SPA_MIN(SPA_POD_ARRAY_N_VALUES(position), SPA_AUDIO_MAX_CHANNELS);
		memcpy(info->position, values, n_values * sizeof(uint32_t));
	}
	else
		SPA_FLAG_SET(info->flags, SPA_AUDIO_FLAG_UNPOSITIONED);

	return res;
}

static inline struct spa_pod *
spa_format_audio_raw_build(struct spa_pod_builder *builder, uint32_t id, struct spa_audio_info_raw *info)
{
	const struct spa_pod_id media_type = SPA_POD_Id(SPA_MEDIA_TYPE_audio);
	const struct spa_pod_id media_subtype = SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw);
	const struct spa_pod_id format = SPA_POD_Id(info->format);
	const struct spa_pod_int rate = SPA_POD_Int(info->rate);
	const struct spa_pod_int channels = SPA_POD_Int(info->channels);

	spa_pod_builder_push_object(builder, SPA_TYPE_OBJECT_Format, id);
	spa_pod_builder_props(builder,
			SPA_FORMAT_mediaType,		&media_type,
			SPA_FORMAT_mediaSubtype,	&media_subtype,
			SPA_FORMAT_AUDIO_format,	&format,
			SPA_FORMAT_AUDIO_rate,		&rate,
			SPA_FORMAT_AUDIO_channels,	&channels,
			0);

	if (!SPA_FLAG_CHECK(info->flags, SPA_AUDIO_FLAG_UNPOSITIONED)) {
		spa_pod_builder_prop(builder, SPA_FORMAT_AUDIO_position, 0);
		spa_pod_builder_array(builder, sizeof(uint32_t), SPA_TYPE_Id,
				info->channels, info->position);
	}

	return (struct spa_pod*)spa_pod_builder_pop(builder);
}


#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* __SPA_PARAM_AUDIO_FORMAT_UTILS */
