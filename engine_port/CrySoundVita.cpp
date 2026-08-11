/* Real PS Vita audio backend for CrySound.
   ----------------------------------------
   CrySound is a proprietary FMOD-derived middleware SDK with no source
   anywhere in this tree -- only a compiled crysound.dll ships with the retail
   PC game.  CrySoundSystem/CrySoundStubs.cpp (which this file replaces in the
   Vita build, see engine_port/CMakeLists.txt) satisfied the linker by
   returning 0 from every entry point, which meant the port had no audio at
   all.  This is a genuine implementation of the subset the engine actually
   calls, on top of sceAudioOut: a software mixer with per-channel volume,
   pan, playback frequency, looping and 3D distance attenuation, fed by a real
   RIFF/WAVE parser.  Everything Far Cry's sound data actually needs -- the
   1357 sounds in fcdata/sounds.pak are plain PCM, 8 or 16 bit, mono or
   stereo, at 22050 or 44100 Hz.

   Deliberately still inert: the DirectSound FX rack, EAX/I3DL2 reverb, DSP
   units, CD audio, recording and the MOD/tracker music player (CM_*).  Those
   have no Vita equivalent and the engine treats a null return from them as
   "unsupported", which is the honest answer.  Sounds the parser does not
   understand (the handful of .ogg music streams -- there is no Vorbis decoder
   here) fail to load exactly as an unreadable file would, rather than being
   faked. */
#include <crysound.h>

#include <psp2/audioout.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{

// sceAudioOut's MAIN port runs at a fixed 48 kHz; everything is resampled to
// it. 1024 frames per block is ~21 ms, low enough for gunfire to feel attached
// to the trigger and long enough that the mixer thread is not woken constantly.
const int kOutRate    = 48000;
const int kGrain      = 1024;
const int kMaxChannels = 32;
const int kMaxStreamBytes = 24 * 1024 * 1024;
/* fcdata/Sounds.pak alone is 243 MB and the engine keeps decoded buffers
   resident until its own cache evicts them, against a 224 MB newlib heap
   shared with the level.  Cap what decoded audio may occupy: past the cap a
   load fails the same way an unreadable file does, which costs a sound
   effect, where an unchecked allocation would cost the whole session. */
const size_t kSampleMemoryBudget = 48u * 1024u * 1024u;
size_t g_sampleBytes = 0;

// CS_SAMPLE and CS_STREAM are already declared at global scope by crysound.h;
// this file supplies their definitions below.
struct MixChannel
{
	CS_SAMPLE *sample;
	CS_STREAM *stream;     //!< set for callback-driven streams (music)
	double     pos;        //!< fractional frame cursor into the sample
	double     step;       //!< sample frames advanced per output frame
	int        volume;     //!< 0..255, as CrySound defines it
	int        pan;        //!< 0..255, 128 = centred
	int        frequency;  //!< playback rate in Hz
	int        priority;
	bool       active;
	bool       paused;
	bool       looping;
	bool       muted;
	bool       reserved;
	bool       is3D;
	float      pos3D[3];
	float      vel3D[3];
	float      gain3D;     //!< distance attenuation, recomputed by CS_Update
	float      pan3D;      //!< 0..1 stereo placement from the listener basis
};

SceUID       g_port       = -1;
SceUID       g_thread     = -1;
SceUID       g_mutex      = -1;
volatile int g_running    = 0;
int          g_masterVol  = 255;
MixChannel   g_channels[kMaxChannels];
short        g_outBuf[2][kGrain * 2];
int          g_bufIndex   = 0;
int         *g_mixAccum   = 0;

float g_listenerPos[3] = { 0.0f, 0.0f, 0.0f };
float g_listenerFwd[3] = { 0.0f, 1.0f, 0.0f };
float g_listenerTop[3] = { 0.0f, 0.0f, 1.0f };
float g_rolloff        = 1.0f;
float g_distanceFactor = 1.0f;

#if defined(VITA_DEBUG_AUDIO)
// Verification aid: the mixer's own peak level plus sample-parse tallies,
// dumped to ux0:data so a headless run can prove real audio is being produced
// rather than silence. Absent from distributable builds.
volatile int g_debugPeak    = 0;
volatile int g_debugBlocks  = 0;
int          g_debugLoaded  = 0;
int          g_debugFailed  = 0;
#endif

CS_OPENCALLBACK  g_open  = 0;
CS_CLOSECALLBACK g_close = 0;
CS_READCALLBACK  g_read  = 0;
CS_SEEKCALLBACK  g_seek  = 0;
CS_TELLCALLBACK  g_tell  = 0;

inline void Lock()   { if (g_mutex >= 0) sceKernelLockMutex(g_mutex, 1, 0); }
inline void Unlock() { if (g_mutex >= 0) sceKernelUnlockMutex(g_mutex, 1); }

} // namespace

/* CS_SAMPLE and CS_STREAM are opaque forward declarations in crysound.h, so
   the backend owns their layout.  A stream is a sample plus a cursor: Far Cry
   streams music and long ambience, and decoding those progressively would need
   the codec this build does not have, so a stream is simply read in full and
   played through the same mixer path. */
struct CS_SAMPLE
{
	short *pcm;          //!< interleaved signed 16 bit
	int    frames;       //!< frames, not samples: stereo counts a pair as one
	int    channels;
	int    rate;
	unsigned int mode;
	int    loopStart, loopEnd;
	int    defVol, defPan, defPri, defFreq;
	float  minDistance, maxDistance;
	int    maxPlaybacks;
	size_t bytes;        //!< decoded size, tracked against the memory budget
	char   name[64];
};

struct CS_STREAM
{
	CS_SAMPLE *sample;
	int        channel;   //!< mixer channel it was last played on, -1 if none
	unsigned int position;

	/* A stream may instead be driven by a callback: that is how CMusicSystem
	   plays, decoding its (AD)PCM pattern data on demand rather than from a
	   file.  The callback fills a scratch block on the game thread and the
	   mixer consumes it from this single-producer/single-consumer ring, so
	   music decoding can never stall the audio thread. */
	CS_STREAMCALLBACK callback;
	int    cbParam;
	int    cbRate;
	int    cbChannels;
	short *ring;
	int    ringFrames;
	// Unsigned so that the modulo used to index the ring can never go negative
	// and the producer/consumer difference still wraps correctly, however long
	// a session runs.
	volatile unsigned int writeFrame;
	volatile unsigned int readFrame;
	unsigned char *scratch;
	int    scratchBytes;
};

