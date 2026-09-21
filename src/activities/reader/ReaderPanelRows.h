#pragma once

// The reader toolbar panel's bottom-sheet geometry, kept freestanding (no
// GfxRenderer, no UITheme, no Arduino) so host-tests/readersheet exercises THE
// SAME arithmetic the device runs instead of a copy of it. A copy is how this
// went wrong the first time: the sheet and the renderer each had their own idea
// of the row stride and nothing compared them.
//
// rowGap MUST be the gap the list will actually draw with --
// Screen::resolveListProps(props).rowGap -- and never the raw theme token.
// ListProps::rowGap defaults to the -1 sentinel, and the SDK resolves that to
// theme.listTouchRowGap on any touch board, so the two differ by 6px per row on
// every X4 Pro and Sticky while agreeing exactly on button-only boards. That
// disagreement left a six-row panel 30px shorter than its own contents and is
// card #546.
namespace readerpanel {

struct Geometry {
  int rows;         // whole rows the sheet is sized for
  int stride;       // per-row advance, gap included
  int sheetHeight;  // chrome + rows, with no trailing gap
};

inline Geometry panelGeometry(int safeHeight, int rowH, int rowGap, int chrome, int itemCount, int heightPercent,
                              int heightMaxPercent) {
  const int stride = rowH + rowGap;
  // A non-positive stride would divide by zero below. It cannot happen with the
  // shipped tokens, but the sheet is sized from theme values and a theme is
  // data: fail to one row rather than to a CPU exception on the device.
  if (stride <= 0) return Geometry{1, stride, chrome};
  const int target = (safeHeight * heightPercent) / 100;
  const int cap = (safeHeight * heightMaxPercent) / 100;
  int rows = (target - chrome + rowGap) / stride;
  if (chrome + (rows + 1) * stride - rowGap <= cap) ++rows;
  if (itemCount > 0 && rows > itemCount) rows = itemCount;
  if (rows < 1) rows = 1;
  return Geometry{rows, stride, chrome + rows * stride - rowGap};
}

// What `rows` rows actually occupy when the renderer draws them at `drawnGap`.
// The whole bug is this exceeding the list band panelGeometry() reserved.
inline int rowsDrawnHeight(int rows, int rowH, int drawnGap) {
  return rows <= 0 ? 0 : rows * (rowH + drawnGap) - drawnGap;
}

}  // namespace readerpanel
