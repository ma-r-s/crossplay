#pragma once

// How Home's menu fits under the cover tile (fork-local, host-tested in
// host-tests/homefit). Pure arithmetic, so the rule can be checked against
// every theme and row count without a panel.
//
// The tile keeps its height: three of the four list themes draw their cover at
// a fixed size whatever rect they are handed, so a shorter tile would put the
// art on top of the first row. The menu gives instead, in this order:
//   1. its gaps, down to kMinRowGap,
//   2. its rows, down to kMinRowHeight,
//   3. the bottom margin, down to kMinBottomMargin.
// The margin starts at the theme's side padding so the menu sits in the same
// frame as everything else. A trailing gap is counted because RoundedRaff pages
// its menu by rows * (height + gap): a menu that "fits" without it puts the
// last row on a second page nobody can see.
namespace homefit {

constexpr int kMinRowGap = 4;
constexpr int kMinRowHeight = 40;
constexpr int kMinBottomMargin = 12;

struct Input {
  int rows;         // menu rows, the shelf's folders included
  int rowHeight;    // the theme's own row height
  int rowGap;       // the theme's own gap
  int menuTop;      // y of the menu rect
  int leadIn;       // rows start this far below menuTop (Classic)
  int pageHeight;   // logical panel height
  int hintsHeight;  // button hints band, 0 on touch boards
  int sidePadding;  // the theme's content side padding: the margin to match
};

struct Fit {
  int rowHeight;
  int rowGap;
  int bottomMargin;
  int menuBottom;  // y the menu rect ends at
  bool fits;
};

inline int need(const int rows, const int rowHeight, const int rowGap) { return rows * (rowHeight + rowGap); }

inline Fit fit(const Input& in) {
  Fit f{in.rowHeight, in.rowGap, in.sidePadding > kMinBottomMargin ? in.sidePadding : kMinBottomMargin, 0, true};
  const auto space = [&in](const int margin) {
    return in.pageHeight - in.hintsHeight - margin - in.menuTop - in.leadIn;
  };
  int available = space(f.bottomMargin);
  if (in.rows > 0 && need(in.rows, f.rowHeight, f.rowGap) > available) {
    const int floorGap = in.rowGap < kMinRowGap ? in.rowGap : kMinRowGap;
    const int gap = available / in.rows - f.rowHeight;
    f.rowGap = gap > floorGap ? gap : floorGap;
    if (need(in.rows, f.rowHeight, f.rowGap) > available) {
      const int height = available / in.rows - f.rowGap;
      f.rowHeight = height > kMinRowHeight ? height : kMinRowHeight;
    }
    if (need(in.rows, f.rowHeight, f.rowGap) > available) {
      const int margin = f.bottomMargin - (need(in.rows, f.rowHeight, f.rowGap) - available);
      f.bottomMargin = margin > kMinBottomMargin ? margin : kMinBottomMargin;
      available = space(f.bottomMargin);
    }
    f.fits = need(in.rows, f.rowHeight, f.rowGap) <= available;
  }
  f.menuBottom = in.pageHeight - in.hintsHeight - f.bottomMargin;
  return f;
}

}  // namespace homefit