namespace
{

unsigned short ReadU16(const unsigned char *p) { return (unsigned short)(p[0] | (p[1] << 8)); }
unsigned int   ReadU32(const unsigned char *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
	       ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

//! Parses a RIFF/WAVE image already in memory into a mixer-ready sample.
//! Returns 0 for anything that is not linear PCM, which is how an .ogg or a
//! truncated file reports itself -- the caller then fails the load honestly.
CS_SAMPLE *ParseWav(const unsigned char *data, int length)
{
	if (!data || length < 44)
		return 0;
	if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0)
		return 0;

	int channels = 0, rate = 0, bits = 0, formatTag = 0;
	const unsigned char *pcmData = 0;
	int pcmBytes = 0;

	int offset = 12;
	while (offset + 8 <= length)
	{
		const unsigned int chunkSize = ReadU32(data + offset + 4);
		const unsigned char *body = data + offset + 8;
		// A chunk claiming to extend past the buffer means a truncated file.
		if (chunkSize > (unsigned int)(length - offset - 8))
			break;

		if (memcmp(data + offset, "fmt ", 4) == 0 && chunkSize >= 16)
		{
			formatTag = ReadU16(body);
			channels  = ReadU16(body + 2);
			rate      = (int)ReadU32(body + 4);
			bits      = ReadU16(body + 14);
		}
		else if (memcmp(data + offset, "data", 4) == 0)
		{
			pcmData  = body;
			pcmBytes = (int)chunkSize;
		}
		offset += 8 + (int)chunkSize + ((int)chunkSize & 1); // chunks are word aligned
	}

	// 0xFFFE is WAVE_FORMAT_EXTENSIBLE, which the retail data uses for a few
	// sounds; its sub-format is PCM whenever the bit depth is 8 or 16.
	const bool bPcm = (formatTag == 1 || formatTag == 0xFFFE);
	if (!bPcm || !pcmData || pcmBytes <= 0 || channels < 1 || channels > 2 || rate <= 0)
		return 0;
	if (bits != 8 && bits != 16)
		return 0;

	const int bytesPerFrame = channels * (bits / 8);
	const int frames = pcmBytes / bytesPerFrame;
	if (frames <= 0)
		return 0;

	const size_t pcmBytesNeeded = (size_t)frames * channels * sizeof(short);
	if (g_sampleBytes + pcmBytesNeeded > kSampleMemoryBudget)
		return 0;

	CS_SAMPLE *smp = (CS_SAMPLE *)calloc(1, sizeof(CS_SAMPLE));
	if (!smp)
		return 0;
	smp->pcm = (short *)malloc(pcmBytesNeeded);
	if (!smp->pcm)
	{
		free(smp);
		return 0;
	}
	smp->bytes = pcmBytesNeeded;
	g_sampleBytes += pcmBytesNeeded;

	if (bits == 16)
	{
		memcpy(smp->pcm, pcmData, (size_t)frames * channels * sizeof(short));
	}
	else
	{
		// 8 bit WAV data is unsigned with 128 as silence.
		for (int i = 0; i < frames * channels; i++)
			smp->pcm[i] = (short)(((int)pcmData[i] - 128) << 8);
	}

	smp->frames      = frames;
	smp->channels    = channels;
	smp->rate        = rate;
	smp->loopStart   = 0;
	smp->loopEnd     = frames - 1;
	smp->defVol      = 255;
	smp->defPan      = 128;
	smp->defPri      = 128;
	smp->defFreq     = rate;
	smp->minDistance = 1.0f;
	smp->maxDistance = 1000000.0f;
	smp->maxPlaybacks = kMaxChannels;
	return smp;
}

//! Reads a whole file through the pak-aware callbacks the engine installs with
//! CS_File_SetCallbacks, so sounds inside fcdata/*.pak are reachable.
unsigned char *ReadWholeFile(const char *name, int *outLength, int maxBytes)
{
	*outLength = 0;
	if (!g_open || !g_read || !g_seek || !g_tell || !name)
		return 0;
	unsigned int handle = g_open(name);
	if (!handle)
		return 0;

	g_seek(handle, 0, 2 /* SEEK_END */);
	const int length = g_tell(handle);
	g_seek(handle, 0, 0 /* SEEK_SET */);
	if (length <= 0 || length > maxBytes)
	{
		if (g_close) g_close(handle);
		return 0;
	}

	/* Sniff the container before committing to the whole file.  The menu music
	   alone is a 13 MB Vorbis stream that this backend cannot decode, and
	   pulling it into the heap only to reject it is a spike worth avoiding. */
	unsigned char magic[12];
	if (g_read(magic, sizeof(magic), handle) != (int)sizeof(magic) ||
		memcmp(magic, "RIFF", 4) != 0 || memcmp(magic + 8, "WAVE", 4) != 0)
	{
		if (g_close) g_close(handle);
		return 0;
	}
	g_seek(handle, 0, 0 /* SEEK_SET */);

	unsigned char *buffer = (unsigned char *)malloc((size_t)length);
	if (!buffer)
	{
		if (g_close) g_close(handle);
		return 0;
	}
	const int got = g_read(buffer, length, handle);
	if (g_close) g_close(handle);
	if (got <= 0)
	{
		free(buffer);
		return 0;
	}
	*outLength = got;
	return buffer;
}

inline bool ValidChannel(int channel)
{
	return channel >= 0 && channel < kMaxChannels;
}

void PumpStream(CS_STREAM *st); //!< defined with the stream entry points below

/* Callback streams are registered here rather than discovered by walking the
   mixer channels, because they must be pumped even while nothing is playing
   them.  CSystem::Update skips the whole sound system when the game is paused
   -- which is exactly what the menu is -- so a pump driven from CS_Update
   would leave the menu music starved from the moment it started. */
const int kMaxCallbackStreams = 4;
CS_STREAM *g_cbStreams[kMaxCallbackStreams] = { 0, 0, 0, 0 };

void RegisterCallbackStream(CS_STREAM *st)
{
	Lock();
	for (int i = 0; i < kMaxCallbackStreams; i++)
		if (!g_cbStreams[i]) { g_cbStreams[i] = st; break; }
	Unlock();
}

void UnregisterCallbackStream(CS_STREAM *st)
{
	Lock();
	for (int i = 0; i < kMaxCallbackStreams; i++)
		if (g_cbStreams[i] == st) g_cbStreams[i] = 0;
	Unlock();
}

//! Snapshots the registry under the lock, then renders outside it: the music
//! callback takes the music system's own critical section, and holding the
//! mixer lock across that would let the two deadlock against each other.
void PumpAllStreams()
{
	CS_STREAM *snapshot[kMaxCallbackStreams];
	Lock();
	for (int i = 0; i < kMaxCallbackStreams; i++)
		snapshot[i] = g_cbStreams[i];
	Unlock();

	for (int i = 0; i < kMaxCallbackStreams; i++)
		if (snapshot[i])
			PumpStream(snapshot[i]);
}

//! Adds one block of a callback-driven stream (music) into the accumulator.
//! Consumes whatever the producer has published; underruns fall silent for
//! those frames rather than repeating stale audio.
void MixStreamChannel(MixChannel &ch)
{
	CS_STREAM *st = ch.stream;
	if (!st || !st->ring || st->ringFrames <= 0)
		return;

	const float gain = (ch.volume / 255.0f) * (g_masterVol / 255.0f);
	const float angle = (ch.pan / 255.0f) * 1.5707963f;
	const float gainL = cosf(angle) * 1.41421356f * gain;
	const float gainR = sinf(angle) * 1.41421356f * gain;
	const double step = (double)st->cbRate / (double)kOutRate;

	for (int i = 0; i < kGrain; i++)
	{
		const unsigned int available = st->writeFrame - st->readFrame;
		if (available <= 1)
			break; // starved: leave the rest of the block silent
		const unsigned int index = st->readFrame % (unsigned int)st->ringFrames;
		const unsigned int next  = (st->readFrame + 1) % (unsigned int)st->ringFrames;
		const float frac = (float)ch.pos;

		int left, right;
		if (st->cbChannels == 2)
		{
			const int l0 = st->ring[index * 2],     l1 = st->ring[next * 2];
			const int r0 = st->ring[index * 2 + 1], r1 = st->ring[next * 2 + 1];
			left  = l0 + (int)((l1 - l0) * frac);
			right = r0 + (int)((r1 - r0) * frac);
		}
		else
		{
			const int s0 = st->ring[index], s1 = st->ring[next];
			left = right = s0 + (int)((s1 - s0) * frac);
		}

		g_mixAccum[i * 2]     += (int)(left  * gainL);
		g_mixAccum[i * 2 + 1] += (int)(right * gainR);

		// ch.pos carries the sub-frame remainder between blocks.
		ch.pos += step;
		while (ch.pos >= 1.0)
		{
			ch.pos -= 1.0;
			st->readFrame++;
		}
	}
}

//! Mixes one block. Called only from the audio thread, with the lock held.
void MixBlock(short *out)
{
	memset(g_mixAccum, 0, sizeof(int) * kGrain * 2);

	for (int c = 0; c < kMaxChannels; c++)
	{
		MixChannel &ch = g_channels[c];
		if (!ch.active || ch.paused || ch.muted)
			continue;
		if (ch.stream)
		{
			MixStreamChannel(ch);
			continue;
		}
		if (!ch.sample)
			continue;

		const CS_SAMPLE *smp = ch.sample;
		// CrySound volume and the master are both 0..255; 3D channels carry an
		// extra distance term computed once per frame in CS_Update.
		float gain = (ch.volume / 255.0f) * (g_masterVol / 255.0f);
		if (ch.is3D)
			gain *= ch.gain3D;

		const float panPos = ch.is3D ? ch.pan3D : (ch.pan / 255.0f);
		// Constant-power panning keeps a sound's loudness steady as it crosses
		// in front of the listener instead of dipping in the middle.
		const float angle = panPos * 1.5707963f;
		float gainL = cosf(angle) * 1.41421356f * gain;
		float gainR = sinf(angle) * 1.41421356f * gain;
		if (gainL > 4.0f) gainL = 4.0f;
		if (gainR > 4.0f) gainR = 4.0f;

		for (int i = 0; i < kGrain; i++)
		{
			int frame = (int)ch.pos;
			if (frame >= smp->frames)
			{
				if (ch.looping && smp->frames > 0)
				{
					const int loopLen = (smp->loopEnd - smp->loopStart) + 1;
					if (loopLen > 0)
					{
						ch.pos = smp->loopStart + fmod(ch.pos - smp->loopStart, (double)loopLen);
						frame = (int)ch.pos;
					}
					else
					{
						ch.pos = 0.0;
						frame = 0;
					}
				}
				else
				{
					ch.active = false;
					break;
				}
			}

			// Most of the sound set is 22050 Hz against a 48 kHz output, so the
			// resampler runs on almost every sound; linear interpolation costs
			// one multiply-add per sample and avoids the aliasing hiss that
			// nearest-neighbour picks up on that ratio.
			int next = frame + 1;
			if (next >= smp->frames)
				next = ch.looping ? smp->loopStart : frame;
			const float frac = (float)(ch.pos - (double)frame);

			int left, right;
			if (smp->channels == 2)
			{
				const int l0 = smp->pcm[frame * 2],     l1 = smp->pcm[next * 2];
				const int r0 = smp->pcm[frame * 2 + 1], r1 = smp->pcm[next * 2 + 1];
				left  = l0 + (int)((l1 - l0) * frac);
				right = r0 + (int)((r1 - r0) * frac);
			}
			else
			{
				const int s0 = smp->pcm[frame], s1 = smp->pcm[next];
				left = right = s0 + (int)((s1 - s0) * frac);
			}

			g_mixAccum[i * 2]     += (int)(left  * gainL);
			g_mixAccum[i * 2 + 1] += (int)(right * gainR);
			ch.pos += ch.step;
		}
	}

	for (int i = 0; i < kGrain * 2; i++)
	{
		int v = g_mixAccum[i];
		if (v >  32767) v =  32767;
		if (v < -32768) v = -32768;
		out[i] = (short)v;
#if defined(VITA_DEBUG_AUDIO)
		const int mag = v < 0 ? -v : v;
		if (mag > g_debugPeak) g_debugPeak = mag;
#endif
	}
#if defined(VITA_DEBUG_AUDIO)
	g_debugBlocks++;
#endif
}

int MixerThread(SceSize, void *)
{
	while (g_running)
	{
		short *block = g_outBuf[g_bufIndex];
		g_bufIndex ^= 1;

		// Refilled here, one block ahead of when it is needed, so a slow decode
		// eats into this block's slack rather than causing a dropout.
		PumpAllStreams();

#if defined(VITA_DEBUG_AUDIO)
		// Reported from this thread rather than CS_Update so the numbers keep
		// coming while the game is paused in a menu, which is exactly the case
		// the mixer-thread pump exists to cover. ~2 s at 48 kHz / 1024 frames.
		{
			static int s_lastReport = 0;
			if (g_debugBlocks - s_lastReport > 94)
			{
				s_lastReport = g_debugBlocks;
				int streamChannels = 0, streamBuffered = 0;
				for (int i = 0; i < kMaxCallbackStreams; i++)
					if (g_cbStreams[i])
					{
						streamChannels++;
						streamBuffered = (int)(g_cbStreams[i]->writeFrame - g_cbStreams[i]->readFrame);
					}
				FILE *f = fopen("ux0:data/farcry_audio.txt", "ab");
				if (f)
				{
					fprintf(f, "blocks=%d peak=%d/32767 playing=%d streams=%d buffered=%d "
						"samples_ok=%d samples_failed=%d pcm_kb=%d\n",
						g_debugBlocks, g_debugPeak, CS_GetChannelsPlaying(),
						streamChannels, streamBuffered,
						g_debugLoaded, g_debugFailed, (int)(g_sampleBytes / 1024));
					fclose(f);
				}
				g_debugPeak = 0;
			}
		}
#endif

		Lock();
		MixBlock(block);
		Unlock();

		// Blocks until the previously queued block has been consumed, which is
		// what paces this loop -- no sleeping or timing of our own.
		sceAudioOutOutput(g_port, block);
	}
	return 0;
}

} // namespace

