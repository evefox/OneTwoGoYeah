/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <mad.h>
#include <ogg/ogg.h>
#include <vorbis/codec.h>

#include <stdio.h>
#include <string.h>

int main(void) {
  ogg_sync_state sync_state;

  if (strcmp(mad_version, "MPEG Audio Decoder 0.15.1 (beta)") != 0) {
    fprintf(stderr, "unexpected libmad snapshot: %s\n", mad_version);
    return 1;
  }
  if (strcmp(mad_build, "FPM_64BIT ASO_ZEROCHECK ") != 0) {
    fprintf(stderr, "unexpected libmad build configuration: %s\n", mad_build);
    return 1;
  }

  if (strcmp(vorbis_version_string(), "Xiph.Org libVorbis 1.3.5") != 0) {
    fprintf(stderr, "unexpected libVorbis snapshot: %s\n",
            vorbis_version_string());
    return 1;
  }

  if (ogg_sync_init(&sync_state) != 0) {
    fprintf(stderr, "libogg snapshot failed to initialize\n");
    return 1;
  }
  if (ogg_sync_clear(&sync_state) != 0) {
    fprintf(stderr, "libogg snapshot failed to clear\n");
    return 1;
  }

  return 0;
}
