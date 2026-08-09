#pragma once

// Host-only stub, for tools_local/xkcd/convert.cpp.
//
// lib/GfxRenderer/BitmapHelpers.cpp is where this fork's 1-bit Atkinson
// ditherer actually lives, and the pack builder has to use *that* code rather
// than a port of it: the device converts newly downloaded comics itself, so a
// second implementation on the host would mean comics from the pack and comics
// from wifi are dithered differently, on the same screen, with nothing to say
// why.
//
// BitmapHelpers.cpp pulls in Bitmap.h for createBmpHeader(), which the pack
// builder never calls, and Bitmap.h includes <HalStorage.h> only to name
// HalFile in signatures. A forward declaration is the whole dependency.
//
// This is a stub for the *tool*, not for the firmware. Nothing here is
// compiled into anything that runs on the device.

class HalFile;