signed char CS_SetOutput(int outputtype) { return 1; }
signed char CS_SetDriver(int driver) { return 1; }
signed char CS_SetMixer(int mixer) { return 1; }
signed char CS_SetBufferSize(int len_ms) { return 1; }
signed char CS_SetHWND(void *hwnd) { return 1; }
signed char CS_SetMinHardwareChannels(int min) { return 1; }
signed char CS_SetMaxHardwareChannels(int max) { return 1; }

signed char CS_Init(int mixrate, int maxsoftwarechannels, unsigned int flags)
{
	if (g_running)
		return 1;

	memset(g_channels, 0, sizeof(g_channels));
	for (int i = 0; i < kMaxChannels; i++)
	{
		g_channels[i].volume    = 255;
		g_channels[i].pan       = 128;
		g_channels[i].frequency = kOutRate;
	}

	g_mixAccum = (int *)malloc(sizeof(int) * kGrain * 2);
	if (!g_mixAccum)
		return 0;
	memset(g_outBuf, 0, sizeof(g_outBuf));

	g_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, kGrain, kOutRate,
		SCE_AUDIO_OUT_MODE_STEREO);
	if (g_port < 0)
	{
		sceClibPrintf("[VITA AUDIO] sceAudioOutOpenPort failed: 0x%08x\n", (unsigned)g_port);
		free(g_mixAccum);
		g_mixAccum = 0;
		return 0;
	}
	int portVolume[2] = { SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB };
	sceAudioOutSetVolume(g_port,
		(SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH),
		portVolume);

	g_mutex = sceKernelCreateMutex("crysound_mix", 0, 0, 0);
	g_running = 1;
	/* Pin the mixer to its own core.  The sixth argument is the CPU affinity
	   mask and this passed 0, which means "inherit", so the mixer ran on
	   whichever core created it -- the main thread's, core 0.  Everything else
	   in this port is on that same core (the engine is single threaded and this
	   is the only other thread it has), so a 32-voice software mixer resampling
	   and summing into a float accumulator at 48 kHz was being paid for out of
	   the frame budget, continuously.  Core 2 is otherwise idle here. */
	g_thread = sceKernelCreateThread("crysound_mixer", MixerThread, 0x10000100, 0x10000,
		0, SCE_KERNEL_CPU_MASK_USER_2, 0);
	if (g_thread < 0)
	{
		// Older kernels reject an explicit user mask on some configurations;
		// an unpinned mixer is still better than no audio at all.
		sceClibPrintf("[VITA AUDIO] pinned mixer thread rejected (0x%08x), retrying unpinned\n",
			(unsigned)g_thread);
		g_thread = sceKernelCreateThread("crysound_mixer", MixerThread, 0x10000100, 0x10000, 0, 0, 0);
	}
	if (g_thread < 0)
	{
		sceClibPrintf("[VITA AUDIO] mixer thread creation failed: 0x%08x\n", (unsigned)g_thread);
		g_running = 0;
		sceAudioOutReleasePort(g_port);
		g_port = -1;
		return 0;
	}
	sceKernelStartThread(g_thread, 0, 0);
	sceClibPrintf("[VITA AUDIO] mixer running: %d Hz, %d frames per block, %d channels\n",
		kOutRate, kGrain, kMaxChannels);
	return 1;
}

