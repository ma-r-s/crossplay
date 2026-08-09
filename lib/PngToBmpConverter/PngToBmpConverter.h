#pragma once

#include <HalStorage.h>

class Print;

class PngToBmpConverter {
  static bool pngFileToBmpStreamInternal(HalFile& pngFile, Print& bmpOut, int targetWidth, int targetHeight,
                                         bool oneBit, bool crop = true);

 public:
  static bool pngFileToBmpStream(HalFile& pngFile, Print& bmpOut, bool crop = true);
  static bool pngFileToBmpStreamWithSize(HalFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
  static bool pngFileTo1BitBmpStreamWithSize(HalFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
  // Contain, not cover. `...WithSize` above crops to FILL the target box
  // despite the "Max" in its parameter names, so passing a generous height
  // bound scales the image UP to meet it: a normal comic asked to fit
  // 480x16384 came out 14845x16384. This one scales to fit INSIDE the box,
  // which is what a caller bounding a download actually wants.
  static bool pngFileTo1BitBmpStreamFitWithin(HalFile& pngFile, Print& bmpOut, int maxWidth, int maxHeight);
};
