/* Spa A2DP LHDC codec */
/* SPDX-FileCopyrightText: Copyright © 2020 Wim Taymans */
/* SPDX-FileCopyrightText: Copyright © 2024 anonymix007 */
/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <unistd.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#include <arpa/inet.h>

#include <spa/utils/string.h>
#include <spa/utils/dict.h>
#include <spa/pod/parser.h>
#include <spa/param/props.h>
#include <spa/param/audio/format.h>

#include <lhdcBT.h>
#include <lhdcBT_dec.h>

#include "rtp.h"
#include "media-codecs.h"

static struct spa_log *log;

struct props {
	int eqmid;
};

struct rtp_lhdc_payload {
	uint8_t seq_num;
	uint8_t latency:2;
	uint8_t frame_count:6;
};

static_assert(sizeof(struct rtp_lhdc_payload) == sizeof(uint16_t), "LHDC payload header must be 2 bytes");

struct impl {
	HANDLE_LHDC_BT lhdc;

	bool dec_initialized;

	struct rtp_header *header;
	struct rtp_lhdc_payload *payload;

	int mtu;
	int eqmid;
	int frequency;
	int bit_depth;
	int codesize;
	int block_size;
	int frame_length;
	int frame_count;
	uint8_t seq_num;
	int32_t buf[2][LHDCV2_BT_ENC_BLOCK_SIZE];
};

static int codec_fill_caps(const struct media_codec *codec, uint32_t flags, uint8_t caps[A2DP_MAX_CAPS_SIZE])
{
	static const a2dp_lhdc_v3_t a2dp_lhdc = {
		.info.vendor_id = LHDC_V3_VENDOR_ID,
		.info.codec_id = LHDC_V3_CODEC_ID,
		.frequency = LHDC_SAMPLING_FREQ_44100 | LHDC_SAMPLING_FREQ_48000 | LHDC_SAMPLING_FREQ_96000,
		.bit_depth = LHDC_BIT_DEPTH_16 | LHDC_BIT_DEPTH_24,
		.jas = 0,
		.ar = 0,
		.version = LHDC_VER3,
		.max_bit_rate = LHDC_MAX_BIT_RATE_900K,
		.low_latency = 0,
		.llac = 0,
		.ch_split_mode = LHDC_CH_SPLIT_MODE_NONE,
		.meta = 0,
		.min_bitrate = 0,
		.larc = 0,
		.lhdc_v4 = 1,
	};

	memcpy(caps, &a2dp_lhdc, sizeof(a2dp_lhdc));
	return sizeof(a2dp_lhdc);
}

static const struct media_codec_config
lhdc_frequencies[] = {
	{ LHDC_SAMPLING_FREQ_44100, 44100, 0 },
	{ LHDC_SAMPLING_FREQ_48000, 48000, 2 },
	{ LHDC_SAMPLING_FREQ_96000, 96000, 1 },
};

static int codec_select_config(const struct media_codec *codec, uint32_t flags,
			       const void *caps, size_t caps_size,
			       const struct media_codec_audio_info *info,
			       const struct spa_dict *settings, uint8_t config[A2DP_MAX_CAPS_SIZE])
{
	a2dp_lhdc_v3_t conf;
	int i;

	if (caps_size < sizeof(conf))
		return -EINVAL;

	memcpy(&conf, caps, sizeof(conf));

	if (codec->vendor.vendor_id != conf.info.vendor_id ||
		codec->vendor.codec_id != conf.info.codec_id)
		return -ENOTSUP;

	if ((i = media_codec_select_config(lhdc_frequencies,
		SPA_N_ELEMENTS(lhdc_frequencies),
					   conf.frequency,
					   info ? info->rate : A2DP_CODEC_DEFAULT_RATE
	)) < 0)
		return -ENOTSUP;
		conf.frequency = lhdc_frequencies[i].config;

		conf.low_latency = 0;
		conf.llac = 0;
		conf.lhdc_v4 = 1;
		conf.bit_depth = LHDC_BIT_DEPTH_24;

		memcpy(config, &conf, sizeof(conf));

		return sizeof(conf);
}

