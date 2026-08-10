/* Vita: vitasdk ships prebuilt libogg.a/libvorbis*.a but not their headers
   (verified absent under $VITASDK/arm-vita-eabi/include), and per
   CrySoundSystem/OGGDecoderStubs.cpp's own comment those prebuilt libs are
   soft-float (ABI-incompatible with this hard-float build) anyway, so real
   OGG decoding is intentionally stubbed, not linked against the real libs.
   This header supplies just the one real, standard libogg type
   CrySoundSystem/OGGDecoder.h needs to compile -- taken from libogg's
   actual public API, not fabricated. */
#ifndef OGG_OGG_H_VITA_COMPAT
#define OGG_OGG_H_VITA_COMPAT

#include <stdint.h>

typedef int64_t ogg_int64_t;

#endif
