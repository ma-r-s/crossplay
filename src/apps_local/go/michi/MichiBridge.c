// See MichiBridge.h for why this file exists at all.

#include "MichiBridge.h"

#include <string.h>

#include "michi.h"

// nsims() lives in control.c and michi.h does not declare it.
int nsims(Game *game);

static int      gReady = 0;
static Position *gPos;
static Game     *gGame;
static int      *gOwnerMap;
static int      *gScoreCount;
static TreeNode *gTree;
static int       gLastSimulations;
static uint32_t  gLastMs;

// A point of ours, as a point of michi's.
//
// michi lays the board out as a bordered array indexed `row * (N + 1) + col`,
// with row counted from the top and col one-based, and it plays a board of
// `pos->size` inside an array sized for the compile-time maximum N. So a 9x9
// game on an N=19 build sits in the bottom-left of the larger array, which is
// what the `N - size` term is.
static Point michi_point(int row, int col, int size)
{
    return (Point)((N - size + 1 + row) * (N + 1) + col + 1);
}

static int our_point(Point pt, int size, int *row, int *col)
{
    int r = (int)pt / (N + 1) - (N - size + 1);
    int c = (int)pt % (N + 1) - 1;
    if (r < 0 || r >= size || c < 0 || c >= size) return 0;
    *row = r; *col = c;
    return 1;
}

void michi_bridge_init(void)
{
    if (gReady) return;
    // michi logs through a FILE* that ui.c opens, and ui.c is not vendored.
    // board_util.c is patched to accept a null sink; this says so out loud
    // rather than leaving a null global looking like an oversight.
    flog = NULL;

    make_pat3set();
    // The ladder reader's Position stack, one slot per level the depth cap
    // allows and one spare. See the note in michi.c: upstream's static 500 is
    // about 8MB at N=19.
    michi_stack_alloc(MICHI_LADDER_MAX + 1);
    // The LARGE pattern board, which upstream initialises inside
    // init_large_patterns() -- the function that loads patterns.prob and
    // patterns.spat from disk. Those two files are several megabytes and are
    // not vendored, so large-pattern matching is off here. expand() still calls
    // copy_to_large_board() unconditionally, and with the coordinate map left
    // zeroed that copy writes every point to large_board[0] and trips its own
    // assert. One line here is cheaper than a patch to somebody else's board.
    init_large_board();
    already_suggested = michi_calloc(1, sizeof(Mark));
    board_init();
    gOwnerMap   = michi_calloc(BOARDSIZE, sizeof(int));
    gScoreCount = michi_calloc(2 * N * N + 1, sizeof(int));
    gPos  = new_position();
    gGame = michi_calloc(1, sizeof(Game));
    gGame->pos = gPos;
    // time_init 0 means "use the simulation count", not "use a clock". michi's
    // own time management reads clock(), whose meaning on this chip is not
    // something to build a move budget on; the budget is applied below instead.
    gGame->time_init = 0;

    // The tree has to EXIST before the first genmove, because genmove's first
    // act is to free the one it was given and free_tree dereferences its
    // argument without checking it. Upstream's ui.c creates it once before the
    // GTP loop; there is no ui.c here, so this is that line.
    gTree = new_tree_node();

    // Play the game out to the end, or stop once it is decided? michi-c2's own
    // CGOS configuration says 1, and CGOS is a machine playing machines. A
    // person watching a finished board being filled in one point at a time
    // reads it as the machine not knowing the game is over.
    play_until_the_end = 0;

    // Never resign. michi returns RESIGN_MOVE once its win rate drops below
    // this, and the bridge would have to report that as a pass -- which hands
    // the opponent a free move every turn for the rest of a lost game. Losing
    // games are played out here; the app decides when a game is over.
    RESIGN_THRES = 0.0;

    // Quiet. michi is a command-line program and says so: at verbosity 2 it
    // dumps its whole tree to stderr after every search, and REPORT_PERIOD
    // prints a progress line inside one. Neither has a reader here -- the
    // device has no stderr and the host suite's output is the suite's.
    verbosity = 0;
    REPORT_PERIOD = 1000000000;

    gReady = 1;
}

// `ko` and `lastMove` are OUR point indices (-1 for none, -2 for a pass).
static void adopt(int size, const uint8_t *board, int toMove, int komiHalves, int ko, int lastMove, int moveNumber)
{
    board_set_size(gPos, size);
    empty_position(gPos);
    board_set_komi(gPos, (float)komiHalves / 2.0f);

    for (int row = 0; row < size; row++) {
        for (int col = 0; col < size; col++) {
            uint8_t here = board[row * size + col];
            if (here == 0) continue;
            // PLACED, not played: play_move would apply captures and ko, and
            // the position handed in is already the result of both.
            board_place_stone(gPos, michi_point(row, col, size), here == 1 ? BLACK : WHITE);
        }
    }
    board_set_color_to_play(gPos, toMove == 1 ? BLACK : WHITE);

    // board_place_stone() is play_move() with the move counter wound back, so
    // the loop above has just left pos->last on the bottom-right-most stone on
    // the board and pos->ko on whatever that placement happened to arm. Both
    // are wrong and both are read: ko decides which moves are legal, and last
    // is what every local heuristic in the playout looks at. Set them from the
    // GAME, which is the only thing that knows.
    board_set_ko(gPos, ko >= 0 ? michi_point(ko / size, ko % size, size) : PASS_MOVE);
    board_set_ko_old(gPos, PASS_MOVE);
    board_set_last(gPos, lastMove >= 0 ? michi_point(lastMove / size, lastMove % size, size) : PASS_MOVE);
    // Not carried across the boundary, and PASS_MOVE is how michi spells "there
    // is no such move" -- make_list_last_moves_neighbors skips them on it.
    board_set_last2(gPos, PASS_MOVE);
    board_set_last3(gPos, PASS_MOVE);
    // The move count matters for one thing: a playout that inherits `last ==
    // PASS_MOVE` with moves already played starts as though the opponent had
    // just passed, which is exactly right when they did. At move zero it must
    // not, so the count has to be honest.
    board_set_nmoves(gPos, moveNumber > 0 ? moveNumber : 0);

    slist_clear(allpoints);
    FORALL_POINTS(gPos, pt)
        if (point_color(gPos, pt) == EMPTY) slist_push(allpoints, pt);
    gGame->komi = board_komi(gPos);
}

