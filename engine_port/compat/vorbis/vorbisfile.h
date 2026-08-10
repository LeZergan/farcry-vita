/* Vita: see engine_port/compat/ogg/ogg.h for why this exists -- real
   libvorbis headers aren't shipped by vitasdk, and the prebuilt .a's are
   ABI-incompatible anyway, so OGG decoding is intentionally stubbed (see
   CrySoundSystem/OGGDecoderStubs.cpp). The types/signatures below match
   libvorbis's real, standard public API exactly (fields CryEngine's own
   COGGDecoder actually reads: vorbis_info::channels/rate/bitrate_nominal,
   vorbis_comment::user_comments/vendor, ov_callbacks' 4 function pointers)
   -- this is a real API shape, not a fabricated one. OggVorbis_File is
   opaque here: the real struct is a large decoder-internal type, but every
   ov_* call against it is stubbed (see OGGDecoderStubs.cpp) to never
   actually dereference its contents, so reserving anonymous storage is
   enough -- it just needs a stable address to pass around. */
#ifndef VORBIS_VORBISFILE_H_VITA_COMPAT
#define VORBIS_VORBISFILE_H_VITA_COMPAT

#include <ogg/ogg.h>
#include <stddef.h>

/* Real libvorbis error codes (public API, OV_* from vorbis/codec.h /
   vorbisfile.h) -- COGGDecoder checks for these by name even though the
   stubbed ov_* functions below only ever actually return -1/0. */
#define OV_FALSE      -1
#define OV_EOF        -2
#define OV_HOLE       -3
#define OV_EREAD      -128
#define OV_EFAULT     -129
#define OV_EIMPL      -130
#define OV_EINVAL     -131
#define OV_ENOTVORBIS -132
#define OV_EBADHEADER -133
#define OV_EVERSION   -134
#define OV_ENOTAUDIO  -135
#define OV_EBADPACKET -136
#define OV_EBADLINK   -137
#define OV_ENOSEEK    -138

typedef struct vorbis_info {
	int version;
	int channels;
	long rate;
	long bitrate_upper;
	long bitrate_nominal;
	long bitrate_lower;
	long bitrate_window;
	void *codec_setup;
} vorbis_info;

typedef struct vorbis_comment {
	char **user_comments;
	int *comment_lengths;
	int comments;
	char *vendor;
} vorbis_comment;

typedef struct {
	size_t (*read_func)(void *ptr, size_t size, size_t nmemb, void *datasource);
	int (*seek_func)(void *datasource, ogg_int64_t offset, int whence);
	int (*close_func)(void *datasource);
	long (*tell_func)(void *datasource);
} ov_callbacks;

/* Opaque -- see file header comment. Sized generously; never actually
   dereferenced by the stubbed ov_* functions. */
typedef struct OggVorbis_File {
	unsigned char _opaque[256];
} OggVorbis_File;

#ifdef __cplusplus
extern "C" {
#endif

int ov_clear(OggVorbis_File *vf);
int ov_open_callbacks(void *datasource, OggVorbis_File *vf, char *initial, long ibytes, ov_callbacks callbacks);
long ov_seekable(OggVorbis_File *vf);
ogg_int64_t ov_pcm_total(OggVorbis_File *vf, int i);
int ov_pcm_seek(OggVorbis_File *vf, ogg_int64_t pos);
vorbis_info *ov_info(OggVorbis_File *vf, int link);
vorbis_comment *ov_comment(OggVorbis_File *vf, int link);
long ov_read(OggVorbis_File *vf, char *buffer, int length, int bigendianp, int word, int sgned, int *bitstream);

#ifdef __cplusplus
}
#endif

#endif
