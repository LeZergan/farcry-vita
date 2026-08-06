/* Real (return-0/null) stub implementations for CrySound, a proprietary
   third-party sound middleware SDK with NO source anywhere in this tree
   -- only a compiled crysound.dll exists in the installed retail game.
   Auto-generated from the real declarations in CryCommon/crysound.h so
   every signature matches exactly. This disables audio output entirely
   for a first booting build; a real backend (OpenAL, etc.) can replace
   this later without touching CrySoundSystem's own calling code, since
   the CS_ / CM_ interface stays identical. */
#include <crysound.h>

signed char CS_SetOutput(int outputtype) { return 0; }
signed char CS_SetDriver(int driver) { return 0; }
signed char CS_SetMixer(int mixer) { return 0; }
signed char CS_SetBufferSize(int len_ms) { return 0; }
signed char CS_SetHWND(void *hwnd) { return 0; }
signed char CS_SetMinHardwareChannels(int min) { return 0; }
signed char CS_SetMaxHardwareChannels(int max) { return 0; }
signed char CS_Init(int mixrate, int maxsoftwarechannels, unsigned int flags) { return 0; }
void CS_Close() {  }
void CS_Update() {  }
void CS_SetSpeakerMode(unsigned int speakermode) {  }
void CS_SetSFXMasterVolume(int volume) {  }
void CS_SetPanSeperation(float pansep) {  }
int CS_GetError() { return 0; }
float CS_GetVersion() { return 0.0f; }
int CS_GetOutput() { return 0; }
void * CS_GetOutputHandle() { return 0; }
int CS_GetDriver() { return 0; }
int CS_GetMixer() { return 0; }
int CS_GetNumDrivers() { return 0; }
signed char * CS_GetDriverName(int id) { return 0; }
signed char CS_GetDriverCaps(int id, unsigned int *caps) { return 0; }
int CS_GetOutputRate() { return 0; }
int CS_GetMaxChannels() { return 0; }
int CS_GetMaxSamples() { return 0; }
int CS_GetSFXMasterVolume() { return 0; }
int CS_GetNumHardwareChannels() { return 0; }
int CS_GetChannelsPlaying() { return 0; }
float CS_GetCPUUsage() { return 0.0f; }
void CS_GetMemoryStats(unsigned int *currentalloced, unsigned int *maxalloced) {  }
CS_SAMPLE * CS_Sample_Load(int index, const char *name_or_data, unsigned int mode, int memlength) { return 0; }
CS_SAMPLE * CS_Sample_Alloc(int index, int length, unsigned int mode, int deffreq, int defvol, int defpan, int defpri) { return 0; }
void CS_Sample_Free(CS_SAMPLE *sptr) {  }
signed char CS_Sample_Upload(CS_SAMPLE *sptr, void *srcdata, unsigned int mode) { return 0; }
signed char CS_Sample_Lock(CS_SAMPLE *sptr, int offset, int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2) { return 0; }
signed char CS_Sample_Unlock(CS_SAMPLE *sptr, void *ptr1, void *ptr2, unsigned int len1, unsigned int len2) { return 0; }
signed char CS_Sample_SetMode(CS_SAMPLE *sptr, unsigned int mode) { return 0; }
signed char CS_Sample_SetLoopPoints(CS_SAMPLE *sptr, int loopstart, int loopend) { return 0; }
signed char CS_Sample_SetDefaults(CS_SAMPLE *sptr, int deffreq, int defvol, int defpan, int defpri) { return 0; }
signed char CS_Sample_SetMinMaxDistance(CS_SAMPLE *sptr, float min, float max) { return 0; }
signed char CS_Sample_SetMaxPlaybacks(CS_SAMPLE *sptr, int max) { return 0; }
CS_SAMPLE * CS_Sample_Get(int sampno) { return 0; }
char * CS_Sample_GetName(CS_SAMPLE *sptr) { return 0; }
unsigned int CS_Sample_GetLength(CS_SAMPLE *sptr) { return 0; }
signed char CS_Sample_GetLoopPoints(CS_SAMPLE *sptr, int *loopstart, int *loopend) { return 0; }
signed char CS_Sample_GetDefaults(CS_SAMPLE *sptr, int *deffreq, int *defvol, int *defpan, int *defpri) { return 0; }
unsigned int CS_Sample_GetMode(CS_SAMPLE *sptr) { return 0; }
int CS_PlaySound(int channel, CS_SAMPLE *sptr) { return 0; }
int CS_PlaySoundEx(int channel, CS_SAMPLE *sptr, CS_DSPUNIT *dsp, signed char startpaused) { return 0; }
signed char CS_StopSound(int channel) { return 0; }
signed char CS_SetFrequency(int channel, int freq) { return 0; }
signed char CS_SetVolume(int channel, int vol) { return 0; }
signed char CS_SetVolumeAbsolute(int channel, int vol) { return 0; }
signed char CS_SetPan(int channel, int pan) { return 0; }
signed char CS_SetSurround(int channel, signed char surround) { return 0; }
signed char CS_SetMute(int channel, signed char mute) { return 0; }
signed char CS_SetPriority(int channel, int priority) { return 0; }
signed char CS_SetReserved(int channel, signed char reserved) { return 0; }
signed char CS_SetPaused(int channel, signed char paused) { return 0; }
signed char CS_SetLoopMode(int channel, unsigned int loopmode) { return 0; }
signed char CS_SetCurrentPosition(int channel, unsigned int offset) { return 0; }
signed char CS_IsPlaying(int channel) { return 0; }
int CS_GetFrequency(int channel) { return 0; }
int CS_GetVolume(int channel) { return 0; }
int CS_GetPan(int channel) { return 0; }
signed char CS_GetSurround(int channel) { return 0; }
signed char CS_GetMute(int channel) { return 0; }
int CS_GetPriority(int channel) { return 0; }
signed char CS_GetReserved(int channel) { return 0; }
signed char CS_GetPaused(int channel) { return 0; }
unsigned int CS_GetLoopMode(int channel) { return 0; }
unsigned int CS_GetCurrentPosition(int channel) { return 0; }
CS_SAMPLE * CS_GetCurrentSample(int channel) { return 0; }
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
void CS_3D_SetDistanceFactor(float scale) {  }
void CS_3D_SetRolloffFactor(float scale) {  }
signed char CS_3D_SetAttributes(int channel, float *pos, float *vel) { return 0; }
signed char CS_3D_GetAttributes(int channel, float *pos, float *vel) { return 0; }
void CS_3D_Listener_SetCurrent(int current, int numlisteners) {  }
void CS_3D_Listener_SetAttributes(float *pos, float *vel, float fx, float fy, float fz, float tx, float ty, float tz) {  }
void CS_3D_Listener_GetAttributes(float *pos, float *vel, float *fx, float *fy, float *fz, float *tx, float *ty, float *tz) {  }
signed char CS_Stream_SetBufferSize(int ms) { return 0; }
CS_STREAM * CS_Stream_OpenFile(const char *filename, unsigned int mode, int memlength) { return 0; }
CS_STREAM * CS_Stream_Create(CS_STREAMCALLBACK callback, int length, unsigned int mode, int samplerate, int userdata) { return 0; }
signed char CS_Stream_Close(CS_STREAM *stream) { return 0; }
int CS_Stream_Play(int channel, CS_STREAM *stream) { return 0; }
int CS_Stream_PlayEx(int channel, CS_STREAM *stream, CS_DSPUNIT *dsp, signed char startpaused) { return 0; }
signed char CS_Stream_Stop(CS_STREAM *stream) { return 0; }
int CS_Stream_GetOpenState(CS_STREAM *stream) { return 0; }
signed char CS_Stream_SetPosition(CS_STREAM *stream, unsigned int position) { return 0; }
unsigned int CS_Stream_GetPosition(CS_STREAM *stream) { return 0; }
signed char CS_Stream_SetTime(CS_STREAM *stream, int ms) { return 0; }
int CS_Stream_GetTime(CS_STREAM *stream) { return 0; }
int CS_Stream_GetLength(CS_STREAM *stream) { return 0; }
int CS_Stream_GetLengthMs(CS_STREAM *stream) { return 0; }
signed char CS_Stream_SetLoopPoints(CS_STREAM *stream, unsigned int loopstartpcm, unsigned int loopendpcm) { return 0; }
CS_SAMPLE * CS_Stream_GetSample(CS_STREAM *stream) { return 0; }
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
void CS_File_SetCallbacks(CS_OPENCALLBACK useropen, CS_CLOSECALLBACK userclose, CS_READCALLBACK userread, CS_SEEKCALLBACK userseek, CS_TELLCALLBACK usertell) {  }