void michi_bridge_seed(uint32_t seed)
{
    michi_bridge_init();   // zobrist setup saves and restores idum, so seed after it
    idum = (unsigned int)seed;
}

int michi_bridge_genmove(int size, const uint8_t *board, int toMove, int komiHalves, int ko, int lastMove,
                         int moveNumber, int simulations, uint32_t budgetMs, uint32_t (*nowMs)(void))
{
    michi_bridge_init();
    // Idempotent, and here rather than only in init because michi_bridge_forget
    // gives the stack back when the app closes.
    michi_stack_alloc(MICHI_LADDER_MAX + 1);
    adopt(size, board, toMove, komiHalves, ko, lastMove, moveNumber);

    // One search, bounded by a clock rather than sliced into chunks.
    //
    // tree_search() decides for itself when a position is clear enough to stop
    // reading, and BOTH of its tests compare the simulations done against the
    // count it was handed. An earlier version of this function ran the budget
    // as a growing series of small tree_search() calls so the clock could be
    // read between them; each of those calls stopped itself almost at once,
    // because a chunk of eight is "twenty percent read" after two simulations.
    // The search asked for five hundred played about a hundred, and the engine
    // lost twenty-one points of win rate against GNU Go 3.8 for it. The clock
    // now lives inside the loop (see michi_set_deadline in michi.c), so the
    // count michi reasons about is the whole budget and the move still ends on
    // time. Overshoot is one simulation.
    //
    // is_better_to_pass() is deliberately not called. It runs
    // compute_all_status(), which segmentation faults on a nearly full board,
    // and it can only answer yes when the opponent has just passed -- which the
    // position handed to us never records, because the stones are PLACED rather
    // than played. Passing is the app's decision and it is taken in GoMichi.
    N_SIMS = simulations;
    gGame->time_init = 0;

    free_tree(gTree);
    gTree = new_tree_node();
    memset(gOwnerMap, 0, BOARDSIZE * sizeof(int));
    memset(gScoreCount, 0, (2 * N * N + 1) * sizeof(int));
    nplayouts_real = 0;

    uint32_t began = nowMs != NULL ? nowMs() : 0;
    michi_set_deadline(nowMs, began, budgetMs);
    Point pt = tree_search(gPos, gTree, simulations, gOwnerMap, gScoreCount, 0);
    michi_set_deadline(NULL, 0, 0);

    gLastMs = nowMs != NULL ? nowMs() - began : 0;
    gLastSimulations = nplayouts_real;

    if (pt == PASS_MOVE || pt == RESIGN_MOVE) return -1;
    int row, col;
    if (!our_point(pt, size, &row, &col)) return -1;
    return row * size + col;
}

int michi_bridge_last_simulations(void) { return gLastSimulations; }
uint32_t michi_bridge_last_ms(void) { return gLastMs; }

int michi_bridge_ranked(int size, int *out, int max)
{
    if (gTree == NULL || gTree->children == NULL || max <= 0) return 0;
    // Partial selection by visit count, which is what best_move() ranks by and
    // what the search's own answer is. `max` is a handful, so this is cheaper
    // than sorting the whole child list.
    int found = 0;
    for (int rank = 0 ; rank < max ; rank++) {
        TreeNode *best = NULL;
        for (TreeNode **child = gTree->children ; *child != NULL ; child++) {
            if ((*child)->move == PASS_MOVE || (*child)->move == RESIGN_MOVE) continue;
            int already = 0;
            for (int i = 0 ; i < found ; i++) {
                int row, col;
                if (our_point((*child)->move, size, &row, &col) && out[i] == row * size + col) { already = 1; break; }
            }
            if (already) continue;
            if (best == NULL || (*child)->v > best->v) best = *child;
        }
        if (best == NULL) break;
        int row, col;
        if (!our_point(best->move, size, &row, &col)) break;
        out[found++] = row * size + col;
    }
    return found;
}

void michi_bridge_context(int size, int *ko, int *lastMove, int *moveNumber)
{
    int row, col;
    if (ko != NULL) {
        Point pt = board_ko(gPos);
        *ko = (pt != PASS_MOVE && our_point(pt, size, &row, &col)) ? row * size + col : -1;
    }
    if (lastMove != NULL) {
        Point pt = board_last_move(gPos);
        if (pt == PASS_MOVE) *lastMove = board_nmoves(gPos) > 0 ? -2 : -1;
        else *lastMove = our_point(pt, size, &row, &col) ? row * size + col : -1;
    }
    if (moveNumber != NULL) *moveNumber = (int)board_nmoves(gPos);
}

void michi_bridge_forget(void)
{
    if (!gReady) return;
    free_tree(gTree);
    // Replaced rather than nulled: genmove's first act is to free this, and
    // free_tree dereferences its argument without checking it.
    gTree = new_tree_node();
    // And the ladder stack, which is the larger of the two. Re-allocated on the
    // next genmove.
    michi_stack_free();
}
