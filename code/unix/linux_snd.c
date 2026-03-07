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
// SDL2 sound backend for the x86_64 port.
//
// SDL2 calls our audio callback from a dedicated thread, pulling mixed
// samples out of dma.buffer.  The mixer writes into dma.buffer from the
// main thread; the callback reads from it -- the ring-buffer index
// (dma_pos) is the only shared state, updated atomically.
//
// During cinematic playback, the callback bypasses dma.buffer entirely
// and reads directly from s_rawsamples[] -- the ring buffer fed by
// S_RawSamples() from the RoQ decoder.  This avoids the mixer's
// mix-ahead / repaint timing issues that cause audio "blinking."

#include <SDL.h>
#include <string.h>
#include <stdlib.h>

#include "../game/q_shared.h"
#include "../client/snd_local.h"

extern int Sys_Milliseconds(void);

static int	snd_inited;
static int	dma_pos;	// read position in dma.buffer (in samples)

// Cinematic direct audio: bypass DMA mixer, read s_rawsamples directly
static volatile int cin_direct;		// nonzero = cinematic direct mode active
static volatile int cin_readpos;	// read position in s_rawsamples timeline

static cvar_t *sndbits;
static cvar_t *sndspeed;
static cvar_t *sndchannels;

#define SND_BUFFER_SAMPLES	16384	// must be power of 2 (total samples, all channels)

void Snd_Memset(void *dest, const int val, const size_t count)
{
	Com_Memset(dest, val, count);
}

// SDL audio callback -- runs in a separate thread.
// In cinematic direct mode, reads from s_rawsamples[] ring buffer.
// Otherwise, copies mixed audio from dma.buffer ring into SDL's output.
static void sdl_audio_callback(void *userdata, Uint8 *stream, int len)
{
	(void)userdata;

	if (!snd_inited) {
		memset(stream, 0, len);
		return;
	}

	if (cin_direct) {
		// Direct cinematic audio: read from s_rawsamples[]
		int frames = len / (dma.channels * (dma.samplebits / 8));
		short *out = (short *)stream;
		int rp = cin_readpos;
		int re = s_rawend;
		int i;

		// If we fell behind the ring buffer, skip ahead
		if (re - rp > MAX_RAW_SAMPLES) {
			rp = re - MAX_RAW_SAMPLES;
		}

		for (i = 0; i < frames; i++) {
			if (rp < re) {
				int idx = rp & (MAX_RAW_SAMPLES - 1);
				int left  = s_rawsamples[idx].left >> 8;
				int right = s_rawsamples[idx].right >> 8;
				// clamp to 16-bit
				if (left > 32767) left = 32767;
				else if (left < -32768) left = -32768;
				if (right > 32767) right = 32767;
				else if (right < -32768) right = -32768;
				out[i*2]   = (short)left;
				out[i*2+1] = (short)right;
				rp++;
			} else {
				out[i*2]   = 0;
				out[i*2+1] = 0;
			}
		}
		cin_readpos = rp;

		// Keep dma_pos advancing so S_GetSoundtime doesn't stall
		dma_pos = (dma_pos + frames * dma.channels) % dma.samples;
		return;
	}

	// Normal path: copy from dma.buffer
	if (!dma.buffer) {
		memset(stream, 0, len);
		return;
	}

	{
		int bytes_per_sample = dma.samplebits / 8;
		int total_bytes      = dma.samples * bytes_per_sample;
		int pos              = dma_pos * bytes_per_sample;

		while (len > 0) {
			int chunk = total_bytes - pos;
			if (chunk > len)
				chunk = len;
			memcpy(stream, dma.buffer + pos, chunk);
			stream += chunk;
			len    -= chunk;
			pos     = (pos + chunk) % total_bytes;
		}

		dma_pos = pos / bytes_per_sample;
	}
}

void SNDDMA_CinDirectStart(void)
{
	SDL_LockAudio();
	cin_readpos = s_rawend;
	cin_direct  = 1;
	SDL_UnlockAudio();
}

void SNDDMA_CinDirectStop(void)
{
	SDL_LockAudio();
	cin_direct = 0;
	SDL_UnlockAudio();
}

qboolean SNDDMA_Init(void)
{
	SDL_AudioSpec desired, obtained;

	if (snd_inited)
		return qtrue;

	if (!SDL_WasInit(SDL_INIT_AUDIO)) {
		if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
			Com_Printf("SDL audio: init failed: %s\n", SDL_GetError());
			return qfalse;
		}
	}

	sndbits     = Cvar_Get("sndbits",     "16",    CVAR_ARCHIVE);
	sndspeed    = Cvar_Get("sndspeed",    "44100", CVAR_ARCHIVE);
	sndchannels = Cvar_Get("sndchannels", "2",     CVAR_ARCHIVE);

	dma.samplebits = (int)sndbits->value;
	if (dma.samplebits != 8)
		dma.samplebits = 16;

	dma.speed = (int)sndspeed->value;
	if (dma.speed <= 0)
		dma.speed = 44100;

	dma.channels = (int)sndchannels->value;
	if (dma.channels < 1 || dma.channels > 2)
		dma.channels = 2;

	memset(&desired, 0, sizeof(desired));
	desired.freq     = dma.speed;
	desired.format   = (dma.samplebits == 16) ? AUDIO_S16LSB : AUDIO_U8;
	desired.channels = dma.channels;
	desired.samples  = 1024;	// ~23 ms at 44100 Hz
	desired.callback = sdl_audio_callback;

	if (SDL_OpenAudio(&desired, &obtained) < 0) {
		Com_Printf("SDL audio: could not open device: %s\n",
			SDL_GetError());
		return qfalse;
	}

	// adapt to what SDL actually gave us
	dma.speed      = obtained.freq;
	dma.channels   = obtained.channels;
	dma.samplebits = (obtained.format & 0xFF);  // bits from format

	dma.samples          = SND_BUFFER_SAMPLES;
	dma.submission_chunk = obtained.samples;
	dma.buffer           = (byte *)calloc(1, dma.samples * (dma.samplebits / 8));
	if (!dma.buffer) {
		Com_Printf("SDL audio: failed to allocate DMA buffer\n");
		SDL_CloseAudio();
		return qfalse;
	}

	dma_pos    = 0;
	snd_inited = 1;

	Com_Printf("SDL audio: %d Hz, %d-bit, %s\n",
		dma.speed, dma.samplebits,
		dma.channels == 2 ? "stereo" : "mono");

	SDL_PauseAudio(0);	// start playback

	return qtrue;
}

int SNDDMA_GetDMAPos(void)
{
	if (!snd_inited)
		return 0;
	return dma_pos;
}

void SNDDMA_Shutdown(void)
{
	if (!snd_inited)
		return;

	snd_inited = 0;

	SDL_CloseAudio();

	if (dma.buffer) {
		free(dma.buffer);
		dma.buffer = NULL;
	}
}

void SNDDMA_Submit(void)
{
	// SDL pulls audio via callback -- nothing to push here.
}

void SNDDMA_BeginPainting(void)
{
}
