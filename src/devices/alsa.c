/*
alsa.c - ALSA sound backend
Copyright (C) 2025  David Korenchuk <github.com/epoll-reactor-2>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifdef USE_ALSA

#include "compiler.h"
#include "dlib.h"
#include "sound-hda.h"
#include "utils.h"


#if CHECK_INCLUDE(alsa/asoundlib.h, 1)
#include <alsa/asoundlib.h>
#endif

#define ASOUND_DLIB_SYM(sym) static __typeof__(sym) *MACRO_CONCAT(sym, _dlib) = NULL;

ASOUND_DLIB_SYM(snd_pcm_open)
ASOUND_DLIB_SYM(snd_pcm_hw_params_malloc)
ASOUND_DLIB_SYM(snd_pcm_hw_params_any)
ASOUND_DLIB_SYM(snd_pcm_hw_params_set_access)
ASOUND_DLIB_SYM(snd_pcm_hw_params_set_format)
ASOUND_DLIB_SYM(snd_pcm_hw_params_set_channels)
ASOUND_DLIB_SYM(snd_pcm_hw_params_set_rate_near)
ASOUND_DLIB_SYM(snd_pcm_hw_params_set_period_size_near)
ASOUND_DLIB_SYM(snd_pcm_hw_params_set_buffer_size_near)
ASOUND_DLIB_SYM(snd_pcm_hw_params)
ASOUND_DLIB_SYM(snd_pcm_writei)
ASOUND_DLIB_SYM(snd_pcm_prepare)
ASOUND_DLIB_SYM(snd_pcm_drain)
ASOUND_DLIB_SYM(snd_pcm_close)
ASOUND_DLIB_SYM(snd_pcm_hw_params_free)

#define snd_pcm_open snd_pcm_open_dlib
#define snd_pcm_hw_params_malloc snd_pcm_hw_params_malloc_dlib
#define snd_pcm_hw_params_any snd_pcm_hw_params_any_dlib
#define snd_pcm_hw_params_set_access snd_pcm_hw_params_set_access_dlib
#define snd_pcm_hw_params_set_format snd_pcm_hw_params_set_format_dlib
#define snd_pcm_hw_params_set_channels snd_pcm_hw_params_set_channels_dlib
#define snd_pcm_hw_params_set_rate_near snd_pcm_hw_params_set_rate_near_dlib
#define snd_pcm_hw_params_set_period_size_near snd_pcm_hw_params_set_period_size_near_dlib
#define snd_pcm_hw_params_set_buffer_size_near snd_pcm_hw_params_set_buffer_size_near_dlib
#define snd_pcm_hw_params snd_pcm_hw_params_dlib
#define snd_pcm_writei snd_pcm_writei_dlib
#define snd_pcm_prepare snd_pcm_prepare_dlib
#define snd_pcm_drain snd_pcm_drain_dlib
#define snd_pcm_close snd_pcm_close_dlib
#define snd_pcm_hw_params_free snd_pcm_hw_params_free_dlib

typedef struct {
    snd_pcm_t *pcm_handle;
} alsa_subsystem_t;

static void alsa_sound_write(sound_subsystem_t *subsystem, void *data, size_t size)
{
    alsa_subsystem_t *alsa = subsystem->sound_data;

    // The previous version called snd_pcm_writei exactly once and
    // dropped whatever didn't land: on xrun (-EPIPE) it prepared the
    // PCM but skipped re-writing the chunk; on a positive short return
    // it ignored the tail; on -EINTR it also dropped. Each lost chunk
    // is an audible click/crackle.
    //
    // Loop until every frame is delivered. size is in bytes and we
    // always feed mono 16-bit, so one frame = 2 bytes. Bail on
    // unrecoverable errors after a handful of retries so a disconnected
    // device doesn't spin us forever.
    int16_t *buf = (int16_t*)data;
    snd_pcm_uframes_t remaining = size / 2;
    int xrun_retries = 4;

    while (remaining > 0) {
        snd_pcm_sframes_t n = snd_pcm_writei(alsa->pcm_handle, buf, remaining);
        if (n < 0) {
            if (n == -EAGAIN || n == -EINTR) {
                // Spurious short-wake; try again with the same data.
                continue;
            }
            if (n == -EPIPE || n == -ESTRPIPE) {
                // Xrun / suspend. Reset the PCM and retry the chunk.
                // -ESTRPIPE means suspended — resume would be ideal but
                // prepare is enough to get us back to a writable state.
                if (--xrun_retries < 0) return;
                snd_pcm_prepare(alsa->pcm_handle);
                continue;
            }
            // Unrecoverable (EBADFD, ENODEV, ...) — give up this chunk.
            return;
        }
        buf       += n;     // int16_t* step = one sample per element
        remaining -= n;
    }
}

static bool alsa_load_symbols(void)
{
    dlib_ctx_t *libasound = dlib_open("asound", DLIB_NAME_PROBE);
    if (libasound == NULL) {
        rvvm_warn("Cannot load libasound.so");
        return 0;
    }

    bool avail = 1;

#define ASOUND_DLIB_RESOLVE(lib, sym) \
do { \
    sym = dlib_resolve(lib, #sym); \
    avail = avail && !!sym; \
} while (0)

    ASOUND_DLIB_RESOLVE(libasound, snd_pcm_open);
    ASOUND_DLIB_RESOLVE(libasound, snd_pcm_hw_params_malloc);
    ASOUND_DLIB_RESOLVE(libasound, snd_pcm_hw_params_any);
    ASOUND_DLIB_RESOLVE(libasound, snd_pcm_hw_params_set_access);
    ASOUND_DLIB_RESOLVE(libasound, snd_pcm_hw_params_set_format);
    ASOUND_DLIB_RESOLVE(libasound, snd_pcm_hw_params_set_channels);
    ASOUND_DLIB_RESOLVE(libasound, snd_pcm_hw_params_set_rate_near);
    ASOUND_DLIB_RESOLVE(libasound, snd_pcm_hw_params_set_period_size_near);
    ASOUND_DLIB_RESOLVE(libasound, snd_pcm_hw_params_set_buffer_size_near);
    ASOUND_DLIB_RESOLVE(libasound, snd_pcm_hw_params);
    ASOUND_DLIB_RESOLVE(libasound, snd_pcm_writei);
    ASOUND_DLIB_RESOLVE(libasound, snd_pcm_prepare);
    ASOUND_DLIB_RESOLVE(libasound, snd_pcm_drain);
    ASOUND_DLIB_RESOLVE(libasound, snd_pcm_close);
    ASOUND_DLIB_RESOLVE(libasound, snd_pcm_hw_params_free);

    dlib_close(libasound);

    return avail;
}

bool alsa_sound_init(sound_subsystem_t *sound)
{
    bool libasound_avail = true;
    DO_ONCE(libasound_avail = alsa_load_symbols());
    if (!libasound_avail) {
        rvvm_error("Could not load libasound.so");
        return false;
    }

    alsa_subsystem_t *subsystem = safe_new_obj(alsa_subsystem_t);
    if (snd_pcm_open(&subsystem->pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        rvvm_warn("Failed to open ALSA device");
        return false;
    }

    snd_pcm_t *pcm_device = subsystem->pcm_handle;
    snd_pcm_hw_params_t *params = NULL;
    // Match the HDA codec's advertised format (48 kHz mono 16-bit in
    // sound-hda.c's CODEC_PARAM_SUPP_PCM_SIZE_RATES). Opening the host
    // PCM at 192 kHz while the codec feeds 48 kHz streams plays audio
    // 4× fast → two octaves up.
    unsigned int sample_rate = 48000;
    int channels = 1;
    // Host PCM latency. The guest runs its own ALSA buffer on top of the
    // emulated HDA DMA BDL — that's what user-space apps size via
    // snd_pcm_hw_params on the guest. These numbers only control the
    // host-side PipeWire/ALSA ring the emulator feeds into.
    //
    // 128 frames / 2.67 ms was too tight: a single host scheduling hiccup
    // xruns PipeWire and mpg123's write returns short (3400 of 4608) as
    // back-pressure propagates. 960-frame periods (20 ms) with a 4×
    // buffer (80 ms total) give PipeWire enough slack to ride out jitter
    // without noticeable user-facing latency.
    snd_pcm_uframes_t period_frames = 960;
    snd_pcm_uframes_t buffer_frames = 3840;
    int dir = 0;

    snd_pcm_hw_params_malloc(&params);
    snd_pcm_hw_params_any(pcm_device, params);

    snd_pcm_hw_params_set_access(pcm_device, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm_device, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm_device, params, channels);
    snd_pcm_hw_params_set_rate_near(pcm_device, params, &sample_rate, &dir);
    snd_pcm_hw_params_set_period_size_near(pcm_device, params, &period_frames, &dir);
    snd_pcm_hw_params_set_buffer_size_near(pcm_device, params, &buffer_frames);
    snd_pcm_hw_params(pcm_device, params);

    subsystem->pcm_handle = pcm_device;

    sound->sound_data = subsystem;
    sound->write = alsa_sound_write;

    return true;
}

#endif /* USE_ALSA */
