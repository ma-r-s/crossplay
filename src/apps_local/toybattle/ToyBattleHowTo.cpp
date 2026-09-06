#include <cstdio>

#include "../ui/ToyboxFormat.h"
#include "ToyBattleMenus.h"
#include "ToyBattleScreens.h"

// HOW TO PLAY, in three treatments, built at once so they can be photographed
// side by side and one chosen. An option described in prose gets judged on the
// prose; an option rendered at native size gets judged.
//
// What the deck they replace got wrong, all of it visible in one screenshot per
// page rather than deduced:
//
//   * Six of its seven pages drew the SAME empty Castle Field lattice. The
//     picture occupied 70% of the panel and carried no information at all: no
//     troops, no ownership, no path. The three "spotlight" brackets on THE LINE
//     HOME sat on three unconnected nodes, so the one page whose whole subject
//     is a connected walk showed three dots and no walk.
//   * The caption box was a fixed 132px against a 45px line, so any caption
//     needing four lines drew straight over the PREV/PLAY buttons. SPECIAL
//     BASES did, and shipped that way.
//   * THE TROOPS drew each numeral in a 30px box inside a 78px tile and then
//     put the mark at y+56. The display cut is taller than 30, so the numeral
//     overflowed its box downwards and every mark landed on top of its own
//     number -- eight collisions on one screen.
//
// So the shared parts here are not styling. They are the two things that were
// actually missing: a diagram that can hold a POSITION, and a caption that is
// measured before it is placed.

