/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// PulseAudio sound backend -- replaces the original OSS /dev/dsp implementation
// Uses libpulse-simple which works through PipeWire's PulseAudio emulation

#include <pulse/simple.h>
#include <pulse/error.h>
#include <stdlib.h>

#include "../game/q_shared.h"
#include "../client/snd_local.h"

static pa_simple *pa_handle;
static int snd_inited;
static int snd_sent;  // mono samples submitted to PulseAudio
static int snd_start_ms;  // Com_Milliseconds at sound start

static cvar_t *sndbits;
static cvar_t *sndspeed;
static cvar_t *sndchannels;

#define PA_BUFFER_SAMPLES 16384  // must be power of 2

void Snd_Memset(void *dest, const int val, const size_t count)
{
	Com_Memset(dest, val, count);
}

qboolean SNDDMA_Init(void)
{
	pa_sample_spec ss;
	pa_buffer_attr ba;
	int error;

	if (snd_inited)
		return qtrue;

	sndbits = Cvar_Get("sndbits", "16", CVAR_ARCHIVE);
	sndspeed = Cvar_Get("sndspeed", "44100", CVAR_ARCHIVE);
	sndchannels = Cvar_Get("sndchannels", "2", CVAR_ARCHIVE);

	dma.samplebits = (int)sndbits->value;
	if (dma.samplebits != 8)
		dma.samplebits = 16;

	dma.speed = (int)sndspeed->value;
	if (dma.speed <= 0)
		dma.speed = 44100;

	dma.channels = (int)sndchannels->value;
	if (dma.channels < 1 || dma.channels > 2)
		dma.channels = 2;

	ss.format = (dma.samplebits == 16) ? PA_SAMPLE_S16LE : PA_SAMPLE_U8;
	ss.rate = dma.speed;
	ss.channels = dma.channels;

	// ~100ms target latency, enough to absorb frame timing jitter
	ba.maxlength = (uint32_t)-1;
	ba.tlength = dma.speed * dma.channels * (dma.samplebits / 8) / 10;
	ba.prebuf = (uint32_t)-1;
	ba.minreq = (uint32_t)-1;
	ba.fragsize = (uint32_t)-1;

	pa_handle = pa_simple_new(NULL, "quake3", PA_STREAM_PLAYBACK,
		NULL, "game", &ss, NULL, &ba, &error);
	if (!pa_handle) {
		Com_Printf("PulseAudio: Could not connect: %s\n", pa_strerror(error));
		return qfalse;
	}

	dma.samples = PA_BUFFER_SAMPLES;
	dma.submission_chunk = 1;
	dma.buffer = calloc(1, dma.samples * (dma.samplebits / 8));
	if (!dma.buffer) {
		Com_Printf("PulseAudio: Could not allocate DMA buffer\n");
		pa_simple_free(pa_handle);
		pa_handle = NULL;
		return qfalse;
	}

	snd_sent = 0;
	snd_start_ms = 0;
	snd_inited = 1;

	Com_Printf("PulseAudio: %d Hz, %d-bit, %s\n", dma.speed, dma.samplebits,
		dma.channels == 2 ? "stereo" : "mono");

	return qtrue;
}

int SNDDMA_GetDMAPos(void)
{
	int elapsed;

	if (!snd_inited)
		return 0;

	if (!snd_start_ms)
		snd_start_ms = Com_Milliseconds();

	elapsed = Com_Milliseconds() - snd_start_ms;
	return ((elapsed * dma.speed / 1000) * dma.channels) % dma.samples;
}

void SNDDMA_Shutdown(void)
{
	if (pa_handle) {
		pa_simple_drain(pa_handle, NULL);
		pa_simple_free(pa_handle);
		pa_handle = NULL;
	}
	if (dma.buffer) {
		free(dma.buffer);
		dma.buffer = NULL;
	}
	snd_inited = 0;
}

void SNDDMA_Submit(void)
{
	int samples_to_write, offset, chunk, bytes_per_sample;
	int error;
	extern int s_paintedtime;

	if (!snd_inited || !pa_handle)
		return;

	samples_to_write = s_paintedtime * dma.channels - snd_sent;
	if (samples_to_write <= 0)
		return;
	// cap to ~20ms per submit to avoid blocking the game loop too long
	{
		int max_samples = dma.speed * dma.channels / 50;
		if (samples_to_write > max_samples)
			samples_to_write = max_samples;
	}

	bytes_per_sample = dma.samplebits / 8;

	while (samples_to_write > 0) {
		offset = (snd_sent % dma.samples) * bytes_per_sample;

		// contiguous chunk until buffer wraps
		chunk = dma.samples - (snd_sent % dma.samples);
		if (chunk > samples_to_write)
			chunk = samples_to_write;
		if (chunk <= 0)
			break;

		if (pa_simple_write(pa_handle, dma.buffer + offset,
				chunk * bytes_per_sample, &error) < 0) {
			Com_Printf("PulseAudio write error: %s\n", pa_strerror(error));
			break;
		}

		snd_sent += chunk;
		samples_to_write -= chunk;
	}
}

void SNDDMA_BeginPainting(void)
{
}
