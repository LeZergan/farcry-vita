/* libvorbis/libogg: vitasdk ships prebuilt libvorbis.a/libogg.a, but
   they're built soft-float while this engine build is hard-float
   (Tag_ABI_VFP_args mismatch at link time -- "uses VFP register
   arguments" vs "does not"). Building real libvorbis/libogg from
   source with matching flags was the alternative, but per explicit
   direction the priority right now is getting the engine to link and
   render, not working OGG music playback. Honest stubs: every call
   fails cleanly (matching each function's real error-return convention)
   rather than pretending to succeed. OGGDecoder.cpp's own OGG loading
   will simply fail to open/read any file. See
   engine_port/compat/README.md. */
#include "StdAfx.h"
#include <vorbis/vorbisfile.h>

extern "C" {

int ov_clear(OggVorbis_File * /*vf*/)
{
	return 0;
}

int ov_open_callbacks(void * /*datasource*/, OggVorbis_File * /*vf*/,
		char * /*initial*/, long /*ibytes*/, ov_callbacks /*callbacks*/)
{
	return -1;
}

long ov_seekable(OggVorbis_File * /*vf*/)
{
	return 0;
}

ogg_int64_t ov_pcm_total(OggVorbis_File * /*vf*/, int /*i*/)
{
	return 0;
}

int ov_pcm_seek(OggVorbis_File * /*vf*/, ogg_int64_t /*pos*/)
{
	return -1;
}

vorbis_info *ov_info(OggVorbis_File * /*vf*/, int /*link*/)
{
	return NULL;
}

vorbis_comment *ov_comment(OggVorbis_File * /*vf*/, int /*link*/)
{
	return NULL;
}

long ov_read(OggVorbis_File * /*vf*/, char * /*buffer*/, int /*length*/,
		int /*bigendianp*/, int /*word*/, int /*sgned*/, int * /*bitstream*/)
{
	return 0;
}

}