namespace tbui {
namespace {

namespace tb = toybattle;

void chrome(toybox::Screen& screen, const char* title, const char* rightLabel = "") {
  fui::HeaderProps header;
  header.title = title;
  header.rightLabel = rightLabel;
  header.subtitleText = fui::TextStyle{};
  header.subtitleText.font = toybox::kUiFont;
  header.subtitleText.color = fui::Color::White;
  header.subtitleText.align = fui::TextAlign::Right;
  // The counter is in the band too, and the title's room is what is left of
  // it -- which headerBand now works out from the component's own arithmetic
  // rather than from this sum. The sum was wrong in a way nothing showed: the
  // component ALREADY subtracts the right label's width, so reserving it here
  // as well charged the title for the counter twice.
  header.borderEdges = fui::EdgesNone;
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);
  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

fui::TextStyle styled(const fui::FontId font, const fui::TextAlign align, const uint8_t lines = 1) {
  fui::TextStyle style;
  style.font = font;
  style.align = align;
  style.maxLines = lines;
  return style;
}

// Text centred on the box rather than on the box's top edge. `text()` centres a
// line BLOCK inside its rect, so a one-line rect shorter than the line height
// pushes the ink out of both ends; this asks the target how tall a line is and
// hands it a rect that size.
void centred(toybox::Screen& screen, const fui::Rect box, const char* text, const fui::FontId font, const bool white) {
  fui::TextStyle style = styled(font, fui::TextAlign::Center);
  style.color = white ? fui::Color::White : fui::Color::Black;
  const int16_t h = screen.target().lineHeight(font);
  screen.target().text(fui::makeRect(box.x, static_cast<int16_t>(box.y + (box.height - h) / 2), box.width, h), text,
                       style);
}

// ---------------------------------------------------------------------------
// Teaching diagrams
// ---------------------------------------------------------------------------
//
// A Scene is a handful of slots, the links between them, and what is standing
// on each -- in a 0..1000 box, so one table draws at 400px on a rules page and
// at 56px in a reference row. It is the piece the old deck did not have: its
// picture came from `miniBoard`, which takes a Terrain and draws the SHAPE of a
// map. A rule about ownership, covering or reach cannot be drawn by a function
// that has no way to say who holds what.
//
// Everything below is the board's own vocabulary at another size, not a second
// set of symbols to learn: dithered ground, black edge, yours knocked out of a
// black plate, theirs sitting on the ground, an H.Q. wearing a heavier edge.

enum : uint8_t {
  MarkNone = 0,
  MarkTick = 1 << 0,   // this is allowed
  MarkCross = 1 << 1,  // this is not
  MarkRing = 1 << 2,   // look here
};

enum : uint8_t { Yours = 0, Theirs = 1, Nobody = 2 };

struct Node {
  int16_t x, y;
  // ' ' an empty base, '*' or '1'..'7' a troop standing on it, 'H' your H.Q.,
  // 'E' theirs.
  char face;
  uint8_t owner;
  uint8_t mark;
  tb::Special badge;
};

struct Link {
  uint8_t a, b;
  // A held step is drawn at the board's own path weight; the rest hairline. The
  // difference is the whole subject of THE LINE HOME.
  bool held;
};

struct Medals {
  int16_t x, y;
  uint8_t count;
  uint8_t mark;
};

struct Scene {
  const Node* nodes;
  uint8_t nodeCount;
  const Link* links;
  uint8_t linkCount;
  const Medals* medals;
  uint8_t medalCount;
};

// A verdict, on a badge.
//
// It cannot be a bare tick or a bare cross: a bare X is already troop 3's mark
// on the card page, so the same glyph at the same weight meant "forbidden" on
// one page and "removes a neighbour" on another. A verdict rides a filled disc,
// which nothing on a board does, and the white ring keeps it off whatever line
// or dither it lands on.
void verdictMark(toybox::Screen& screen, const fui::Point at, const int16_t size, const bool yes) {
  const int16_t r = static_cast<int16_t>(size / 2);
  const int16_t h = static_cast<int16_t>(size / 4);
  const fui::Paint ink = fui::Paint::solid(fui::Color::White);
  toybox::disc(screen, at.x, at.y, static_cast<int16_t>(r + 2), fui::Color::White);
  toybox::disc(screen, at.x, at.y, r, fui::Color::Black);
  if (yes) {
    screen.target().line(fui::Point{static_cast<int16_t>(at.x - h), at.y},
                         fui::Point{static_cast<int16_t>(at.x - h / 3), static_cast<int16_t>(at.y + h)}, 3, ink);
    screen.target().line(fui::Point{static_cast<int16_t>(at.x - h / 3), static_cast<int16_t>(at.y + h)},
                         fui::Point{static_cast<int16_t>(at.x + h), static_cast<int16_t>(at.y - h)}, 3, ink);
    return;
  }
  screen.target().line(fui::Point{static_cast<int16_t>(at.x - h), static_cast<int16_t>(at.y - h)},
                       fui::Point{static_cast<int16_t>(at.x + h), static_cast<int16_t>(at.y + h)}, 3, ink);
  screen.target().line(fui::Point{static_cast<int16_t>(at.x + h), static_cast<int16_t>(at.y - h)},
                       fui::Point{static_cast<int16_t>(at.x - h), static_cast<int16_t>(at.y + h)}, 3, ink);
}

// Medals as the board draws them: filled pips on a knocked-out plate.
void verdictMark(toybox::Screen& screen, fui::Point at, int16_t size, bool yes);

void medalCluster(toybox::Screen& screen, const fui::Point at, const int count, const int16_t pipR,
                  const uint8_t mark) {
  const int16_t span = static_cast<int16_t>(count * (pipR * 2 + 3) - 3);
  const fui::Rect plate =
      fui::makeRect(static_cast<int16_t>(at.x - span / 2 - 5), static_cast<int16_t>(at.y - pipR - 4),
                    static_cast<int16_t>(span + 10), static_cast<int16_t>(pipR * 2 + 8));
  screen.target().fill(plate, fui::Paint::solid(fui::Color::White), 8);
  for (int i = 0; i < count; ++i) {
    toybox::disc(screen, static_cast<int16_t>(at.x - span / 2 + pipR + i * (pipR * 2 + 3)), at.y, pipR,
                 fui::Color::Black);
  }
  if (mark & MarkRing) {
    toybox::bracket(screen,
                    fui::makeRect(static_cast<int16_t>(plate.x - 4), static_cast<int16_t>(plate.y - 4),
                                  static_cast<int16_t>(plate.width + 8), static_cast<int16_t>(plate.height + 8)),
                    7, 3);
  }
  if (mark & (MarkTick | MarkCross)) {
    verdictMark(screen, fui::Point{static_cast<int16_t>(plate.right() + pipR), at.y}, static_cast<int16_t>(pipR * 3),
                (mark & MarkTick) != 0);
  }
}

void drawNode(toybox::Screen& screen, const fui::Point at, const int16_t size, const Node& node) {
  const int16_t half = static_cast<int16_t>(size / 2);
  const fui::Rect box = fui::makeRect(static_cast<int16_t>(at.x - half), static_cast<int16_t>(at.y - half), size, size);
  const bool hq = node.face == 'H' || node.face == 'E';
  // Silhouette says "there is a rule about what may land here", badge says
  // "this base does something after you land" -- the board's split, kept, so a
  // gate does not come out of the rules screen looking like an effect.
  const bool restricts = node.badge == tb::Special::Gate || node.badge == tb::Special::Nullify;
  const uint8_t corner = restricts ? 0 : static_cast<uint8_t>(size / 6);
  const bool empty = node.owner == Nobody;

  screen.target().fill(box, fui::Paint::dither(empty ? fui::Color::LightGray : fui::Color::DarkGray), corner);
  screen.target().stroke(box, fui::Paint::solid(fui::Color::Black),
                         static_cast<uint8_t>(hq ? 5 : (node.badge != tb::Special::None ? 5 : 3)), corner);

  const fui::FontId face = size >= 44 ? toybox::kUiFont : toybox::kTileFont;
  // A black glyph on a 50% checkerboard survives at the board's 52px slot and
  // does not at a 40px reference thumbnail: the stems and the dither interlock,
  // and an H.Q.'s crossbar simply goes. So every value that is NOT knocked out
  // of a black plate gets a white one to sit on -- the same "a value you must
  // read rides a white plate" the gate tabs and the medal clusters use.
  const auto plate = [&]() {
    const int16_t inset = static_cast<int16_t>(size / 5);
    screen.target().fill(fui::makeRect(static_cast<int16_t>(box.x + inset), static_cast<int16_t>(box.y + inset),
                                       static_cast<int16_t>(size - inset * 2), static_cast<int16_t>(size - inset * 2)),
                         fui::Paint::solid(fui::Color::White), static_cast<uint8_t>(corner ? corner - 1 : 0));
  };
  if (hq) {
    plate();
    centred(screen, box, node.face == 'H' ? "H" : "E", face, false);
  } else if (node.face != ' ') {
    // Yours is knocked out of a black plate, theirs stands on the ground. The
    // same inversion the board uses, so no third convention is invented here.
    if (node.owner == Yours) {
      // Flush to the border. Inset by an eighth it left a one-pixel ring of the
      // ground's checkerboard between the two, which at 40px is not a margin
      // but a fringe -- and it pushed a 6 towards reading as an 8.
      screen.target().fill(box, fui::Paint::solid(fui::Color::Black), corner);
    } else {
      plate();
    }
    const char pip[2] = {node.face, '\0'};
    centred(screen, box, pip, face, node.owner == Yours);
  }

  if (node.badge != tb::Special::None && !restricts) {
    const int16_t r = static_cast<int16_t>(size / 5 + 2);
    const fui::Point badgeAt{static_cast<int16_t>(box.right() - 2), static_cast<int16_t>(box.y + 2)};
    // Badge and plate are both solid black; touching, they fuse into one lump
    // and eat the corner -- which is the very thing the silhouette says.
    toybox::disc(screen, badgeAt.x, badgeAt.y, static_cast<int16_t>(r + 2), fui::Color::White);
    toybox::disc(screen, badgeAt.x, badgeAt.y, r, fui::Color::Black);
    drawSpecialGlyph(screen, badgeAt, static_cast<int16_t>(r + 1), node.badge, true);
  }

  if (node.mark & MarkRing) {
    const int16_t arm = static_cast<int16_t>(half + 6);
    toybox::bracket(screen,
                    fui::makeRect(static_cast<int16_t>(at.x - arm), static_cast<int16_t>(at.y - arm),
                                  static_cast<int16_t>(arm * 2), static_cast<int16_t>(arm * 2)),
                    7, 3);
  }
  // Verdicts hang off the corner rather than over the face: the number is the
  // thing being judged and covering it defeats the picture.
  const int16_t clear = static_cast<int16_t>(size / 6);
  const fui::Point verdict{static_cast<int16_t>(box.right() + clear), static_cast<int16_t>(box.bottom() + clear)};
  if (node.mark & MarkTick) verdictMark(screen, verdict, static_cast<int16_t>(size / 2), true);
  if (node.mark & MarkCross) verdictMark(screen, verdict, static_cast<int16_t>(size / 2), false);
}

// The scene, fitted to `box`. Nodes are inset by their own radius plus the
// decoration that hangs outside them, so a spotlight or a verdict never leaves
// the box it was given.
// What a scene needs OUTSIDE its nodes: the widest of the spotlight bracket and
// the verdict badge. Exposed so that two panels of a minimal pair can lay out
// against the same inset -- derived per-panel, the one wearing a cross drew
// inside a smaller box, and the pair then differed in scale, position and
// spacing as well as in the one thing it was supposed to be about.
int16_t sceneInset(const Scene& scene, const int16_t node) {
  int16_t extra = 3;
  for (int i = 0; i < scene.nodeCount; ++i) {
    const uint8_t mark = scene.nodes[i].mark;
    if ((mark & MarkRing) && extra < node / 6 + 8) extra = static_cast<int16_t>(node / 6 + 8);
    if ((mark & (MarkTick | MarkCross)) && extra < node / 6 + node / 4 + 4) {
      extra = static_cast<int16_t>(node / 6 + node / 4 + 4);
    }
  }
  return extra;
}

void drawScene(toybox::Screen& screen, const fui::Rect& box, const Scene& scene, const int16_t node,
               const int16_t forcedExtra = -1) {
  const int16_t extra = forcedExtra >= 0 ? forcedExtra : sceneInset(scene, node);
  const int16_t pad = static_cast<int16_t>(node / 2 + extra);
  const int16_t usableW = static_cast<int16_t>(box.width - pad * 2);
  const int16_t usableH = static_cast<int16_t>(box.height - pad * 2);
  if (usableW <= 0 || usableH <= 0) return;
  const auto at = [&](const int16_t nx, const int16_t ny) {
    return fui::Point{static_cast<int16_t>(box.x + pad + static_cast<int32_t>(nx) * usableW / 1000),
                      static_cast<int16_t>(box.y + pad + static_cast<int32_t>(ny) * usableH / 1000)};
  };

  for (int i = 0; i < scene.linkCount; ++i) {
    const Link& link = scene.links[i];
    const Node& a = scene.nodes[link.a];
    const Node& b = scene.nodes[link.b];
    screen.target().line(at(a.x, a.y), at(b.x, b.y), link.held ? 7 : 2, fui::Paint::solid(fui::Color::Black));
  }
  for (int i = 0; i < scene.medalCount; ++i) {
    const Medals& m = scene.medals[i];
    medalCluster(screen, at(m.x, m.y), m.count, static_cast<int16_t>(node / 6 + 2), m.mark);
  }
  for (int i = 0; i < scene.nodeCount; ++i) {
    const Node& n = scene.nodes[i];
    drawNode(screen, at(n.x, n.y), node, n);
  }
}

// ---------------------------------------------------------------------------
// The page frame
// ---------------------------------------------------------------------------

// Two pills, relabelling rather than disappearing, and the body left over.
//
// The caption is MEASURED and the picture gets what is left, which is the whole
// fix for a caption that used to be given a fixed 132px and drew over the
// buttons when it needed more. A layout that cannot overflow beats a layout
// checked for overflow: this one has no number in it to get wrong.
struct PageFrame {
  fui::Rect picture;
  fui::Rect caption;
};

PageFrame pageFrame(toybox::Screen& screen, const char* caption, const fui::TextStyle& style, const int page,
                    const int pages, const int16_t captionBand = 0) {
  const fui::Rect actions = screen.takeBottom(toybox::kPillHeight, toybox::kGutter);
  const int16_t width = static_cast<int16_t>((actions.width - toybox::kGutter) / 2);
  const char* labels[2] = {page == 0 ? "BACK" : "PREV", page + 1 == pages ? "PLAY" : "NEXT"};
  const fui::ActionId ids[2] = {ActionPagePrev, ActionPageNext};
  for (int i = 0; i < 2; ++i) {
    fui::ButtonProps props;
    props.label = labels[i];
    props.action = ids[i];
    const bool forward = i == 1;
    props.text = forward ? styled(toybox::kUiFont, fui::TextAlign::Center) : toybox::buttonText(screen.theme());
    if (forward) props.text.color = fui::Color::White;
    props.styles = forward ? toybox::invertedStyles() : toybox::rowStyles();
    props.radius = toybox::kPillRadius;
    screen.button(props, fui::makeRect(static_cast<int16_t>(actions.x + i * (width + toybox::kGutter)), actions.y,
                                       width, actions.height));
  }

  const fui::Rect body = screen.body();
  PageFrame frame;
  if (caption == nullptr || caption[0] == '\0') {
    frame.picture = body;
    frame.caption = fui::makeRect(body.x, body.bottom(), body.width, 0);
    return frame;
  }
  int16_t tall = fui::measureWrappedText(screen.target(), caption, style, body.width).height;
  // A caller reserving one band for a whole deck passes it in; it is still a
  // measurement, just taken over every page instead of this one.
  if (captionBand > tall) tall = captionBand;
  frame.caption = fui::makeRect(body.x, static_cast<int16_t>(body.bottom() - tall), body.width, tall);
  frame.picture = fui::makeRect(body.x, body.y, body.width, static_cast<int16_t>(body.height - tall - toybox::kMargin));
  return frame;
}

fui::TextStyle captionStyle() {
  fui::TextStyle style = styled(toybox::kUiFont, fui::TextAlign::Center, 4);
  return style;
}

// ===========================================================================
// A -- THE WALKTHROUGH
// ===========================================================================
//
// One position, advanced a move at a time. The board is the same eight slots on
// every page, so the deck is a game happening rather than seven diagrams, and
// each page's picture differs from the last by exactly the thing the page is
// about.
//
// Six bases and two H.Q., not a real terrain: a printed map is fifteen bases
// and every rule here needs three or four. Its shape is chosen so that one
// region, one cut-off base and one covering all fit on it at once.

constexpr int kWalkNodes = 8;
constexpr int kWalkLinkCount = 13;

constexpr Link kWalkLinks[] = {
    {0, 1, false}, {0, 2, false}, {0, 3, false}, {1, 2, false}, {2, 3, false}, {1, 4, false}, {2, 4, false},
    {2, 5, false}, {3, 5, false}, {4, 5, false}, {4, 6, false}, {5, 6, false}, {6, 7, false},
};

// Held steps, for the page whose subject is the walk itself.
constexpr Link kWalkLinksHeld[] = {
    {0, 1, false}, {0, 2, false}, {0, 3, false}, {1, 2, false}, {2, 3, false}, {1, 4, false}, {2, 4, false},
    {2, 5, false}, {3, 5, false}, {4, 5, false}, {4, 6, true},  {5, 6, false}, {6, 7, true},
};

// The eight slots, in the one order every page below repeats: their H.Q., a top
// row of three, a middle pair, the base outside your door, your H.Q.
//
//        0 E
//    1       2       3
//        4       5
//            6
//            7 H
constexpr Node kGoalNodes[] = {
    {500, 30, 'E', Theirs, MarkRing, tb::Special::None},  {30, 250, ' ', Nobody, MarkNone, tb::Special::None},
    {500, 250, ' ', Nobody, MarkNone, tb::Special::None}, {970, 250, ' ', Nobody, MarkNone, tb::Special::None},
    {215, 560, ' ', Nobody, MarkNone, tb::Special::None}, {785, 560, ' ', Nobody, MarkNone, tb::Special::None},
    {500, 790, ' ', Nobody, MarkNone, tb::Special::None}, {500, 965, 'H', Yours, MarkRing, tb::Special::None},
};
constexpr Medals kGoalMedals[] = {{500, 430, 2, MarkRing}};

constexpr Node kTurnNodes[] = {
    {500, 30, 'E', Theirs, MarkNone, tb::Special::None},  {30, 250, ' ', Nobody, MarkNone, tb::Special::None},
    {500, 250, ' ', Nobody, MarkNone, tb::Special::None}, {970, 250, ' ', Nobody, MarkNone, tb::Special::None},
    {215, 560, ' ', Nobody, MarkNone, tb::Special::None}, {785, 560, ' ', Nobody, MarkNone, tb::Special::None},
    {500, 790, '4', Yours, MarkRing, tb::Special::None},  {500, 965, 'H', Yours, MarkNone, tb::Special::None},
};
constexpr Medals kPlainMedals[] = {{500, 430, 2, MarkNone}};

constexpr Node kReachNodes[] = {
    {500, 30, 'E', Theirs, MarkNone, tb::Special::None},   {30, 250, ' ', Nobody, MarkCross, tb::Special::None},
    {500, 250, ' ', Nobody, MarkCross, tb::Special::None}, {970, 250, ' ', Nobody, MarkCross, tb::Special::None},
    {215, 560, ' ', Nobody, MarkTick, tb::Special::None},  {785, 560, ' ', Nobody, MarkTick, tb::Special::None},
    {500, 790, '4', Yours, MarkNone, tb::Special::None},   {500, 965, 'H', Yours, MarkNone, tb::Special::None},
};

constexpr Node kCoverNodes[] = {
    {500, 30, 'E', Theirs, MarkNone, tb::Special::None},   {30, 250, ' ', Nobody, MarkNone, tb::Special::None},
    {500, 250, '4', Theirs, MarkNone, tb::Special::None},  {970, 250, ' ', Nobody, MarkNone, tb::Special::None},
    {215, 560, '6', Theirs, MarkCross, tb::Special::None}, {785, 560, '3', Theirs, MarkTick, tb::Special::None},
    {500, 790, '5', Yours, MarkNone, tb::Special::None},   {500, 965, 'H', Yours, MarkNone, tb::Special::None},
};

constexpr Node kChainNodes[] = {
    {500, 30, 'E', Theirs, MarkNone, tb::Special::None},  {30, 250, ' ', Nobody, MarkNone, tb::Special::None},
    {500, 250, '7', Theirs, MarkNone, tb::Special::None}, {970, 250, '2', Yours, MarkCross, tb::Special::None},
    {215, 560, '5', Yours, MarkNone, tb::Special::None},  {785, 560, '6', Theirs, MarkNone, tb::Special::None},
    {500, 790, '4', Yours, MarkNone, tb::Special::None},  {500, 965, 'H', Yours, MarkNone, tb::Special::None},
};

constexpr Node kRegionNodes[] = {
    {500, 30, 'E', Theirs, MarkNone, tb::Special::None}, {30, 250, ' ', Nobody, MarkNone, tb::Special::None},
    {500, 250, '6', Yours, MarkNone, tb::Special::None}, {970, 250, ' ', Nobody, MarkNone, tb::Special::None},
    {215, 560, '5', Yours, MarkNone, tb::Special::None}, {785, 560, '3', Yours, MarkNone, tb::Special::None},
    {500, 790, '4', Yours, MarkNone, tb::Special::None}, {500, 965, 'H', Yours, MarkNone, tb::Special::None},
};
constexpr Medals kRegionMedals[] = {{500, 430, 2, MarkRing}};

constexpr Node kSpecialNodes[] = {
    {500, 30, 'E', Theirs, MarkNone, tb::Special::None},  {30, 250, ' ', Nobody, MarkNone, tb::Special::None},
    {500, 250, ' ', Nobody, MarkRing, tb::Special::Draw}, {970, 250, ' ', Nobody, MarkNone, tb::Special::None},
    {215, 560, ' ', Nobody, MarkNone, tb::Special::None}, {785, 560, ' ', Nobody, MarkRing, tb::Special::Recall},
    {500, 790, ' ', Nobody, MarkNone, tb::Special::None}, {500, 965, 'H', Yours, MarkNone, tb::Special::None},
};

struct WalkPage {
  const char* title;
  // The one page whose subject IS a base that has lost its walk home.
  bool showsCut;
  const char* caption;
  const Node* nodes;
  const Link* links;
  const Medals* medals;
  uint8_t medalCount;
  bool troops;
};

constexpr WalkPage kWalkPages[] = {
    {"THE GOAL", false, "TAKE THE MEDALS THIS MAP IS WORTH, OR LAND ONE TROOP ON THEIR H.Q.", kGoalNodes, kWalkLinks,
     kGoalMedals, 1, false},
    {"YOUR TURN", false, "PLACE ONE TROOP NEXT TO YOUR H.Q., OR DRAW TWO. THAT IS THE WHOLE TURN.", kTurnNodes,
     kWalkLinks, kPlainMedals, 1, false},
    {"YOUR REACH", false, "THE NEXT ONE GOES BESIDE A BASE YOU ALREADY HOLD. NEVER FURTHER.", kReachNodes, kWalkLinks,
     kPlainMedals, 1, false},
    {"COVERING", false, "YOU MAY LAND ON A SMALLER TROOP OF THEIRS. THE BURIED ONE STAYS BURIED.", kCoverNodes,
     kWalkLinks, kPlainMedals, 1, false},
    {"THE LINE HOME", true, "CUT THE WALK BACK TO YOUR H.Q. AND THE BASE AT THE FAR END STOPS COUNTING.", kChainNodes,
     kWalkLinksHeld, kPlainMedals, 1, false},
    {"REGIONS", false, "HOLD EVERY BASE AROUND A REGION AND ITS MEDALS ARE YOURS FOR GOOD.", kRegionNodes, kWalkLinks,
     kRegionMedals, 1, false},
    {"THE TROOPS", false, "", nullptr, nullptr, nullptr, 0, true},
    {"SPECIAL BASES", false, "A BADGED BASE ACTS WHEN YOU LAND ON IT. TAP ? IN A GAME TO SEE WHICH ONES THIS MAP HAS.",
     kSpecialNodes, kWalkLinks, kPlainMedals, 1, false},
};
constexpr int kWalkCount = static_cast<int>(sizeof(kWalkPages) / sizeof(kWalkPages[0]));

void buildWalkthrough(toybox::Screen& screen, const int page) {
  const WalkPage& content = kWalkPages[page];
  char counter[toybox::kSlashCounterChars];
  std::snprintf(counter, sizeof(counter), "%d/%d", page + 1, kWalkCount);
  chrome(screen, content.title, counter);

  const fui::TextStyle caption = captionStyle();
  // The deck's tallest caption, not this page's. A per-page caption height
  // gives a per-page picture rect, and the map then rescaled by 23% and jumped
  // on every page turn -- on a panel that full-refreshes, in a deck whose whole
  // premise is that you compare this page with the last one.
  const fui::Rect whole = screen.contentRect();
  int16_t tallest = 0;
  for (int i = 0; i < kWalkCount; ++i) {
    const int16_t h =
        fui::measureWrappedText(screen.target(), kWalkPages[i].caption, caption, static_cast<int16_t>(whole.width))
            .height;
    if (h > tallest) tallest = h;
  }
  const PageFrame frame = pageFrame(screen, content.caption, caption, page, kWalkCount, tallest);
  if (content.troops) {
    // The same screen the ? card's second page shows, drawn by the same
    // function. Eight faces in a grid taught the two shapes and not one of the
    // eight meanings, which is the half of the page that matters.
    troopReference(screen, frame.picture, 1);
  } else {
    const Scene scene{content.nodes, 8, content.links, 13, content.medals, content.medalCount};
    // The node is sized from the room, capped so a short picture does not draw
    // eight dinner plates into each other.
    int16_t node = static_cast<int16_t>(frame.picture.height / 9);
    if (node > 60) node = 60;
    if (node < 26) node = 26;
    drawScene(screen, frame.picture, scene, node);
  }
  screen.target().text(frame.caption, content.caption, caption);
}

}  // namespace

int howToPages() { return kWalkCount; }

// --- what the suite needs to see -------------------------------------------
//
// The pages are hand-authored tables, and nothing in the drawing can tell
// whether the position on one is a position that could exist. It could not:
// COVERING put two enemy troops on bases with no walk back to their own H.Q.,
// which is the rule the page four along is about. Handing the graph out lets
// host-tests do the walk itself rather than trusting a self-check written from
// the same assumption as the data.
int howToNodeCount() { return kWalkNodes; }
int howToLinkCount() { return kWalkLinkCount; }
void howToLinkAt(const int i, int& a, int& b) {
  a = kWalkLinks[i].a;
  b = kWalkLinks[i].b;
}
// 0 yours, 1 theirs, 2 nobody. An H.Q. counts as its owner's, and is never a
// stepping stone -- exactly as Game::reachable treats it.
int howToOwnerAt(const int page, const int node) {
  const Node& n = kWalkPages[page].nodes == nullptr ? kGoalNodes[node] : kWalkPages[page].nodes[node];
  if (kWalkPages[page].nodes == nullptr) return Nobody;
  // Both arms spelled as int. `Nobody` is an unnamed-enum constant and
  // `n.owner` is a uint8_t, and GCC's -Wextra makes mixing the two in a
  // conditional an error while clang says nothing -- the same gcc/clang gap
  // that has now cost two CI rounds tonight.
  return n.face == ' ' ? static_cast<int>(Nobody) : static_cast<int>(n.owner);
}
bool howToIsHq(const int node) { return node == 0 || node == 7; }
int howToHqSeat(const int node) { return node == 7 ? Yours : Theirs; }
// The one page whose subject is a base that has LOST its walk home.
bool howToCutOff(const int page, const int node) {
  if (kWalkPages[page].nodes == nullptr || !kWalkPages[page].showsCut) return false;
  return (kWalkPages[page].nodes[node].mark & MarkCross) != 0;
}

void buildHowTo(toybox::Screen& screen, const HowToModel& model) {
  buildWalkthrough(screen, (model.page < 0 || model.page >= kWalkCount) ? 0 : model.page);
}

}  // namespace tbui