static int codec_enum_config(const struct media_codec *codec, uint32_t flags,
			     const void *caps, size_t caps_size, uint32_t id, uint32_t idx,
			     struct spa_pod_builder *b, struct spa_pod **param)
{
	a2dp_lhdc_v3_t conf;
	struct spa_pod_frame f[2];
	struct spa_pod_choice *choice;
	uint32_t i = 0;
	uint32_t position[SPA_AUDIO_MAX_CHANNELS];

	if (caps_size < sizeof(conf))
		return -EINVAL;

	memcpy(&conf, caps, sizeof(conf));

	if (idx > 0)
		return 0;

	spa_pod_builder_push_object(b, &f[0], SPA_TYPE_OBJECT_Format, id);
	spa_pod_builder_add(b,
			    SPA_FORMAT_mediaType,	  SPA_POD_Id(SPA_MEDIA_TYPE_audio),
			    SPA_FORMAT_mediaSubtype,   SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
			    SPA_FORMAT_AUDIO_format,   SPA_POD_Id(SPA_AUDIO_FORMAT_S32),
			    0);
	spa_pod_builder_prop(b, SPA_FORMAT_AUDIO_rate, 0);

	spa_pod_builder_push_choice(b, &f[1], SPA_CHOICE_None, 0);
	choice = (struct spa_pod_choice*)spa_pod_builder_frame(b, &f[1]);
	i = 0;
	if (conf.frequency & LHDC_SAMPLING_FREQ_48000) {
		if (i++ == 0)
			spa_pod_builder_int(b, 48000);
		spa_pod_builder_int(b, 48000);
	}
	if (conf.frequency & LHDC_SAMPLING_FREQ_44100) {
		if (i++ == 0)
			spa_pod_builder_int(b, 44100);
		spa_pod_builder_int(b, 44100);
	}
	if (conf.frequency & LHDC_SAMPLING_FREQ_96000) {
		if (i++ == 0)
			spa_pod_builder_int(b, 96000);
		spa_pod_builder_int(b, 96000);
	}
	if (i > 1)
		choice->body.type = SPA_CHOICE_Enum;
	spa_pod_builder_pop(b, &f[1]);

	if (i == 0)
		return -EINVAL;


	position[0] = SPA_AUDIO_CHANNEL_FL;
	position[1] = SPA_AUDIO_CHANNEL_FR;
	spa_pod_builder_add(b,
			    SPA_FORMAT_AUDIO_channels, SPA_POD_Int(2),
			    SPA_FORMAT_AUDIO_position, SPA_POD_Array(sizeof(uint32_t),
								     SPA_TYPE_Id, 2, position),
		     0);
	*param = spa_pod_builder_pop(b, &f[0]);
	return *param == NULL ? -EIO : 1;
}

static int codec_get_block_size(void *data)
{
	struct impl *this = data;
	return this->codesize;
}

static const struct { const char *name; int v; } eqmids[] = {
	{ "low0", .v = LHDCBT_QUALITY_LOW0 },
	{ "low1", .v = LHDCBT_QUALITY_LOW1 },
	{ "low2", .v = LHDCBT_QUALITY_LOW2 },
	{ "low3", .v = LHDCBT_QUALITY_LOW3 },
	{ "low4", .v = LHDCBT_QUALITY_LOW4 },
	{ "low",  .v = LHDCBT_QUALITY_LOW  },
	{ "mid",  .v = LHDCBT_QUALITY_MID  },
	{ "high", .v = LHDCBT_QUALITY_HIGH },
	{ "auto", .v = LHDCBT_QUALITY_AUTO },
	{ 0 },
};

static int string_to_eqmid(const char * eqmid)
{
	for (size_t i = 0; eqmids[i].name; i++) {
		if (spa_streq(eqmids[i].name, eqmid))
			return eqmids[i].v;
	}
	return LHDCBT_QUALITY_AUTO;
}

static void *codec_init_props(const struct media_codec *codec, uint32_t flags, const struct spa_dict *settings)
{
	struct props *p = calloc(1, sizeof(struct props));
	const char *str;

	if (p == NULL)
		return NULL;

	if (settings == NULL || (str = spa_dict_lookup(settings, "bluez5.a2dp.lhdc.quality")) == NULL)
		str = "auto";

	p->eqmid = string_to_eqmid(str);
	return p;
}

static void codec_clear_props(void *props)
{
	free(props);
}

