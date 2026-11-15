#ifndef ZHAOXU_FTOPTION_ZOS_H
#define ZHAOXU_FTOPTION_ZOS_H

/* Start from the unmodified upstream option set, then remove the external
 * codecs, environment probing and file-stream layer that ZOS does not have.
 * Faces are always opened from memory with FT_New_Memory_Face. */
#include <freetype/config/ftoption.h>

#undef FT_CONFIG_OPTION_USE_ZLIB
#undef FT_CONFIG_OPTION_USE_BZIP2
#undef FT_CONFIG_OPTION_USE_PNG
#undef FT_CONFIG_OPTION_USE_HARFBUZZ
#undef FT_CONFIG_OPTION_USE_BROTLI
#undef FT_CONFIG_OPTION_SYSTEM_ZLIB
#undef FT_CONFIG_OPTION_ENVIRONMENT_PROPERTIES

#define FT_CONFIG_OPTION_DISABLE_STREAM_SUPPORT

#endif