void CS_Close()
{
	if (!g_running)
		return;
	g_running = 0;
	if (g_thread >= 0)
	{
		sceKernelWaitThreadEnd(g_thread, 0, 0);
		sceKernelDeleteThread(g_thread);
		g_thread = -1;
	}
	if (g_port >= 0)
	{
		sceAudioOutReleasePort(g_port);
		g_port = -1;
	}
	if (g_mutex >= 0)
	{
		sceKernelDeleteMutex(g_mutex);
		g_mutex = -1;
	}
	free(g_mixAccum);
	g_mixAccum = 0;
}

void CS_Update()
{
	if (!g_running)
		return;

	// 3D attenuation is resolved here, once per game frame, rather than inside
	// the mixer: the listener and emitter positions only change at frame rate,
	// and the audio thread should stay a tight sample loop.
	Lock();
	for (int c = 0; c < kMaxChannels; c++)
	{
		MixChannel &ch = g_channels[c];
		if (!ch.active || !ch.is3D || !ch.sample)
			continue;

		const float dx = (ch.pos3D[0] - g_listenerPos[0]) * g_distanceFactor;
		const float dy = (ch.pos3D[1] - g_listenerPos[1]) * g_distanceFactor;
		const float dz = (ch.pos3D[2] - g_listenerPos[2]) * g_distanceFactor;
		const float distance = sqrtf(dx * dx + dy * dy + dz * dz);

		const float minDist = ch.sample->minDistance > 0.01f ? ch.sample->minDistance : 1.0f;
		const float maxDist = ch.sample->maxDistance;
		if (distance <= minDist)
			ch.gain3D = 1.0f;
		else if (distance >= maxDist)
			ch.gain3D = 0.0f;
		else
		{
			// Inverse-distance rolloff, the model CrySound's callers assume.
			ch.gain3D = minDist / (minDist + g_rolloff * (distance - minDist));
			if (ch.gain3D > 1.0f) ch.gain3D = 1.0f;
			if (ch.gain3D < 0.0f) ch.gain3D = 0.0f;
		}

		// Place the emitter across the stereo field using the listener's own
		// right vector (forward x top), so turning the camera moves the sound.
		const float rx = g_listenerFwd[1] * g_listenerTop[2] - g_listenerFwd[2] * g_listenerTop[1];
		const float ry = g_listenerFwd[2] * g_listenerTop[0] - g_listenerFwd[0] * g_listenerTop[2];
		const float rz = g_listenerFwd[0] * g_listenerTop[1] - g_listenerFwd[1] * g_listenerTop[0];
		if (distance > 0.001f)
		{
			float side = (dx * rx + dy * ry + dz * rz) / distance;
			if (side >  1.0f) side =  1.0f;
			if (side < -1.0f) side = -1.0f;
			ch.pan3D = 0.5f + 0.5f * side;
		}
		else
			ch.pan3D = 0.5f;
	}
	Unlock();
}
void CS_SetSpeakerMode(unsigned int speakermode) {  }
void CS_SetSFXMasterVolume(int volume)
{
	Lock();
	g_masterVol = volume < 0 ? 0 : (volume > 255 ? 255 : volume);
	Unlock();
}
void CS_SetPanSeperation(float pansep) {  }
int CS_GetError() { return 0; }
// The engine refuses to bring the sound system up unless this matches exactly
// (CSoundSystem::Init's "Incorrect DLL version for CRYSOUND" gate).
float CS_GetVersion() { return CS_VERSION; }
int CS_GetOutput() { return 0; }
void * CS_GetOutputHandle() { return 0; }
int CS_GetDriver() { return 0; }
int CS_GetMixer() { return 0; }
int CS_GetNumDrivers() { return 1; }
signed char * CS_GetDriverName(int id) { return (signed char *)"PS Vita sceAudioOut"; }
signed char CS_GetDriverCaps(int id, unsigned int *caps) { if (caps) *caps = 0; return 1; }
int CS_GetOutputRate() { return kOutRate; }
int CS_GetMaxChannels() { return kMaxChannels; }
int CS_GetMaxSamples() { return 0; }
int CS_GetSFXMasterVolume() { return g_masterVol; }
int CS_GetNumHardwareChannels() { return 0; }
int CS_GetChannelsPlaying()
{
	int n = 0;
	Lock();
	for (int i = 0; i < kMaxChannels; i++)
		if (g_channels[i].active && !g_channels[i].paused)
			n++;
	Unlock();
	return n;
}
float CS_GetCPUUsage() { return 0.0f; }
void CS_GetMemoryStats(unsigned int *currentalloced, unsigned int *maxalloced)
{
	if (currentalloced) *currentalloced = 0;
	if (maxalloced) *maxalloced = 0;
}

CS_SAMPLE * CS_Sample_Load(int index, const char *name_or_data, unsigned int mode, int memlength)
{
	CS_SAMPLE *smp = 0;
	if (mode & CS_LOADMEMORY)
	{
		smp = ParseWav((const unsigned char *)name_or_data, memlength);
	}
	else
	{
		int length = 0;
		unsigned char *file = ReadWholeFile(name_or_data, &length, kMaxStreamBytes);
		if (file)
		{
			smp = ParseWav(file, length);
			free(file);
		}
	}
#if defined(VITA_DEBUG_AUDIO)
	if (smp) g_debugLoaded++; else g_debugFailed++;
#endif
	if (!smp)
		return 0;

	smp->mode = mode;
	if (!(mode & CS_LOADMEMORY) && name_or_data)
	{
		strncpy(smp->name, name_or_data, sizeof(smp->name) - 1);
		smp->name[sizeof(smp->name) - 1] = 0;
	}
	return smp;
}

CS_SAMPLE * CS_Sample_Alloc(int index, int length, unsigned int mode, int deffreq, int defvol, int defpan, int defpri)
{
	if (length <= 0)
		return 0;
	const int channels = (mode & CS_STEREO) ? 2 : 1;
	CS_SAMPLE *smp = (CS_SAMPLE *)calloc(1, sizeof(CS_SAMPLE));
	if (!smp)
		return 0;
	smp->pcm = (short *)calloc((size_t)length * channels, sizeof(short));
	if (!smp->pcm)
	{
		free(smp);
		return 0;
	}
	smp->bytes     = (size_t)length * channels * sizeof(short);
	g_sampleBytes += smp->bytes;
	smp->frames    = length;
	smp->channels  = channels;
	smp->rate      = deffreq > 0 ? deffreq : kOutRate;
	smp->mode      = mode;
	smp->loopStart = 0;
	smp->loopEnd   = length - 1;
	smp->defVol    = defvol;
	smp->defPan    = defpan;
	smp->defPri    = defpri;
	smp->defFreq   = smp->rate;
	smp->minDistance = 1.0f;
	smp->maxDistance = 1000000.0f;
	smp->maxPlaybacks = kMaxChannels;
	return smp;
}

void CS_Sample_Free(CS_SAMPLE *sptr)
{
	if (!sptr)
		return;
	// Silence any channel still reading this sample before the memory goes.
	Lock();
	for (int i = 0; i < kMaxChannels; i++)
		if (g_channels[i].sample == sptr)
		{
			g_channels[i].active = false;
			g_channels[i].sample = 0;
		}
	Unlock();
	if (g_sampleBytes >= sptr->bytes)
		g_sampleBytes -= sptr->bytes;
	free(sptr->pcm);
	free(sptr);
}

