#!/bin/sh
# Builds and runs the note-format tests. NotesCore is freestanding C++17.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-notes-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/notes
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC test_notes.cpp $SRC/NotesCore.cpp -o "$BUILD_DIR/test_notes"
"$BUILD_DIR/test_notes"
