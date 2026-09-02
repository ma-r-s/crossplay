// uzlib_adler32 / uzlib_crc32 for the host link.
//
// The vendored lib/uzlib ships only tinflate.c -- the upstream adler32.c and
// crc32.c are not in the tree. On the device that costs nothing: the only
// reference to them is uzlib_uncompress_chksum(), which nothing in this
// firmware calls (InflateReader.cpp uses uzlib_uncompress()), so the linker
// drops it. A plain host link has no dead-strip pass and fails instead.
//
// These ABORT rather than returning a plausible number. If a future change
// routes the font path through the checksummed entry point, the suite must say
// so loudly -- a stub quietly returning 0 would make a checksum mismatch look
// like a pass.
#include <stdio.h>
#include <stdlib.h>

unsigned int uzlib_adler32(const void *data, unsigned int length, unsigned int prev_sum) {
  (void)data;
  (void)length;
  (void)prev_sum;
  fprintf(stderr, "FAIL fontguard  uzlib_adler32 called: the font path now uses the checksummed uzlib entry point, "
                  "which this suite stubs out. Link the real adler32.c/crc32.c.\n");
  abort();
}

unsigned int uzlib_crc32(const void *data, unsigned int length, unsigned int crc) {
  (void)data;
  (void)length;
  (void)crc;
  fprintf(stderr, "FAIL fontguard  uzlib_crc32 called: the font path now uses the checksummed uzlib entry point, "
                  "which this suite stubs out. Link the real adler32.c/crc32.c.\n");
  abort();
}