signed char CS_Sample_Upload(CS_SAMPLE *sptr, void *srcdata, unsigned int mode)
{
	if (!sptr || !srcdata || !sptr->pcm)
		return 0;
	memcpy(sptr->pcm, srcdata, (size_t)sptr->frames * sptr->channels * sizeof(short));
	return 1;
}
signed char CS_Sample_Lock(CS_SAMPLE *sptr, int offset, int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2)
{
	if (!sptr || !sptr->pcm || !ptr1 || !len1)
		return 0;
	const int frameBytes = sptr->channels * (int)sizeof(short);
	*ptr1 = (unsigned char *)sptr->pcm + (size_t)offset * frameBytes;
	*len1 = (unsigned int)(length * frameBytes);
	if (ptr2) *ptr2 = 0;
	if (len2) *len2 = 0;
	return 1;
}
signed char CS_Sample_Unlock(CS_SAMPLE *sptr, void *ptr1, void *ptr2, unsigned int len1, unsigned int len2) { return 1; }
signed char CS_Sample_SetMode(CS_SAMPLE *sptr, unsigned int mode)
{
	if (!sptr) return 0;
	sptr->mode = mode;
	return 1;
}
signed char CS_Sample_SetLoopPoints(CS_SAMPLE *sptr, int loopstart, int loopend)
{
	if (!sptr) return 0;
	// Clamped because the mixer indexes the buffer directly from these; a loop
	// region past the end would read off the allocation.
	if (loopstart < 0) loopstart = 0;
	if (loopstart >= sptr->frames) loopstart = sptr->frames - 1;
	if (loopend < loopstart) loopend = loopstart;
	if (loopend >= sptr->frames) loopend = sptr->frames - 1;
	sptr->loopStart = loopstart;
	sptr->loopEnd   = loopend;
	return 1;
}
signed char CS_Sample_SetDefaults(CS_SAMPLE *sptr, int deffreq, int defvol, int defpan, int defpri)
{
	if (!sptr) return 0;
	// -1 means "leave this one alone", as CrySound's callers expect.
	if (deffreq >= 0) sptr->defFreq = deffreq;
	if (defvol  >= 0) sptr->defVol  = defvol;
	if (defpan  >= 0) sptr->defPan  = defpan;
	if (defpri  >= 0) sptr->defPri  = defpri;
	return 1;
}
signed char CS_Sample_SetMinMaxDistance(CS_SAMPLE *sptr, float min, float max)
{
	if (!sptr) return 0;
	sptr->minDistance = min;
	sptr->maxDistance = max;
	return 1;
}
signed char CS_Sample_SetMaxPlaybacks(CS_SAMPLE *sptr, int max)
{
	if (!sptr) return 0;
	sptr->maxPlaybacks = max;
	return 1;
}
CS_SAMPLE * CS_Sample_Get(int sampno) { return 0; }
char * CS_Sample_GetName(CS_SAMPLE *sptr) { return sptr ? sptr->name : 0; }
unsigned int CS_Sample_GetLength(CS_SAMPLE *sptr) { return sptr ? (unsigned int)sptr->frames : 0; }
signed char CS_Sample_GetLoopPoints(CS_SAMPLE *sptr, int *loopstart, int *loopend)
{
	if (!sptr) return 0;
	if (loopstart) *loopstart = sptr->loopStart;
	if (loopend)   *loopend   = sptr->loopEnd;
	return 1;
}
signed char CS_Sample_GetDefaults(CS_SAMPLE *sptr, int *deffreq, int *defvol, int *defpan, int *defpri)
{
	if (!sptr) return 0;
	if (deffreq) *deffreq = sptr->defFreq;
	if (defvol)  *defvol  = sptr->defVol;
	if (defpan)  *defpan  = sptr->defPan;
	if (defpri)  *defpri  = sptr->defPri;
	return 1;
}
unsigned int CS_Sample_GetMode(CS_SAMPLE *sptr) { return sptr ? sptr->mode : 0; }

int CS_PlaySound(int channel, CS_SAMPLE *sptr) { return CS_PlaySoundEx(channel, sptr, 0, 0); }