static int codec_enum_props(void *props, const struct spa_dict *settings, uint32_t id, uint32_t idx,
			    struct spa_pod_builder *b, struct spa_pod **param)
{
	struct props *p = props;
	struct spa_pod_frame f[2];
	switch (id) {
		case SPA_PARAM_PropInfo:
		{
			switch (idx) {
				case 0:
					spa_pod_builder_push_object(b, &f[0], SPA_TYPE_OBJECT_PropInfo, id);
					spa_pod_builder_prop(b, SPA_PROP_INFO_id, 0);
					spa_pod_builder_id(b, SPA_PROP_quality);
					spa_pod_builder_prop(b, SPA_PROP_INFO_description, 0);
					spa_pod_builder_string(b, "LHDC quality");

					spa_pod_builder_prop(b, SPA_PROP_INFO_type, 0);
					spa_pod_builder_push_choice(b, &f[1], SPA_CHOICE_Enum, 0);
					spa_pod_builder_int(b, p->eqmid);
					for (size_t i = 0; eqmids[i].name; i++) {
						spa_pod_builder_int(b, eqmids[i].v);
					}
					spa_pod_builder_pop(b, &f[1]);

					spa_pod_builder_prop(b, SPA_PROP_INFO_labels, 0);
					spa_pod_builder_push_struct(b, &f[1]);
					for (size_t i = 0; eqmids[i].name; i++) {
						spa_pod_builder_int(b, eqmids[i].v);
						spa_pod_builder_string(b, eqmids[i].name);
					}
					spa_pod_builder_pop(b, &f[1]);

					*param = spa_pod_builder_pop(b, &f[0]);
					break;
				default:
					return 0;
			}
			break;
		}
				case SPA_PARAM_Props:
				{
					switch (idx) {
						case 0:
							*param = spa_pod_builder_add_object(b,
											    SPA_TYPE_OBJECT_Props, id,
					   SPA_PROP_quality, SPA_POD_Int(p->eqmid));
							break;
						default:
							return 0;
					}
					break;
				}
						default:
							return -ENOENT;
	}
	return 1;
}

static int codec_set_props(void *props, const struct spa_pod *param)
{
	struct props *p = props;
	const int prev_eqmid = p->eqmid;
	if (param == NULL) {
		p->eqmid = LHDCBT_QUALITY_AUTO;
	} else {
		spa_pod_parse_object(param,
				     SPA_TYPE_OBJECT_Props, NULL,
		       SPA_PROP_quality, SPA_POD_OPT_Int(&p->eqmid));
		if (p->eqmid > LHDCBT_QUALITY_AUTO || p->eqmid < LHDCBT_QUALITY_LOW0)
			p->eqmid = prev_eqmid;
	}

	return prev_eqmid != p->eqmid;
}

static LHDC_VERSION_SETUP get_version(const a2dp_lhdc_v3_t *configuration) {
	if (configuration->llac) {
		return LLAC;
	} else if (configuration->lhdc_v4) {
		return LHDC_V4;
	} else {
		return LHDC_V3;
	}
}

static int get_version_setup(const a2dp_lhdc_v3_t *configuration) {
	if (configuration->llac) {
		return VERSION_LLAC;
	} else if (configuration->lhdc_v4) {
		return VERSION_4;
	} else {
		return VERSION_3;
	}
}

static int get_encoder_interval(const a2dp_lhdc_v3_t *configuration) {
	if (configuration->low_latency) {
		return 10;
	} else {
		return 20;
	}
}

static int get_bit_depth(const a2dp_lhdc_v3_t *configuration) {
	if (configuration->bit_depth == LHDC_BIT_DEPTH_16) {
		return 16;
	} else {
		return 24;
	}
}

static LHDCBT_QUALITY_T get_max_bitrate(const a2dp_lhdc_v3_t *configuration) {
	if (configuration->max_bit_rate == LHDC_MAX_BIT_RATE_400K) {
		return LHDCBT_QUALITY_LOW;
	} else if (configuration->max_bit_rate == LHDC_MAX_BIT_RATE_500K) {
		return LHDCBT_QUALITY_MID;
	} else {
		return LHDCBT_QUALITY_HIGH;
	}
}

