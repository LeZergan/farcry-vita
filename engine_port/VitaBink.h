/* Public surface of the vendored libbinkdec decoder (third_party/libbinkdec),
   declared separately so CryGame does not have to include BinkDecoder.h.

   That header also defines an internal "struct Plane", which collides with
   CryEngine's own Plane type the moment it reaches any CryGame translation
   unit.  The library is kept pristine -- no local edits to vendored code, so it
   stays trivially updatable -- and this mirrors only the exported interface,
   which BinkDecoder.h marks as the exportable part. */
#ifndef VITA_BINK_H
#define VITA_BINK_H

#include <stdint.h>

struct BinkHandle
{
	bool isValid;
	int  instanceIndex;
};

struct AudioInfo
{
	uint32_t sampleRate;
	uint32_t nChannels;
	uint32_t idealBufferSize;
};

struct ImagePlane
{
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint8_t *data;
};

typedef ImagePlane YUVbuffer[3];

BinkHandle Bink_Open(const char *fileName);
void       Bink_Close(BinkHandle &handle);
uint32_t   Bink_GetNumAudioTracks(BinkHandle &handle);
void       Bink_GetFrameSize(BinkHandle &handle, uint32_t &width, uint32_t &height);
AudioInfo  Bink_GetAudioTrackDetails(BinkHandle &handle, uint32_t trackIndex);
uint32_t   Bink_GetAudioData(BinkHandle &handle, uint32_t trackIndex, int16_t *data);
float      Bink_GetFrameRate(BinkHandle &handle);
uint32_t   Bink_GetNumFrames(BinkHandle &handle);
uint32_t   Bink_GetCurrentFrameNum(BinkHandle &handle);
uint32_t   Bink_GetNextFrame(BinkHandle &handle, YUVbuffer yuv);
void       Bink_GotoFrame(BinkHandle &handle, uint32_t frameNum);

#endif // VITA_BINK_H