int CS_PlaySoundEx(int channel, CS_SAMPLE *sptr, CS_DSPUNIT *dsp, signed char startpaused)
{
	if (!g_running || !sptr || !sptr->pcm || sptr->frames <= 0)
		return -1;

	Lock();
	if (channel == CS_FREE || !ValidChannel(channel))
	{
		channel = -1;
		for (int i = 0; i < kMaxChannels; i++)
			if (!g_channels[i].active && !g_channels[i].reserved) { channel = i; break; }
		if (channel < 0)
		{
			// Nothing free: displace the quietest lowest-priority voice rather
			// than dropping the new sound outright.
			int worst = -1, worstScore = 0x7fffffff;
			for (int i = 0; i < kMaxChannels; i++)
			{
				if (g_channels[i].reserved)
					continue;
				const int score = g_channels[i].priority * 256 + g_channels[i].volume;
				if (score < worstScore) { worstScore = score; worst = i; }
			}
			if (worst < 0) { Unlock(); return -1; }
			channel = worst;
		}
	}

	MixChannel &ch = g_channels[channel];
	ch.sample    = sptr;
	ch.stream    = 0;   // channel may have last been used by a music stream
	ch.pos       = 0.0;
	ch.volume    = sptr->defVol >= 0 ? sptr->defVol : 255;
	ch.pan       = sptr->defPan >= 0 ? sptr->defPan : 128;
	ch.frequency = sptr->defFreq > 0 ? sptr->defFreq : sptr->rate;
	ch.step      = (double)ch.frequency / (double)kOutRate;
	ch.priority  = sptr->defPri >= 0 ? sptr->defPri : 128;
	ch.looping   = (sptr->mode & CS_LOOP_NORMAL) != 0;
	ch.muted     = false;
	ch.paused    = startpaused != 0;
	ch.is3D      = (sptr->mode & CS_HW3D) != 0;
	ch.gain3D    = 1.0f;
	ch.pan3D     = 0.5f;
	ch.active    = true;
	Unlock();
	return channel;
}
signed char CS_StopSound(int channel)
{
	if (!ValidChannel(channel)) return 0;
	Lock();
	g_channels[channel].active = false;
	g_channels[channel].sample = 0;
	g_channels[channel].stream = 0;
	Unlock();
	return 1;
}
signed char CS_SetFrequency(int channel, int freq)
{
	if (!ValidChannel(channel) || freq <= 0) return 0;
	Lock();
	g_channels[channel].frequency = freq;
	g_channels[channel].step = (double)freq / (double)kOutRate;
	Unlock();
	return 1;
}
signed char CS_SetVolume(int channel, int vol)
{
	if (!ValidChannel(channel)) return 0;
	Lock();
	g_channels[channel].volume = vol < 0 ? 0 : (vol > 255 ? 255 : vol);
	Unlock();
	return 1;
}
signed char CS_SetVolumeAbsolute(int channel, int vol) { return CS_SetVolume(channel, vol); }
signed char CS_SetPan(int channel, int pan)
{
	if (!ValidChannel(channel)) return 0;
	Lock();
	// CS_STEREOPAN (-1) means "leave a stereo sound alone", i.e. dead centre.
	g_channels[channel].pan = (pan == CS_STEREOPAN) ? 128 :
		(pan < 0 ? 0 : (pan > 255 ? 255 : pan));
	Unlock();
	return 1;
}
signed char CS_SetSurround(int channel, signed char surround) { return 1; }
signed char CS_SetMute(int channel, signed char mute)
{
	if (!ValidChannel(channel)) return 0;
	Lock();
	g_channels[channel].muted = mute != 0;
	Unlock();
	return 1;
}
signed char CS_SetPriority(int channel, int priority)
{
	if (!ValidChannel(channel)) return 0;
	g_channels[channel].priority = priority;
	return 1;
}
signed char CS_SetReserved(int channel, signed char reserved)
{
	if (!ValidChannel(channel)) return 0;
	g_channels[channel].reserved = reserved != 0;
	return 1;
}
signed char CS_SetPaused(int channel, signed char paused)
{
	if (!ValidChannel(channel)) return 0;
	Lock();
	g_channels[channel].paused = paused != 0;
	Unlock();
	return 1;
}
signed char CS_SetLoopMode(int channel, unsigned int loopmode)
{
	if (!ValidChannel(channel)) return 0;
	Lock();
	g_channels[channel].looping = (loopmode & CS_LOOP_NORMAL) != 0;
	Unlock();
	return 1;
}
signed char CS_SetCurrentPosition(int channel, unsigned int offset)
{
	if (!ValidChannel(channel)) return 0;
	Lock();
	MixChannel &ch = g_channels[channel];
	if (ch.sample && (int)offset < ch.sample->frames)
		ch.pos = (double)offset;
	Unlock();
	return 1;
}
signed char CS_IsPlaying(int channel)
{
	if (!ValidChannel(channel)) return 0;
	return (g_channels[channel].active && !g_channels[channel].paused) ? 1 : 0;
}
int CS_GetFrequency(int channel) { return ValidChannel(channel) ? g_channels[channel].frequency : 0; }
int CS_GetVolume(int channel) { return ValidChannel(channel) ? g_channels[channel].volume : 0; }
int CS_GetPan(int channel) { return ValidChannel(channel) ? g_channels[channel].pan : 128; }
signed char CS_GetSurround(int channel) { return 0; }
signed char CS_GetMute(int channel) { return ValidChannel(channel) && g_channels[channel].muted ? 1 : 0; }
int CS_GetPriority(int channel) { return ValidChannel(channel) ? g_channels[channel].priority : 0; }
signed char CS_GetReserved(int channel) { return ValidChannel(channel) && g_channels[channel].reserved ? 1 : 0; }
signed char CS_GetPaused(int channel) { return ValidChannel(channel) && g_channels[channel].paused ? 1 : 0; }
unsigned int CS_GetLoopMode(int channel)
{
	if (!ValidChannel(channel)) return CS_LOOP_OFF;
	return g_channels[channel].looping ? CS_LOOP_NORMAL : CS_LOOP_OFF;
}
unsigned int CS_GetCurrentPosition(int channel)
{
	return ValidChannel(channel) ? (unsigned int)g_channels[channel].pos : 0;
}
CS_SAMPLE * CS_GetCurrentSample(int channel) { return ValidChannel(channel) ? g_channels[channel].sample : 0; }
signed char CS_GetCurrentLevels(int channel, float *l, float *r) { return 0; }
int CS_FX_Enable(int channel, unsigned int fx) { return 0; }
signed char CS_FX_Disable(int channel) { return 0; }
signed char CS_FX_SetChorus(int fxid, float WetDryMix, float Depth, float Feedback, float Frequency, int Waveform, float Delay, int Phase) { return 0; }
signed char CS_FX_SetCompressor(int fxid, float Gain, float Attack, float Release, float Threshold, float Ratio, float Predelay) { return 0; }
signed char CS_FX_SetDistortion(int fxid, float Gain, float Edge, float PostEQCenterFrequency, float PostEQBandwidth, float PreLowpassCutoff) { return 0; }
signed char CS_FX_SetEcho(int fxid, float WetDryMix, float Feedback, float LeftDelay, float RightDelay, int PanDelay) { return 0; }
signed char CS_FX_SetFlanger(int fxid, float WetDryMix, float Depth, float Feedback, float Frequency, int Waveform, float Delay, int Phase) { return 0; }
signed char CS_FX_SetGargle(int fxid, int RateHz, int WaveShape) { return 0; }
signed char CS_FX_SetI3DL2Reverb(int fxid, int Room, int RoomHF, float RoomRolloffFactor, float DecayTime, float DecayHFRatio, int Reflections, float ReflectionsDelay, int Reverb, float ReverbDelay, float Diffusion, float Density, float HFReference) { return 0; }
signed char CS_FX_SetParamEQ(int fxid, float Center, float Bandwidth, float Gain) { return 0; }
signed char CS_FX_SetWavesReverb(int fxid, float InGain, float ReverbMix, float ReverbTime, float HighFreqRTRatio) { return 0; }
void CS_3D_SetDopplerFactor(float scale) {  }
void CS_3D_SetDistanceFactor(float scale) { g_distanceFactor = scale > 0.0f ? scale : 1.0f; }
void CS_3D_SetRolloffFactor(float scale) { g_rolloff = scale >= 0.0f ? scale : 1.0f; }
signed char CS_3D_SetAttributes(int channel, float *pos, float *vel)
{
	if (!ValidChannel(channel)) return 0;
	Lock();
	MixChannel &ch = g_channels[channel];
	if (pos) { ch.pos3D[0] = pos[0]; ch.pos3D[1] = pos[1]; ch.pos3D[2] = pos[2]; ch.is3D = true; }
	if (vel) { ch.vel3D[0] = vel[0]; ch.vel3D[1] = vel[1]; ch.vel3D[2] = vel[2]; }
	Unlock();
	return 1;
}
signed char CS_3D_GetAttributes(int channel, float *pos, float *vel)
{
	if (!ValidChannel(channel)) return 0;
	const MixChannel &ch = g_channels[channel];
	if (pos) { pos[0] = ch.pos3D[0]; pos[1] = ch.pos3D[1]; pos[2] = ch.pos3D[2]; }
	if (vel) { vel[0] = ch.vel3D[0]; vel[1] = ch.vel3D[1]; vel[2] = ch.vel3D[2]; }
	return 1;
}
void CS_3D_Listener_SetCurrent(int current, int numlisteners) {  }
void CS_3D_Listener_SetAttributes(float *pos, float *vel, float fx, float fy, float fz, float tx, float ty, float tz)
{
	if (pos) { g_listenerPos[0] = pos[0]; g_listenerPos[1] = pos[1]; g_listenerPos[2] = pos[2]; }
	g_listenerFwd[0] = fx; g_listenerFwd[1] = fy; g_listenerFwd[2] = fz;
	g_listenerTop[0] = tx; g_listenerTop[1] = ty; g_listenerTop[2] = tz;
}
void CS_3D_Listener_GetAttributes(float *pos, float *vel, float *fx, float *fy, float *fz, float *tx, float *ty, float *tz)
{
	if (pos) { pos[0] = g_listenerPos[0]; pos[1] = g_listenerPos[1]; pos[2] = g_listenerPos[2]; }
	if (vel) { vel[0] = vel[1] = vel[2] = 0.0f; }
	if (fx) *fx = g_listenerFwd[0];
	if (fy) *fy = g_listenerFwd[1];
	if (fz) *fz = g_listenerFwd[2];
	if (tx) *tx = g_listenerTop[0];
	if (ty) *ty = g_listenerTop[1];
	if (tz) *tz = g_listenerTop[2];
}
signed char CS_Stream_SetBufferSize(int ms) { return 1; }

/* A "stream" here is read in full and played through the ordinary mixer path.
   Progressive decoding would only matter for a codec, and the one format this
   backend understands (PCM WAVE) is cheap enough to hold resident; the size
   cap keeps a pathological file from eating the heap. */
CS_STREAM * CS_Stream_OpenFile(const char *filename, unsigned int mode, int memlength)
{
	CS_SAMPLE *smp = 0;
	if (mode & CS_LOADMEMORY)
		smp = ParseWav((const unsigned char *)filename, memlength);
	else
	{
		int length = 0;
		unsigned char *file = ReadWholeFile(filename, &length, kMaxStreamBytes);
		if (file)
		{
			smp = ParseWav(file, length);
			free(file);
		}
	}
	if (!smp)
		return 0;

	smp->mode = mode;
	if (!(mode & CS_LOADMEMORY) && filename)
	{
		strncpy(smp->name, filename, sizeof(smp->name) - 1);
		smp->name[sizeof(smp->name) - 1] = 0;
	}

	CS_STREAM *stream = (CS_STREAM *)calloc(1, sizeof(CS_STREAM));
	if (!stream)
	{
		CS_Sample_Free(smp);
		return 0;
	}
	stream->sample  = smp;
	stream->channel = -1;
	return stream;
}
/* CMusicSystem creates its stream this way: it hands over a callback that
   renders freshly decoded music into a buffer whenever more is wanted.  The
   ring is sized at four of those buffers (~0.4 s at the music system's own
   0.1 s latency) so a frame-rate hitch during level streaming does not
   immediately starve the music. */