static void *codec_init(const struct media_codec *codec, uint32_t flags,
			void *config, size_t config_len, const struct spa_audio_info *info,
			void *props, size_t mtu)
{
	struct impl *this;
	a2dp_lhdc_v3_t *conf = config;
	int res;
	struct props *p = props;

	this = calloc(1, sizeof(struct impl));
	if (this == NULL)
		goto error_errno;

	this->lhdc = lhdcBT_get_handle(get_version(conf));
	if (this->lhdc == NULL)
		goto error_errno;

	if (p == NULL) {
		this->eqmid = LHDCBT_QUALITY_AUTO;
	} else {
		this->eqmid = p->eqmid;
	}

	this->mtu = mtu;
	this->frequency = info->info.raw.rate;
	this->bit_depth = get_bit_depth(conf);

	lhdcBT_set_hasMinBitrateLimit(this->lhdc, conf->min_bitrate);
	lhdcBT_set_max_bitrate(this->lhdc, get_max_bitrate(conf));

	res = lhdcBT_init_encoder(this->lhdc,
				  this->frequency,
			   this->bit_depth,
			   this->eqmid,
			   conf->ch_split_mode > LHDC_CH_SPLIT_MODE_NONE,
			   0,
			   this->mtu,
			   get_encoder_interval(conf));
	if (res < 0)
		goto error;

	tLHDCV3_DEC_CONFIG dec_config = {
		.version = get_version_setup(conf),
		.sample_rate = this->frequency,
		.bits_depth = this->bit_depth,
	};

	this->dec_initialized = false;

	if (lhdcBT_dec_init_decoder(&dec_config) < 0)
		goto error;

	this->dec_initialized = true;

	this->block_size = lhdcBT_get_block_Size(this->lhdc);
	this->codesize = info->info.raw.channels * lhdcBT_get_block_Size(this->lhdc);

	switch (info->info.raw.format) {
		case SPA_AUDIO_FORMAT_S32:
			this->codesize *= 4;
			break;
		default:
			res = -EINVAL;
			goto error;
	}

	return this;

	error_errno:
	res = -errno;
	error:
	if (this && this->lhdc)
		lhdcBT_free_handle(this->lhdc);
	free(this);
	errno = -res;
	return NULL;
}

static void codec_deinit(void *data)
{
	struct impl *this = data;
	if (this->lhdc)
		lhdcBT_free_handle(this->lhdc);
	if (this->dec_initialized)
		lhdcBT_dec_deinit_decoder();
	free(this);
}

static int codec_update_props(void *data, void *props)
{
	struct impl *this = data;
	struct props *p = props;
	int res;

	if (p == NULL)
		return 0;

	this->eqmid = p->eqmid;

	if ((res = lhdcBT_set_bitrate(this->lhdc, this->eqmid)) < 0)
		goto error;
	return 0;
	error:
	return res;
}

static int codec_abr_process(void *data, size_t unsent)
{
	struct impl *this = data;
	return this->eqmid == LHDCBT_QUALITY_AUTO ? lhdcBT_adjust_bitrate(this->lhdc, unsent / this->mtu) : -ENOTSUP;
}

static int codec_start_encode (void *data,
			       void *dst, size_t dst_size, uint16_t seqnum, uint32_t timestamp)
{
	struct impl *this = data;

	this->header = (struct rtp_header *)dst;
	this->payload = SPA_PTROFF(dst, sizeof(struct rtp_header), struct rtp_lhdc_payload);
	memset(this->header, 0, sizeof(struct rtp_header)+sizeof(struct rtp_lhdc_payload));

	this->payload->frame_count = 0;
	this->header->v = 2;
	this->header->pt = 96;
	this->header->sequence_number = htons(seqnum);
	this->header->timestamp = htonl(timestamp);
	this->header->ssrc = htonl(1);
	return sizeof(struct rtp_header) + sizeof(struct rtp_lhdc_payload);
}



static void deinterleave_32_c2(int32_t * SPA_RESTRICT * SPA_RESTRICT dst, const int32_t * SPA_RESTRICT src, size_t n_samples)
{
	/* We'll trust the compiler to optimize this */
	const size_t n_channels = 2;
	size_t i, j;
	for (j = 0; j < n_samples; ++j)
		for (i = 0; i < n_channels; ++i)
			dst[i][j] = *src++;
}

