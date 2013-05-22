/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>

#include <xmp.h>

#include "stream/stream.h"
#include "demux.h"
#include "stheader.h"
#include "audio/format.h"

#define SAMPLERATE 48000


static demuxer_t* demux_libxmp_open(demuxer_t* demuxer) {
    sh_audio_t* sh_audio;
    WAVEFORMATEX* w;

    if (demuxer->stream->type != STREAMTYPE_FILE || !demuxer->stream->url)
        return NULL;

    xmp_context c = xmp_create_context();

    if (xmp_load_module(c, demuxer->stream->url) != 0) {
        xmp_free_context(c);
        return NULL;
    }

    sh_audio = new_sh_audio(demuxer,0);
    sh_audio->gsh->codec = "mp-pcm";
    sh_audio->format = AF_FORMAT_S16_NE;
    sh_audio->wf = w = calloc(1, sizeof(*w));
    w->nChannels = 2;
    w->nSamplesPerSec = SAMPLERATE;
    w->nAvgBytesPerSec = SAMPLERATE * 2 * 2;
    w->nBlockAlign = 2 * 2;
    w->wBitsPerSample = 8 * 2;

    if (xmp_start_player(c, SAMPLERATE, 0) != 0) {
        xmp_free_context(c);
        return NULL;
    }

    demuxer->priv = c;

    return demuxer;
}

static int demux_libxmp_fill_buffer(demuxer_t* demuxer, demux_stream_t *ds) {
    xmp_context c = demuxer->priv;

    if (xmp_play_frame(c) != 0)
        return 0;

    struct xmp_frame_info fi;
    xmp_get_frame_info(c, &fi);

    if (fi.loop_count > 0)
        return 0;

    if (fi.buffer_size == 0)
        return 0;

    demux_packet_t *dp = new_demux_packet(fi.buffer_size);
    memcpy(dp->buffer, fi.buffer, fi.buffer_size);

    dp->pts = fi.time / 1000.0;
    dp->pos = 0; // ?

    ds_add_packet(ds, dp);

    return 1;
}

static void demux_libxmp_seek(demuxer_t *demuxer, float rel_seek_secs,
                              float audio_delay, int flags)
{
    xmp_context c = demuxer->priv;
    struct xmp_frame_info fi;
    xmp_get_frame_info(c, &fi);

    double pos = fi.time / 1000.0;
    double len = fi.total_time / 1000.0;

    double base = (flags & SEEK_ABSOLUTE) ? 0 : pos;
    double time;
    if (flags & SEEK_FACTOR) {
        time = base + len * rel_seek_secs;
    } else {
        time = base + rel_seek_secs;
    }

    xmp_seek_time(c, (int)(time * 1000));
}

static int demux_libxmp_control(demuxer_t * demuxer, int cmd, void * arg)
{
    xmp_context c = demuxer->priv;
    struct xmp_frame_info fi;
    xmp_get_frame_info(c, &fi);

    switch (cmd) {
    case DEMUXER_CTRL_GET_TIME_LENGTH:
        *(double *)arg = fi.total_time / 1000.0;
        return DEMUXER_CTRL_OK;
    default:
        return DEMUXER_CTRL_NOTIMPL;
    }
}

static void demux_libxmp_close(struct demuxer *demuxer)
{
    xmp_context c = demuxer->priv;

    if (c) {
        xmp_end_player(c);
        xmp_release_module(c);
        xmp_free_context(c);
    }
}

const demuxer_desc_t demuxer_desc_libxmp = {
  "libxmp demuxer/decoder",
  "libxmp",
  "libxmp",
  "?",
  "",
  DEMUXER_TYPE_GENERIC,
  1,
  NULL,
  demux_libxmp_fill_buffer,
  demux_libxmp_open,
  demux_libxmp_close,
  demux_libxmp_seek,
  demux_libxmp_control,
};
