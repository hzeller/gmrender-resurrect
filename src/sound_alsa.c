/* output_alsa.c - Sound module for alsa
 *
 * Copyright (C) 2014-2019   Mar Chalain
 *
 * This file is part of GMediaRender.
 *
 * GMediaRender is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GMediaRender is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GMediaRender; if not, write to the Free Software 
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, 
 * MA 02110-1301, USA.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <errno.h>

#include <pthread.h>
#include <alsa/asoundlib.h>

#include <mpg123.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "logging.h"
#include "sound_module.h"

struct sound_alsa_global_s
{
        const char *cmd_card;
        snd_pcm_t *pcm;
        int samplesize;
        struct fifo_s *fifo;
};
struct sound_alsa_global_s g = {
        .cmd_card = "default",
        .pcm = NULL,
        .fifo = NULL
};

static int
sound_alsa_open(int channels, int encoding, unsigned int rate)
{
	int ret;

	ret = snd_pcm_open(&g.pcm, g.cmd_card, SND_PCM_STREAM_PLAYBACK, 0);

	if (ret == -1 || g.pcm == NULL)
		return -1;

	snd_pcm_hw_params_t *hw_params;
	ret = snd_pcm_hw_params_malloc(&hw_params);
	ret = snd_pcm_hw_params_any(g.pcm, hw_params);
	ret = snd_pcm_hw_params_set_access(g.pcm, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_format_t pcm_format;
	switch (encoding)
	{
		case MPG123_ENC_SIGNED_32:
			pcm_format = SND_PCM_FORMAT_S32_LE;
			g.samplesize = 4 * 2;
		break;
		case MPG123_ENC_SIGNED_16:
		default:
			pcm_format = SND_PCM_FORMAT_S16_LE;
			g.samplesize = 2 * 2;
		break;
	}
	ret = snd_pcm_hw_params_set_format(g.pcm, hw_params, pcm_format);
	ret = snd_pcm_hw_params_set_rate_near(g.pcm, hw_params, &rate, NULL);
	ret = snd_pcm_hw_params_set_channels(g.pcm, hw_params, channels);

	ret = snd_pcm_hw_params(g.pcm, hw_params);
	ret = snd_pcm_prepare(g.pcm);

	return 0;
}

static ssize_t
sound_alsa_write(unsigned char *buffer, ssize_t size)
{
	snd_pcm_sframes_t ret;
	ret = snd_pcm_writei(g.pcm, buffer, size / g.samplesize);
	if (ret == -EPIPE)
		ret = snd_pcm_recover(g.pcm, ret, 0);
	return ret * g.samplesize;
}

static int
sound_alsa_close(void)
{
	return snd_pcm_close(g.pcm);
}

struct sound_module const *g_sound_alsa = &(struct sound_module)
{
	.name = "alsa",
	.open = sound_alsa_open,
	.write = sound_alsa_write,
	.close = sound_alsa_close,
	.get_volume  = NULL,
	.set_volume  = NULL,
	.get_mute  = NULL,
	.set_mute  = NULL,
};
