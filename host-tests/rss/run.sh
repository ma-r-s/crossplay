#!/bin/sh
# Builds and runs the RSS tests. No device and no PlatformIO: RssCore and
# RssFeed are freestanding C++17, and expat is plain C.
#
#   host-tests/rss/run.sh
#
# The include path is the standard library, lib/Utf8, lib/expat and
# lib/XmlParserUtils, which is the point: if the parsers or the formats ever
# reach for HttpDownloader, the SD card or Arduino, this build fails loudly
# instead of the logic quietly becoming device-only and untested.
#
# expat is built with the firmware's own flags (platformio.ini: XML_GE=0,
# XML_CONTEXT_BYTES=1024), so a feed that parses here parses the same way on the
# device -- in particular, no DTD-declared entity is ever expanded on either.
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name: two worktrees sharing one
# build dir would let one tree pass on a binary the other built.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-rss-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

EXPAT=../../lib/expat
for unit in xmlparse xmlrole xmltok; do
  # -w: expat is vendored C and its warnings are not this suite's business.
  "${CC:-cc}" -std=c99 -O2 -w -DXML_GE=0 -DXML_CONTEXT_BYTES=1024 -I$EXPAT -c "$EXPAT/$unit.c" -o "$BUILD_DIR/$unit.o"
done

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  -DXML_GE=0 -DXML_CONTEXT_BYTES=1024 \
  -I../../lib/Utf8 -I$EXPAT -I../../lib/XmlParserUtils \
  ../../lib/Utf8/Utf8.cpp \
  ../../src/apps_local/hackernews/HackerNewsSaved.cpp \
  ../../src/apps_local/rss/RssCore.cpp \
  ../../src/apps_local/rss/RssFeed.cpp \
  "$BUILD_DIR/xmlparse.o" "$BUILD_DIR/xmlrole.o" "$BUILD_DIR/xmltok.o" \
  test_rss.cpp -o "$BUILD_DIR/test_rss"
"$BUILD_DIR/test_rss"