static int codec_encode(void *data,
			const void *src, size_t src_size,
			void *dst, size_t dst_size,
			size_t *dst_out, int *need_flush)
{
	struct impl *this = data;
	int res, src_used;
	uint32_t dst_used, frame_num = 0;
	int32_t *inputs[2] = { this->buf[0], inputs[1] = this->buf[1] };

	src_used = this->codesize;
	dst_used = dst_size;

	deinterleave_32_c2(inputs, src, this->block_size);

	res = lhdcBT_encode_stereo(this->lhdc, inputs[0], inputs[1], dst, &dst_used, &frame_num);
	if (SPA_UNLIKELY(res < 0))
		return -EINVAL;

	*dst_out = dst_used;

	this->payload->frame_count += frame_num;

	*need_flush = (this->payload->frame_count > 0) ? NEED_FLUSH_ALL : NEED_FLUSH_NO;

	if (this->payload->frame_count > 0)
		this->payload->seq_num = this->seq_num++;

	return src_used;
}

static int codec_start_decode (void *data,
			       const void *src, size_t src_size, uint16_t *seqnum, uint32_t *timestamp)
{
	const struct rtp_header *header = src;
	size_t header_size = sizeof(struct rtp_header);

	spa_return_val_if_fail (src_size > header_size, -EINVAL);

	if (seqnum)
		*seqnum = ntohs(header->sequence_number);
	if (timestamp)
		*timestamp = ntohl(header->timestamp);

	return header_size;
}

static const char *dec_errors[] = {
	[-LHDCBT_DEC_FUNC_SUCCEED] = "OK",
	[-LHDCBT_DEC_FUNC_FAIL] = "General error",
	[-LHDCBT_DEC_FUNC_INPUT_NOT_ENOUGH] = "Not enough input data",
	[-LHDCBT_DEC_FUNC_OUTPUT_NOT_ENOUGH] = "Not enough output space",
	[-LHDCBT_DEC_FUNC_INVALID_SEQ_NO] = "Invalid sequence number",
};

static int codec_decode (void *data,
			 const void *src, size_t src_size,
			 void *dst, size_t dst_size,
			 size_t *dst_out)
{
	uint32_t decoded = dst_size;
	uint32_t consumed = 0;

	int err = 0;

	if ((err = lhdcBT_dec_check_frame_data_enough(src, src_size, &consumed)) < 0)
		goto error;

	consumed += sizeof(struct rtp_lhdc_payload);

	if ((err = lhdcBT_dec_decode(src, consumed, dst, &decoded, 24)) < 0)
		goto error;

	int32_t *samples = dst;
	for (size_t i = 0; i < decoded / 4; i++)
		samples[i] *= (1 << 8);

	if (dst_out)
		*dst_out = decoded;

	return consumed;

	error:
	spa_log_error(log, "lhdcBT_dec_decode: %s (%d)!", dec_errors[-err], err);
	return -1;
}

static int codec_reduce_bitpool(void *data)
{
	return -ENOTSUP;
}

static int codec_increase_bitpool(void *data)
{
	return -ENOTSUP;
}

static void codec_set_log(struct spa_log *global_log)
{
	log = global_log;
	spa_log_topic_init(log, &codec_plugin_log_topic);
}

const struct media_codec a2dp_codec_lhdc = {
	.id = SPA_BLUETOOTH_AUDIO_CODEC_LHDC_V3,
	.codec_id = A2DP_CODEC_VENDOR,
	.vendor = { .vendor_id = LHDC_V3_VENDOR_ID,
		.codec_id = LHDC_V3_CODEC_ID },
		.name = "lhdc_v3",
		.description = "LHDC V3",
		.fill_caps = codec_fill_caps,
		.select_config = codec_select_config,
		.enum_config = codec_enum_config,
		.init_props = codec_init_props,
		.enum_props = codec_enum_props,
		.set_props = codec_set_props,
		.clear_props = codec_clear_props,
		.init = codec_init,
		.deinit = codec_deinit,
		.update_props = codec_update_props,
		.get_block_size = codec_get_block_size,
		.abr_process = codec_abr_process,
		.start_encode = codec_start_encode,
		.encode = codec_encode,
		.start_decode = codec_start_decode,
		.decode = codec_decode,
		.reduce_bitpool = codec_reduce_bitpool,
		.increase_bitpool = codec_increase_bitpool,
		.set_log = codec_set_log,
};

MEDIA_CODEC_EXPORT_DEF(
	"lhdc",
	&a2dp_codec_lhdc
);