CS_STREAM * CS_Stream_Create(CS_STREAMCALLBACK callback, int length, unsigned int mode, int samplerate, int userdata)
{
	if (!callback || length <= 0)
		return 0;

	CS_STREAM *stream = (CS_STREAM *)calloc(1, sizeof(CS_STREAM));
	if (!stream)
		return 0;

	stream->channel     = -1;
	stream->callback    = callback;
	stream->cbParam     = userdata;
	stream->cbRate      = samplerate > 0 ? samplerate : 44100;
	stream->cbChannels  = (mode & CS_STEREO) ? 2 : 1;
	stream->scratchBytes = length;
	stream->scratch     = (unsigned char *)malloc((size_t)length);

	const int bytesPerFrame = stream->cbChannels * (int)sizeof(short);
	stream->ringFrames  = (length / bytesPerFrame) * 4;
	stream->ring        = (short *)calloc((size_t)stream->ringFrames * stream->cbChannels, sizeof(short));
	if (!stream->scratch || !stream->ring || stream->ringFrames <= 0)
	{
		free(stream->scratch);
		free(stream->ring);
		free(stream);
		return 0;
	}
	RegisterCallbackStream(stream);
	return stream;
}

namespace
{
//! Pulls decoded audio from a callback stream on the game thread.
void PumpStream(CS_STREAM *st)
{
	if (!st || !st->callback || !st->ring || !st->scratch)
		return;
	const int bytesPerFrame = st->cbChannels * (int)sizeof(short);
	const int chunkFrames = st->scratchBytes / bytesPerFrame;

	// Keep the ring at least half full; each pass renders exactly the block
	// size the caller asked for when it created the stream.
	while (st->writeFrame - st->readFrame + (unsigned int)chunkFrames <= (unsigned int)st->ringFrames)
	{
		if (!st->callback(st, st->scratch, st->scratchBytes, st->cbParam))
			break;
		const short *src = (const short *)st->scratch;
		for (int f = 0; f < chunkFrames; f++)
		{
			const unsigned int slot = (st->writeFrame + f) % (unsigned int)st->ringFrames;
			for (int c = 0; c < st->cbChannels; c++)
				st->ring[slot * st->cbChannels + c] = src[f * st->cbChannels + c];
		}
		// Published only after the data is in place, so the mixer never reads
		// a frame that is still being written.
		st->writeFrame += chunkFrames;
	}
}
} // namespace
signed char CS_Stream_Close(CS_STREAM *stream)
{
	if (!stream) return 0;
	// Detach from the mixer before any of the stream's memory goes away.
	UnregisterCallbackStream(stream);
	Lock();
	for (int i = 0; i < kMaxChannels; i++)
		if (g_channels[i].stream == stream)
		{
			g_channels[i].active = false;
			g_channels[i].stream = 0;
		}
	Unlock();
	if (stream->channel >= 0 && !stream->callback)
		CS_StopSound(stream->channel);
	CS_Sample_Free(stream->sample);
	free(stream->ring);
	free(stream->scratch);
	free(stream);
	return 1;
}
int CS_Stream_Play(int channel, CS_STREAM *stream) { return CS_Stream_PlayEx(channel, stream, 0, 0); }
int CS_Stream_PlayEx(int channel, CS_STREAM *stream, CS_DSPUNIT *dsp, signed char startpaused)
{
	if (!stream)
		return -1;

	if (stream->callback)
	{
		// Callback stream: claim a channel directly, there is no sample behind it.
		Lock();
		if (channel == CS_FREE || !ValidChannel(channel))
		{
			channel = -1;
			for (int i = 0; i < kMaxChannels; i++)
				if (!g_channels[i].active && !g_channels[i].reserved) { channel = i; break; }
		}
		if (channel < 0) { Unlock(); return -1; }

		MixChannel &ch = g_channels[channel];
		ch.sample    = 0;
		ch.stream    = stream;
		ch.pos       = 0.0;
		ch.volume    = 255;
		ch.pan       = 128;
		ch.frequency = stream->cbRate;
		ch.step      = (double)stream->cbRate / (double)kOutRate;
		ch.priority  = 255;   // music should not be evicted by an effect
		ch.looping   = false;
		ch.muted     = false;
		ch.paused    = startpaused != 0;
		ch.is3D      = false;
		ch.active    = true;
		Unlock();
		stream->channel = channel;
		return channel;
	}

	if (!stream->sample)
		return -1;
	const int ch = CS_PlaySoundEx(channel, stream->sample, dsp, startpaused);
	if (ch >= 0 && stream->position)
		CS_SetCurrentPosition(ch, stream->position);
	stream->channel = ch;
	return ch;
}
signed char CS_Stream_Stop(CS_STREAM *stream)
{
	if (!stream || stream->channel < 0) return 0;
	Lock();
	if (ValidChannel(stream->channel))
	{
		g_channels[stream->channel].active = false;
		g_channels[stream->channel].stream = 0;
		g_channels[stream->channel].sample = 0;
	}
	Unlock();
	// A restarted callback stream should resume from fresh audio, not from
	// whatever the decoder left buffered when it was stopped.
	if (stream->callback)
		stream->readFrame = stream->writeFrame;
	stream->channel = -1;
	return 1;
}
int CS_Stream_GetOpenState(CS_STREAM *stream) { return stream ? 0 : -1; }
signed char CS_Stream_SetPosition(CS_STREAM *stream, unsigned int position)
{
	if (!stream) return 0;
	stream->position = position;
	if (stream->channel >= 0)
		CS_SetCurrentPosition(stream->channel, position);
	return 1;
}
unsigned int CS_Stream_GetPosition(CS_STREAM *stream)
{
	if (!stream) return 0;
	if (stream->channel >= 0)
		return CS_GetCurrentPosition(stream->channel);
	return stream->position;
}
signed char CS_Stream_SetTime(CS_STREAM *stream, int ms)
{
	if (!stream || !stream->sample) return 0;
	return CS_Stream_SetPosition(stream, (unsigned int)((double)ms * stream->sample->rate / 1000.0));
}
int CS_Stream_GetTime(CS_STREAM *stream)
{
	if (!stream || !stream->sample || stream->sample->rate <= 0) return 0;
	return (int)((double)CS_Stream_GetPosition(stream) * 1000.0 / stream->sample->rate);
}
int CS_Stream_GetLength(CS_STREAM *stream) { return stream && stream->sample ? stream->sample->frames : 0; }
int CS_Stream_GetLengthMs(CS_STREAM *stream)
{
	if (!stream || !stream->sample || stream->sample->rate <= 0) return 0;
	return (int)((double)stream->sample->frames * 1000.0 / stream->sample->rate);
}
signed char CS_Stream_SetLoopPoints(CS_STREAM *stream, unsigned int loopstartpcm, unsigned int loopendpcm)
{
	if (!stream || !stream->sample) return 0;
	return CS_Sample_SetLoopPoints(stream->sample, (int)loopstartpcm, (int)loopendpcm);
}
CS_SAMPLE * CS_Stream_GetSample(CS_STREAM *stream) { return stream ? stream->sample : 0; }
CS_DSPUNIT *F_API CS_Stream_CreateDSP(CS_STREAM *stream, CS_DSPCALLBACK callback, int priority, int param) { return 0; }
signed char CS_Stream_SetEndCallback(CS_STREAM *stream, CS_STREAMCALLBACK callback, int userdata) { return 0; }
signed char CS_Stream_SetSynchCallback(CS_STREAM *stream, CS_STREAMCALLBACK callback, int userdata) { return 0; }
int CS_Stream_AddSynchPoint(CS_STREAM *stream, unsigned int pcmoffset, int userdata) { return 0; }
signed char CS_Stream_DeleteSynchPoint(CS_STREAM *stream, int index) { return 0; }
int CS_Stream_GetNumSynchPoints(CS_STREAM *stream) { return 0; }
signed char CS_Stream_SetSubStream(CS_STREAM *stream, int index) { return 0; }
int CS_Stream_GetNumSubStreams(CS_STREAM *stream) { return 0; }
signed char CS_CD_Play(char drive, int track) { return 0; }
void CS_CD_SetPlayMode(char drive, signed char mode) {  }
signed char CS_CD_Stop(char drive) { return 0; }
signed char CS_CD_SetPaused(char drive, signed char paused) { return 0; }
signed char CS_CD_SetVolume(char drive, int volume) { return 0; }
signed char CS_CD_Eject(char drive) { return 0; }
signed char CS_CD_GetPaused(char drive) { return 0; }
int CS_CD_GetTrack(char drive) { return 0; }
int CS_CD_GetNumTracks(char drive) { return 0; }
int CS_CD_GetVolume(char drive) { return 0; }
int CS_CD_GetTrackLength(char drive, int track) { return 0; }
int CS_CD_GetTrackTime(char drive) { return 0; }
CS_DSPUNIT *F_API CS_DSP_Create(CS_DSPCALLBACK callback, int priority, int param) { return 0; }
void CS_DSP_Free(CS_DSPUNIT *unit) {  }
void CS_DSP_SetPriority(CS_DSPUNIT *unit, int priority) {  }
int CS_DSP_GetPriority(CS_DSPUNIT *unit) { return 0; }
void CS_DSP_SetActive(CS_DSPUNIT *unit, signed char active) {  }
signed char CS_DSP_GetActive(CS_DSPUNIT *unit) { return 0; }
CS_DSPUNIT *F_API CS_DSP_GetClearUnit() { return 0; }
CS_DSPUNIT *F_API CS_DSP_GetSFXUnit() { return 0; }
CS_DSPUNIT *F_API CS_DSP_GetMusicUnit() { return 0; }
CS_DSPUNIT *F_API CS_DSP_GetFFTUnit() { return 0; }
CS_DSPUNIT *F_API CS_DSP_GetClipAndCopyUnit() { return 0; }
signed char CS_DSP_MixBuffers(void *destbuffer, void *srcbuffer, int len, int freq, int vol, int pan, unsigned int mode) { return 0; }
void CS_DSP_ClearMixBuffer() {  }
int CS_DSP_GetBufferLength() { return 0; }
int CS_DSP_GetBufferLengthTotal() { return 0; }
float * CS_DSP_GetSpectrum() { return 0; }
signed char CS_Reverb_SetProperties(CS_REVERB_PROPERTIES *prop) { return 0; }
signed char CS_Reverb_GetProperties(CS_REVERB_PROPERTIES *prop) { return 0; }
signed char CS_Reverb_SetChannelProperties(int channel, CS_REVERB_CHANNELPROPERTIES *prop) { return 0; }
signed char CS_Reverb_GetChannelProperties(int channel, CS_REVERB_CHANNELPROPERTIES *prop) { return 0; }
signed char CS_Record_SetDriver(int outputtype) { return 0; }
int CS_Record_GetNumDrivers() { return 0; }
signed char * CS_Record_GetDriverName(int id) { return 0; }
int CS_Record_GetDriver() { return 0; }
signed char CS_Record_StartSample(CS_SAMPLE *sptr, signed char loop) { return 0; }
signed char CS_Record_Stop() { return 0; }
int CS_Record_GetPosition() { return 0; }
CM_MODULE * CM_LoadSong(const char *name) { return 0; }
CM_MODULE * CM_LoadSongMemory(void *data, int length) { return 0; }
signed char CM_FreeSong(CM_MODULE *mod) { return 0; }
signed char CM_PlaySong(CM_MODULE *mod) { return 0; }
signed char CM_StopSong(CM_MODULE *mod) { return 0; }
void CM_StopAllSongs() {  }
signed char CM_SetZxxCallback(CM_MODULE *mod, CM_CALLBACK callback) { return 0; }
signed char CM_SetRowCallback(CM_MODULE *mod, CM_CALLBACK callback, int rowstep) { return 0; }
signed char CM_SetOrderCallback(CM_MODULE *mod, CM_CALLBACK callback, int orderstep) { return 0; }
signed char CM_SetInstCallback(CM_MODULE *mod, CM_CALLBACK callback, int instrument) { return 0; }
signed char CM_SetSample(CM_MODULE *mod, int sampno, CS_SAMPLE *sptr) { return 0; }
signed char CM_SetUserData(CM_MODULE *mod, unsigned int userdata) { return 0; }
signed char CM_OptimizeChannels(CM_MODULE *mod, int maxchannels, int minvolume) { return 0; }
signed char CM_SetReverb(signed char reverb) { return 0; }
signed char CM_SetLooping(CM_MODULE *mod, signed char looping) { return 0; }
signed char CM_SetOrder(CM_MODULE *mod, int order) { return 0; }
signed char CM_SetPaused(CM_MODULE *mod, signed char pause) { return 0; }
signed char CM_SetMasterVolume(CM_MODULE *mod, int volume) { return 0; }
signed char CM_SetMasterSpeed(CM_MODULE *mode, float speed) { return 0; }
signed char CM_SetPanSeperation(CM_MODULE *mod, float pansep) { return 0; }
char * CM_GetName(CM_MODULE *mod) { return 0; }
int CM_GetType(CM_MODULE *mod) { return 0; }
int CM_GetNumOrders(CM_MODULE *mod) { return 0; }
int CM_GetNumPatterns(CM_MODULE *mod) { return 0; }
int CM_GetNumInstruments(CM_MODULE *mod) { return 0; }
int CM_GetNumSamples(CM_MODULE *mod) { return 0; }
int CM_GetNumChannels(CM_MODULE *mod) { return 0; }
CS_SAMPLE * CM_GetSample(CM_MODULE *mod, int sampno) { return 0; }
int CM_GetPatternLength(CM_MODULE *mod, int orderno) { return 0; }
signed char CM_IsFinished(CM_MODULE *mod) { return 0; }
signed char CM_IsPlaying(CM_MODULE *mod) { return 0; }
int CM_GetMasterVolume(CM_MODULE *mod) { return 0; }
int CM_GetGlobalVolume(CM_MODULE *mod) { return 0; }
int CM_GetOrder(CM_MODULE *mod) { return 0; }
int CM_GetPattern(CM_MODULE *mod) { return 0; }
int CM_GetSpeed(CM_MODULE *mod) { return 0; }
int CM_GetBPM(CM_MODULE *mod) { return 0; }
int CM_GetRow(CM_MODULE *mod) { return 0; }
signed char CM_GetPaused(CM_MODULE *mod) { return 0; }
int CM_GetTime(CM_MODULE *mod) { return 0; }
int CM_GetRealChannel(CM_MODULE *mod, int modchannel) { return 0; }
unsigned int CM_GetUserData(CM_MODULE *mod) { return 0; }
signed char CS_SetMemorySystem(void *pool, int poollen, CS_ALLOCCALLBACK useralloc, CS_REALLOCCALLBACK userrealloc, CS_FREECALLBACK userfree) { return 0; }
// CSoundSystem::Init hands us its ICryPak-backed file access here, which is
// the only way to reach sounds that live inside fcdata/*.pak.
void CS_File_SetCallbacks(CS_OPENCALLBACK useropen, CS_CLOSECALLBACK userclose, CS_READCALLBACK userread, CS_SEEKCALLBACK userseek, CS_TELLCALLBACK usertell)
{
	g_open  = useropen;
	g_close = userclose;
	g_read  = userread;
	g_seek  = userseek;
	g_tell  = usertell;
}
