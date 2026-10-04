// michi.c -- A minimalistic Go-playing engine
/*
This is a recoding in C (for speed) of the michi.py code by Petr Baudis
avalaible at https://github.com/pasky/michi .

(c) 2015 Petr Baudis <pasky@ucw.cz> Denis Blumstein <db3108@free.fr>
MIT licence (i.e. almost public domain)

The following comments are taken almost verbatim from the michi.py code

A minimalistic Go-playing engine attempting to strike a balance between
brevity, educational value and strength.  It can beat GNUGo on 13x13 board
on a modest 4-thread laptop.

To start reading the code, begin either:
* Bottom up by looking at the goban implementation (in board.c) - starting with
  the Position struct (in board.h), empty_position() and play_move() functions.
* in the middle of this file by looking at the Monte Carlo playout 
  implementation, starting with the mcplayout() function.
* Top down, by looking at the MCTS implementation, starting with the
  tree_search() function. It is just a loop of tree_descend(),
  mcplayout() and tree_update() round and round.

It may be better to jump around a bit instead of just reading straight
from start to end.

The source is composed in 7 independent parts
- Board routines (in board.c)
- Go heuristics (this file)
- Monte Carlo Playout policy (this file)
- Monte Carlo Tree search (this file)
- User Interface (in ui.c, params.c and debug.c)
- Pattern code : 3x3 and large patterns (in patterns.c)
- Overall management of the search (time, dynamic komi, etc.) (in control.c)

In C, functions prototypes must be declared before use. 
In order to avoid these declarations, functions are defined before they are 
used, which has the same effect.
This means that the higher level functions are found towards the bottom of
this file. This may not be a good idea in terms of readibility but at least
the order is the same as in the michi python code.

Short bibliography
------------------
[1] Martin Mueller, Computer Go, Artificial Intelligence, Vol.134, No 1-2,
    pp 145-179, 2002
[2] Remi Coulom.  Efficient Selectivity and Backup Operators in Monte-Carlo Tree
    Search.  Paolo Ciancarini and H. Jaap van den Herik.  5th International 
    Conference on Computer and Games, May 2006, Turin, Italy.  2006. 
    <inria-00116992>
[3] Sylvain Gelly, Yizao Wang, Remi Munos, Olivier Teytaud.  Modification of UCT
    with Patterns in Monte-Carlo Go. [Research Report] RR-6062, 2006.
    <inria-00117266v3>
[4] David Stern, Ralf Herbrich, Thore Graepel, Bayesian Pattern Ranking for Move
    Prediction in the Game of Go, In Proceedings of the 23rd international 
    conference on Machine learning, pages 873–880, Pittsburgh, Pennsylvania, 
    USA, 2006
[5] Rémi Coulom. Computing Elo Ratings of Move Patterns in the Game of Go. 
    In ICGA Journal (2007), pp 198-208.
[6] Sylvain Gelly, David Silver. Achieving Master Level Play in 9×9 Computer Go.
    Proceedings of the Twenty-Third AAAI Conference on Artificial Intelligence 
    (2008)
[7] Albert L Zobrist. A New Hashing Method with Application for Game Playing.
    Technical Report #88. April 1970
[8] Petr Baudis. MCTS with Information Sharing, PhD Thesis, 2011
[9] Robert Sedgewick, Algorithms in C, Addison-Wesley, 1990

+ many other PhD thesis accessible on the WEB

[1] can be consulted for the definition of Computer Go terms : 
    points, blocks, eyes, false eyes, liberties, etc.
    and historical bibliography
*/
#include "michi.h"

//========================= Definition of Data Structures =====================
// Given a board of size NxN (N=9, 19, ...), we represent the position
// as an (N+1)*(N+2)+1 Byte array, with 0 (empty), 2 (white), 3 (black), 
// and 1 (off-board border to make rules implementation easier).  
// Coordinates are just indices in this array.
//
// -------------------------------- Global Data -------------------------------
// FORK CHANGE: extern, not a second definition. Upstream defines mark1/mark2
// here AND in board.c, and buf here AND in control.c; both pairs link only
// because C compilers used to merge tentative definitions by default
// (-fcommon). GCC 10 turned that off, so every compiler this fork builds with
// -- the host suite's g++ and the Xtensa cross-compiler -- rejects the
// duplicates outright. board.c and control.c keep the definitions.
extern Mark  *mark1, *mark2;
Mark         *already_suggested;
extern char  buf[BUFLEN];
static int   disp_ladder;
static char* colstr  = "@ABCDEFGHJKLMNOPQRST";

// compute_cfg_distances()'s flood-fill queue. Beside the other two scratches
// and for the same reason: 25KB in a frame on the deepest path the search has.
static Point *cfg_fringe;
Point *michi_cfg_fringe(void)
{
    if (cfg_fringe == NULL) cfg_fringe = michi_malloc(30 * BOARDSIZE * sizeof(Point));
    return cfg_fringe;
}

// How deep the ladder reader is; see MICHI_LADDER_MAX below.
static int ladder_depth;

// FORK CHANGE: a deadline the search stops at, checked once per simulation.
//
// The move budget is a wall clock here -- somebody is holding the panel -- and
// upstream's tree_search() runs its whole count in one call with no way out of
// it. Slicing the call into small ones is NOT an alternative: both of the
// early stops inside tree_search compare `i` against the `n` IT was given, so a
// chunk of eight stops itself after two simulations, and the search that was
// asked for five hundred plays a handful. Measured, that cost twenty-one points
// of win rate against GNU Go 3.8. So `n` stays the whole budget and the clock
// ends the loop instead.
//
// michi_now_ms is NULL off the device and in the host suite, where there is no
// clock and the count is the bound.
static uint32_t (*michi_now_ms)(void);
static uint32_t michi_began_ms, michi_budget_ms;

void michi_set_deadline(uint32_t (*now)(void), uint32_t began, uint32_t budget)
{
    michi_now_ms = now;
    michi_began_ms = began;
    michi_budget_ms = budget;
}

// Unsigned subtraction, so a millisecond counter that wraps is still ordered.
static int michi_out_of_time(void)
{
    if (michi_now_ms == NULL) return 0;
    return (uint32_t)(michi_now_ms() - michi_began_ms) >= michi_budget_ms;
}

// expand()'s scratch position. Allocated once, beside the ladder stack and for
// the same reason: 5.7KB in a frame on the deepest path this engine takes.
static Position *expand_scratch;
Position *michi_expand_scratch(void)
{
    if (expand_scratch == NULL) expand_scratch = michi_malloc(sizeof(Position));
    return expand_scratch;
}

// FORK CHANGE: expand()'s four board-sized arrays, off the frame for the same
// reason. They were 5,568 bytes of it at N=19, on the path that ends in the
// ladder reader's recursion.
typedef struct {
    char      cfg_map[BOARDSIZE];
    Info      sizes[BOARDSIZE];
    Point     moves[BOARDSIZE];
    TreeNode *childset[BOARDSIZE];
} ExpandWork;
static ExpandWork *expand_work;
static ExpandWork *michi_expand_work(void)
{
    if (expand_work == NULL) expand_work = michi_malloc(sizeof(ExpandWork));
    return expand_work;
}

// FORK CHANGE: fix_atari()'s ladder workspace, allocated once beside expand()'s.
// Upstream copies the whole position into fix_atari()'s frame, twice, and
// sizeof(Position) is about 16KB at N=19 (5.7KB at 13): that one frame was
// 16,928 bytes, sitting UNDER the ladder reader's recursion (twelve levels of
// 1,600), on a 32KB task. fix_atari() is never re-entered -- the recursion runs
// through fix_atari_r() -- and each copy is dead before the next is made, so
// one scratch position serves both.
static Position *fix_atari_scratch;
static Position *michi_fix_atari_scratch(void)
{
    if (fix_atari_scratch == NULL) fix_atari_scratch = michi_malloc(sizeof(Position));
    return fix_atari_scratch;
}

// Stack of Positions for use in recursive calls fix_atari/read_ladder_attack
//
// FORK CHANGE: allocated, not static, and MICHI_LADDER_MAX + 1 deep rather than 500.
//
// sizeof(Position) is about 5.7KB at N=13, so upstream's `Position
// stack_pos[500]` is 2.8MB of .bss. On a desktop that is nothing; on this chip
// it is nine times the whole DRAM segment, and the device build failed to LINK
// with `dram0_0_seg overflowed by 2811856 bytes`. It goes to PSRAM through
// michi_malloc like everything else here, and michi_stack_free() gives it back
// when the app closes.
//
// Sized by MICHI_LADDER_MAX, not 500 or 128: this stack is only the ladder
// reader's, read_ladder_attack_r() pushes one position per level, and the
// depth cap is the most levels there can be. At N=19 a position is about 16KB,
// so 128 was 2MB of PSRAM of which twelve slots could ever be used.
int avail_pos;
static int stack_pos_depth;
Position *stack_pos;

void michi_stack_alloc(int depth)
{
    if (stack_pos != NULL) return;
    stack_pos = michi_malloc((size_t)depth * sizeof(Position));
    stack_pos_depth = depth;
    avail_pos = 0;
}

void michi_stack_free(void)
{
    free(stack_pos);
    stack_pos = NULL;
    stack_pos_depth = 0;
    avail_pos = 0;
    free(expand_scratch);
    expand_scratch = NULL;
    free(fix_atari_scratch);
    fix_atari_scratch = NULL;
    free(expand_work);
    expand_work = NULL;
    free(cfg_fringe);
    cfg_fringe = NULL;
    ladder_depth = 0;
}

//================================ Go heuristics ==============================
// The couple of functions read_ladder_attack / fix_atari is maybe the most 
// complicated part of the whole program (sadly). 
// Feel free to just TREAT IT AS A BLACK-BOX, it's not really that interesting!
//
int fix_atari_r(Position *pos, Point pt, Slist moves);

// FORK CHANGE: a depth cap on the ladder reader.
//
// read_ladder_attack recurses through fix_atari_r back into itself, one level
// per ply of the ladder, and each level costs about a kilobyte of C stack. On a
// desktop with an eight megabyte stack that is free; on this chip the whole
// task gets tens of kilobytes, and the recursion is the one thing the stack
// budget tool cannot bound (it reports cycles and never sums them).
//
// Twelve plies reads every ladder a nine by nine board can hold and most of
// what thirteen and nineteen can. The arithmetic scripts_local/stack_budget.py
// cannot do for itself, because it reports cycles and never sums them, at N=19
// (measured 2026-10-04, -fstack-usage on the x4pro build):
//
//   down to fix_atari, deepest (tree_search > mcplayout > ... > fix_atari)  4,128
//   12 levels of read_ladder_attack 32 + _r 448 + fix_atari_r 1,120       19,200
//   the leaf (undo_move > compute_block > slist_push)                       3,520
//   total                                                     26,848 of 32,768
//
// That total holds only because fix_atari()'s position copy and expand()'s
// arrays are off the frame (see fix_atari_scratch, ExpandWork); with them on it
// it was about 46KB. A frame that grows with N lands here first. Past it the reader answers "not caught", which is the
// conservative answer: the engine declines to claim a capture it has not
// proved, rather than crashing on a board somebody is looking at.
// (MICHI_LADDER_MAX itself is in michi.h: MichiBridge sizes the stack by it.)
static Point read_ladder_attack_r(Position *pos, Point pt, Slist libs);

Point read_ladder_attack(Position *pos, Point pt, Slist libs)
{
    if (ladder_depth >= MICHI_LADDER_MAX) return PASS_MOVE;
    ladder_depth++;
    Point move = read_ladder_attack_r(pos, pt, libs);
    ladder_depth--;
    return move;
}

__INLINE__ Position *push_position(Position *pos)
{
    // FORK CHANGE: the bound is checked BEFORE the write. Upstream computes the
    // slot, increments, then tests -- so the call that overflows writes one
    // Position past the end and only then reports it.
    if (avail_pos >= stack_pos_depth)
        fatal_error("stack of Position overflow");
    Position *newpos = stack_pos + avail_pos++;
    memcpy(newpos, pos, sizeof(Position));
    return newpos;
}

__INLINE__ void pop_position(void)
{
    avail_pos--;
}

static Point read_ladder_attack_r(Position *pos, Point pt, Slist libs)
// Check if a capturable ladder is being pulled out at pt and return a move
// that continues it in that case. Expects its two liberties in libs.
// Actually, this is a general 2-lib capture exhaustive solver.
{
    char *ret;
    Point moves[100];
    Point l1, l2, move=PASS_MOVE;

    // always play first on the liberty that has 3 neighbors EMPTY
    if (point_nlibs(pos, libs[1]) == 3) {
        l2 = libs[1];
        goto process_l2;
    }
    if (point_nlibs(pos, libs[2]) == 3) {
        l2 = libs[2];
        goto process_l2;
    }
            
    if (line_height(libs[1], board_size(pos)) > 0) { 
        // for ladder try first the liberty that is not on the first line
        l1 = libs[1];
        l2 = libs[2];
    }
    else {
        l1 = libs[2];
        l2 = libs[1];
    }

    Position *pos_l = push_position(pos);
    if (disp_ladder) fprintf(stderr,"read_ladder_attack:\n");
    ret = play_move(pos_l, l1);
    if (ret[0]==0) {         // move is legal
        if (disp_ladder) print_pos(pos_l, stderr, 0);
        // fix_atari_r() will recursively call read_ladder_attack() back
        slist_clear(moves);
        int is_atari = fix_atari_r(pos_l, pt, moves);
        // if block is in atari and cannot escape, it is caugth in a ladder
        if (is_atari && slist_size(moves) == 0) {
            move = l1;
            pop_position();
            goto finished;
        }
    }
    pop_position();

process_l2:     // exploration can be done in pos which is a workspace
    if (disp_ladder) fprintf(stderr,"read_ladder_attack:\n");
    ret = play_move(pos, l2);
    if (ret[0]==0) {         // move is legal
        if (disp_ladder) print_pos(pos, stderr, 0);
        // fix_atari_r() will recursively call read_ladder_attack() back
        slist_clear(moves);
        int is_atari = fix_atari_r(pos, pt, moves);
        // if block is in atari and cannot escape, it is caugth in a ladder
        if (is_atari && slist_size(moves) == 0)
            move = l2;
    }
finished:
    return move;
}

int fix_atari_r(Position *pos, Point pt, Slist moves)
// An atari/capture analysis routine that checks the group at Point pt,
// determining whether (i) it is in atari (ii) if it can escape it,
// either by playing on its liberty or counter-capturing another group.
//
// Return 1 (true) if the group is in atari, 0 otherwise
//        moves : a list of moves that capture or save blocks
//        sizes : list of same lenght as moves (size of corresponding blocks)
// singlept_ok!=0 means that we will not try to save one-point groups
//
// Only called by read_ladder_attack()
{
    Block b = point_block(pos, pt);
    int   in_atari=1;
    // FORK CHANGE: MAX_BLOCKS+1, not 256. This is a Slist of BLOCK ids and
    // there are only MAX_BLOCKS of them (256 at N=19, 128 at N=13), so this is
    // the most the list can hold -- once per level of a recursion that is
    // MICHI_LADDER_MAX deep.
    Point l, libs[5], blocks[MAX_BLOCKS+1], blibs[5];

    slist_clear(moves);
    if (block_nlibs(pos,b) >= 2) { 
        return 0;  
    }

    Color other=color_other(board_color_to_play(pos));
    block_compute_libs(pos, b, libs, 1);
    if (point_color(pos, pt) == other) { 
        // - this is opponent's group, that's enough to capture it
        //fprintf(stderr, "fix_atari_r: opponent block\n");
        slist_insert(moves, libs[1]); 
        return in_atari;
    }

    // This is our group and it is in atari
    // Before thinking about defense, what about counter-capturing a neighbor ?
    make_list_neighbor_blocks_in_atari(pos, b, blocks, pt);
    FORALL_IN_SLIST(blocks, b1) {
        block_compute_libs(pos, b1, blibs, 1);
        slist_insert(moves, blibs[1]);
    }
    slist_sort(moves);    // used only for compatibility tests with michi.py

    l = libs[1];
    // We are escaping.  
    // Will playing our last liberty gain/ at least two liberties?
    char *ret = play_move(pos, l);
    if (ret[0]!=0)
        return 1;     // oops, suicidal move
    b = point_block(pos, l);
    if (block_nlibs(pos,b) >= 2) {
        // Good, there is still some liberty remaining - but if it's just the 
        // two, check that we are not caught in a ladder... (Except that we 
        // don't care if we already have some alternative escape routes!)
        if (slist_size(moves)>0 || block_nlibs(pos,b)>=3) { 
            slist_insert(moves, l);
        }
        else if (block_nlibs(pos,b)==2) {
            block_compute_libs(pos,b,libs,2);
            if (read_ladder_attack(pos,l,libs) == 0)
                slist_insert(moves, l);
        }
    }
    return in_atari;
}

int fix_atari(Position *pos, Point pt, int singlept_ok
        , int twolib_test, int twolib_edgeonly, Slist moves, Slist sizes)
// An atari/capture analysis routine that checks the group at Point pt,
// determining whether (i) it is in atari (ii) if it can escape it,
// either by playing on its liberty or counter-capturing another group.
//
// Return 1 (true) if the group is in atari, 0 otherwise
//        moves : a list of moves that capture or save blocks
//        sizes : list of same lenght as moves (size of corresponding blocks)
// singlept_ok!=0 means that we will not try to save one-point groups
{
    Block b = point_block(pos, pt);
    int   in_atari=1, maxlibs=3;
    // FORK CHANGE: MAX_BLOCKS+1, not 256. Same as fix_atari_r above: this is a
    // Slist of BLOCK ids and there are only MAX_BLOCKS of them.
    Point l, libs[5], blocks[MAX_BLOCKS+1], blibs[5];

    slist_clear(moves); slist_clear(sizes);
    if (singlept_ok && block_size(pos, b) == 1) return 0;
    if (block_nlibs(pos,b) >= 2) { 
        if (twolib_test && block_nlibs(pos,b) == 2 && block_size(pos,b) > 1) {
            block_compute_libs(pos, b, libs, maxlibs);
            if (twolib_edgeonly
                   && ((line_height(libs[1], board_size(pos)))>0 
                        || (line_height(libs[2], board_size(pos)))>0)) {
                // no expensive ladder check
                return 0;
            }
            else {
                // check that the block cannot be caught in a working ladder
                // If it can, that's as good as in atari, a capture threat.
                // (Almost - N/A for countercaptures.)
                Position *workpos = michi_fix_atari_scratch();
                *workpos = *pos;
                Point ladder_attack = read_ladder_attack(workpos, pt, libs);
                if (ladder_attack) {
                    if(slist_insert(moves, ladder_attack))
                        slist_push(sizes, block_size(pos, b));
                }
            }
        } 
        return 0;  
    }

    block_compute_libs(pos, b, libs, maxlibs);
    Color other=color_other(board_color_to_play(pos));
    if (point_color(pos, pt) == other) { 
        // - this is opponent's group, that's enough to capture it
        if (slist_insert(moves, libs[1])) 
            slist_push(sizes, block_size(pos, b));
        return in_atari;
    }

    // This is our group and it is in atari
    // Before thinking about defense, what about counter-capturing a neighbor ?
    make_list_neighbor_blocks_in_atari(pos, b, blocks, pt);
    FORALL_IN_SLIST(blocks, b1) {
        block_compute_libs(pos, b1, blibs, 1);
        slist_insert(moves, blibs[1]);
    }
    slist_sort(moves);
    FORALL_IN_SLIST(moves, n)
        slist_push(sizes,block_size(pos, point_block(pos, n)));

    l = libs[1];
    // We are escaping.  
    // Will playing our last liberty gain/ at least two liberties?
    char *ret = play_move(pos, l);
    if (ret[0]!=0)
        return 1;     // oops, suicidal move
    b = point_block(pos, l);
    if (block_nlibs(pos,b) >= 2) {
        // Good, there is still some liberty remaining - but if it's just the 
        // two, check that we are not caught in a ladder... (Except that we 
        // don't care if we already have some alternative escape routes!)
        if (slist_size(moves)>0 || block_nlibs(pos,b)>=3) { 
            if (slist_insert(moves, l))
                slist_push(sizes, block_size(pos, b));
        }
        else if (block_nlibs(pos,b)==2) {
            block_compute_libs(pos,b,libs,2);
            Position *workpos = michi_fix_atari_scratch();  // workspace for read_ladder_attack
            *workpos = *pos;
            if (read_ladder_attack(workpos, l, libs) == 0)
                if (slist_insert(moves, l))
                    slist_push(sizes, block_size(pos, b));
        }
    }
    undo_move(pos);
    return in_atari;
}

//========================= Montecarlo playout policy =========================
int gen_playout_moves_capture(Position *pos, Slist heuristic_set, float prob,
                                    int expensive_ok, Slist moves, Slist sizes)
// Compute list of candidate next moves in the order of preference (capture)
// heuristic_set is the set of coordinates considered for applying heuristics;
// this is the immediate neighborhood of last two moves in the playout, but
// the whole board while prioring the tree.
{
    int   k, twolib_edgeonly = !expensive_ok;
    Point move2[20], size2[20];

    slist_clear(moves); slist_clear(sizes);
    if (random_int(10000) <= prob*10000.0) {
        mark_init(already_suggested);
        FORALL_IN_SLIST(heuristic_set, pt)
            if (point_is_stone(pos, pt)) {
                Block b = point_block(pos, pt);
                if (is_marked(already_suggested, b)) continue;
                mark(already_suggested, b);
                fix_atari(pos, pt, SINGLEPT_NOK, TWOLIBS_TEST,
                                                twolib_edgeonly, move2, size2);
                k=1;
                FORALL_IN_SLIST(move2, move)
                    if (slist_insert(moves, move))
                        slist_push(sizes, size2[k++]);
            }
        mark_release(already_suggested);
    }
    return slist_size(moves);
}

int gen_playout_moves_pat3(Position *pos, Slist heuristic_set, float prob,
                                                                Slist moves)
// Compute list of candidate next moves in the order of preference (3x3 pattern)
// heuristic_set is the set of coordinates considered for applying heuristics;
// this is the immediate neighborhood of last two moves in the playout, but
// the whole board while prioring the tree.
{
    slist_clear(moves);
    if (random_int(10000) <= prob*10000.0) {
        mark_init(already_suggested);
        FORALL_IN_SLIST(heuristic_set, pt)
            if (point_color(pos,pt) == EMPTY && pat3_match(pos, pt))
               slist_push(moves, pt); 
        mark_release(already_suggested);
    }
    return slist_size(moves);
}

int gen_playout_moves_random(Position *pos, Point moves[BOARDSIZE], Point i0)
// Generate a list of moves (includes false positives - suicide moves;
// does not include true-eye-filling moves), starting from a given board index
// (that can be used for randomization)
{
    Color c=board_color_to_play(pos);
    slist_clear(moves);
    for(Point i=i0 ; i<BOARD_IMAX ; i++) {
        if (point_color(pos,i) != EMPTY) continue;   // ignore NOT EMPTY Points
        if (is_eye(pos,i) == c) continue;        // ignore true eyes for player
        slist_push(moves, i); 
    }
    for(Point i=BOARD_IMIN-1 ; i<i0 ; i++) {
        if (point_color(pos,i) != EMPTY) continue;   // ignore NOT EMPTY Points
        if (is_eye(pos,i) == c) continue;        // ignore true eyes for player
        slist_push(moves, i); 
    }
    return slist_size(moves);
}

Point choose_random_move(Position *pos, Point i0, int disp)
// Replace the sequence gen_playout_moves_random(); choose_from()
{
    char   *ret;
    Color c=board_color_to_play(pos);
    Info     sizes[20];
    Point    ds[20], i=i0, move=PASS_MOVE;
    //Position saved_pos = *pos;

    do {
        if (point_color(pos,i) != EMPTY) goto not_found; 
        if (is_eye(pos,i) == c) goto not_found;  // ignore true eyes for player
        ret = play_move(pos, i);
        if (ret[0] == 0) {    // move OK
            move = i;
            // check if the suggested move did not turn out to be a self-atari
            int r = random_int(10000), tstrej;
            tstrej = r<=10000.0*PROB_RSAREJECT;
            if (tstrej) {
                slist_clear(ds); slist_clear(sizes);
                fix_atari(pos, i, SINGLEPT_OK, TWOLIBS_TEST, 1, ds, sizes);
                if (slist_size(ds) > 0) {
                    if(disp) fprintf(stderr, "rejecting self-atari move %s\n",
                                                           str_coord(i, buf));
                    undo_move(pos);
                    //*pos = saved_pos; // undo move;
                    move = PASS_MOVE;
                    goto not_found;
                }
            }
            break;
        }
not_found:
        i++;
        if (i >= BOARD_IMAX) i = BOARD_IMIN - 1;
    } while (i!=i0);
    return move;
}

Point choose_from(Position *pos, Slist moves, char *kind, int disp)
{
    char   *ret;
    Info   sizes[20];
    Point  move = PASS_MOVE, ds[20];
    //Position saved_pos = *pos;

    FORALL_IN_SLIST(moves, pt) {
        if (is_marked(already_suggested, pt))
            continue;
        mark(already_suggested, pt);
        if (disp && strcmp(kind, "random")!=0)
            fprintf(stderr,"move suggestion (%s) %s\n", kind,str_coord(pt,buf));
        ret = play_move(pos, pt);
        if (ret[0] == 0) {    // move OK
            move = pt;
            // check if the suggested move did not turn out to be a self-atari
            int r = random_int(10000), tstrej;
            if (strcmp(kind,"random") == 0) tstrej = r<=10000.0*PROB_RSAREJECT;
            else                            tstrej = r<= 10000.0*PROB_SSAREJECT;
            if (tstrej) {
                slist_clear(ds); slist_clear(sizes);
                fix_atari(pos, pt, SINGLEPT_OK, TWOLIBS_TEST, 1, ds, sizes);
                if (slist_size(ds) > 0) {
                    if(disp) fprintf(stderr, "rejecting self-atari move %s\n",
                                                           str_coord(pt, buf));
                    undo_move(pos);
                    michi_assert(pos, blocks_OK(pos,pt));
                    //*pos = saved_pos; // undo move;
                    move = PASS_MOVE;
                    continue;
                }
            }
            break;
        }
    }
    return move;
}

Point choose_capture_move(Position *pos,Slist heuristic_set,float prob,int disp)
// Replace the sequence gen_playout_capture_moves(); choose_from()
{
    int   twolib_edgeonly = 1;
    Point move=PASS_MOVE, moves[20], sizes[20];

    if (random_int(10000) <= prob*10000.0) {
        mark_init(already_suggested);
        FORALL_IN_SLIST(heuristic_set, pt)
            if (point_is_stone(pos, pt)) {
                Block b = point_block(pos, pt);
                if (is_marked(already_suggested, b)) continue;
                mark(already_suggested, b);
                fix_atari(pos, pt, SINGLEPT_NOK, TWOLIBS_TEST,
                                                twolib_edgeonly, moves, sizes);
                slist_shuffle(moves);
                move = choose_from(pos, moves, "capture", disp);
                if (move != PASS_MOVE) break;
            }
        mark_release(already_suggested);
    }
    return move;
}

double playout_score(Position *pos, int owner_map[], int score_count[2*N*N+1])
// compute score (>0 if BLACK wins); this assumes a final position with all 
// dead stones captured and only single point eyes on the board ...
{
    double s1;
    int s=0;

    FORALL_POINTS(pos,pt) {
        Color c = point_color(pos, pt);
        if (c == EMPTY) c = is_eyeish(pos,pt);
        if (c == BLACK) {
            s++;
            owner_map[pt]++;
        }
        else if (c == WHITE) {
            s--;
            owner_map[pt]--;
        }
    }
    s1 = s;
    score_count[s + N*N]++;
    return s1 - board_komi(pos) - board_delta_komi(pos);
}

double mcplayout(Position *pos, int amaf_map[], int owner_map[],
                                           int score_count[2*N*N+1], int disp)
// Start a Monte Carlo playout from a given position, return score for to-play
// player at the starting position; amaf_map is board-sized scratchpad recording// who played at a given position first
{
    double s=0.0;
    int    passes=0;
    Point  last_moves_neighbors[20], moves[BOARDSIZE], move;
    if(disp) {
        disp_ladder = 1;
        fprintf(stderr, "** SIMULATION **\n");
    }
    if (board_nmoves(pos)>0 && board_last_move(pos)==0) passes=1;

    while (passes < 2 && board_nmoves(pos) < MAX_GAME_LEN) {
        michi_assert(pos, all_blocks_OK(pos));
        move = 0;
        if(disp) { 
            fprintf(stderr, "mcplayout: idum = %u\n", idum);
            print_pos(pos, stderr, NULL);
        }
        // We simply try the moves our heuristics generate, in a particular
        // order, but not with 100% probability; this is on the border between
        // "rule-based playouts" and "probability distribution playouts".
        make_list_last_moves_neighbors(pos, last_moves_neighbors);

        // Capture heuristic suggestions
        if((move=choose_capture_move(pos, last_moves_neighbors, 
                        PROB_HEURISTIC_CAPTURE, disp)) != PASS_MOVE)
                goto found;

        // 3x3 patterns heuristic suggestions
        if (gen_playout_moves_pat3(pos, last_moves_neighbors,
                                           PROB_HEURISTIC_PAT3, moves)) {
            mark_init(already_suggested);
            if((move=choose_from(pos, moves, "pat3", disp)) != PASS_MOVE) {
                mark_release(already_suggested);
                goto found;
            }
            mark_release(already_suggested);
        }
            
        // FORK CHANGE: draw the start from the BOARD, not from 1..N.
        //
        // choose_random_move() walks forward from here and wraps, so an
        // off-board start still finds a move -- but it funnels every start in
        // the border into the same few entry points, and with N=13 playing 9x9
        // more than half of all starts are in the border. The playout's random
        // move stops being uniform, which is most of what a playout is.
        //
        // Upstream cannot meet this because it builds N to the size it plays,
        // where 1..N IS the board. A board of `size` sits at array rows
        // N-size+1..N, columns 1..size.
        int rsize = board_size(pos);
        int x0 = random_int(rsize) + 1, y0 = random_int(rsize) + 1 + (N - rsize);
        move = choose_random_move(pos, y0*(N+1) + x0 , disp);
found:
        if (move == PASS_MOVE) {      // No valid move : pass
            pass_move(pos);
            passes++;
        }
        else {
            if (amaf_map[move] == 0)      // mark the point with 1 for BLACK
                // WHITE because in michi.py pos is updated after this line
                amaf_map[move] = (board_color_to_play(pos) == WHITE ? 1 : -1);
            passes=0;
        }
        //print_pos(pos, stderr, 0);
    }
    s = playout_score(pos, owner_map, score_count);
    return s;
}
//========================== Montecarlo tree search ===========================
TreeNode* new_tree_node(void)
// Build a new tree node initialized with prior EVEN
{
    TreeNode *node = michi_calloc(1,sizeof(TreeNode));
    node->pv = PRIOR_EVEN; node->pw = PRIOR_EVEN/2;
    return node;
}

void expand(Position *pos, TreeNode *tree)
// add and initialize children to a leaf node which represents the Position pos
{
    // FORK CHANGE: the arrays are a scratch block, see ExpandWork.
    ExpandWork *work = michi_expand_work();
    char     *cfg_map = work->cfg_map;
    int      nchildren = 0;
    Info     *sizes = work->sizes;
    Point    *moves = work->moves;
    TreeNode **childset = work->childset;
    TreeNode *node;
    // Cleared every call. On the frame these were garbage until written; in a
    // block that outlives the call they would be the LAST tree's pointers, and
    // the prior loops below read childset[] for capture and pattern moves that
    // the first loop may not have written. A null is skipped, a stale pointer
    // would be written through.
    memset(childset, 0, sizeof(work->childset));
    // FORK CHANGE: heap, not frame. sizeof(Position) is about 5.7KB at N=13,
    // and this function sits in the deepest path the search takes. expand() is
    // not re-entrant -- it is called from tree_descend and from tree_search,
    // never from itself -- so one scratch position serves every call.
    Position *pos2 = michi_expand_scratch();
    if (board_last_move(pos) != PASS_MOVE)
        compute_cfg_distances(pos, board_last_move(pos), cfg_map);

    // Use light random playout generator to get all the empty points (not eye)
    gen_playout_moves_random(pos, moves, BOARD_IMIN-1);

    // FORK CHANGE: +2, not +1. The array is NULL-terminated and free_tree()
    // walks it until the NULL, but the block at the foot of this function adds
    // a PASS child when nchildren <= 2 -- and when every candidate was legal
    // that child lands in the terminator's slot, so the walk runs off the end
    // and free_tree() frees whatever it reads.
    //
    // Upstream never meets it because genmove() passes out of a decided game
    // before the board gets down to two points. This fork deliberately does
    // not (passing is the app's decision, see MichiBridge.c), so the last two
    // moves of nearly every game are exactly that shape.
    tree->children = michi_calloc(slist_size(moves)+2, sizeof(TreeNode*));
    FORALL_IN_SLIST(moves, pt) {
        assert(point_color(pos, pt) == EMPTY);
        char* ret = play_move(pos, pt);
        if (ret[0] != 0) continue;
        undo_move(pos);
        // pt is a legal move : we build a new node for it
        node = childset[pt]= tree->children[nchildren++] = new_tree_node();
        node->move = pt;
    }
    tree->nchildren = nchildren;

    // Update the prior for the 'capture' and 3x3 patterns suggestions
    gen_playout_moves_capture(pos, allpoints, 1, 1, moves, sizes);
    int k=1;
    FORALL_IN_SLIST(moves, pt) {
        char* ret = play_move(pos, pt);
        if (ret[0] != 0) continue;
        undo_move(pos);
        node = childset[pt];
        if (node == NULL) { k++; continue; }
        if (sizes[k] == 1) {
            node->pv += PRIOR_CAPTURE_ONE;
            node->pw += PRIOR_CAPTURE_ONE;
        }
        else {
            node->pv += PRIOR_CAPTURE_MANY;
            node->pw += PRIOR_CAPTURE_MANY;
        }
        k++;
    }
    gen_playout_moves_pat3(pos, allpoints, 1, moves);
    FORALL_IN_SLIST(moves, pt) {
        char* ret = play_move(pos, pt);
        if (ret[0] != 0) continue;
        undo_move(pos);
        node = childset[pt];
        if (node == NULL) continue;
        node->pv += PRIOR_PAT3;
        node->pw += PRIOR_PAT3;
    }

    // Second pass setting priors, considering each move just once now
    copy_to_large_board(pos);                       // For large patterns
    *pos2 = *pos;
    for (int k=0 ; k<tree->nchildren ; k++) {
        node = tree->children[k];
        Point pt = node->move;
        play_move(pos2, pt);           // No need to check the move is legal

        if (board_last_move(pos) != PASS_MOVE 
            && cfg_map[pt]-1 < LEN_PRIOR_CFG) {
            node->pv += PRIOR_CFG[cfg_map[pt]-1];
            node->pw += PRIOR_CFG[cfg_map[pt]-1];
        }

        int height = line_height(pt, board_size(pos));  // 0-indexed
        if (height <= 2 && empty_area(pos, pt, 3)) {
            // No stones around; negative prior for 1st + 2nd line, positive
            // for 3rd line; sanitizes opening and invasions
            if (height <= 1) {
                node->pv += PRIOR_EMPTYAREA;
                node->pw += 0;
            }
            if (height == 2) {
                node->pv += PRIOR_EMPTYAREA;
                node->pw += PRIOR_EMPTYAREA;
            }
        }

        fix_atari(pos2, pt, SINGLEPT_OK, TWOLIBS_TEST, !TWOLIBS_EDGE_ONLY,
                                                                 moves, sizes);
        if (slist_size(moves) > 0) {
            node->pv += PRIOR_SELFATARI;
            node->pw += 0;  // negative prior
        }

        double patternprob = large_pattern_probability(pt);
        if (patternprob > 0.0) {
            double pattern_prior = sqrt(patternprob);       // tone up
            node->pv += pattern_prior * PRIOR_LARGEPATTERN;
            node->pw += pattern_prior * PRIOR_LARGEPATTERN;
        }
        undo_move(pos2);
    }

    if (tree->nchildren <= 2) {
        int nc = tree->nchildren;
        // add a pass move. Useful, for example in case of seki
        tree->children[nc] = new_tree_node();
        tree->children[nc]->move = PASS_MOVE;
        tree->nchildren = nc+1;
    }
}

void free_tree(TreeNode *tree)
// Free memory allocated for the tree
{
    if (tree->children != NULL) {
        for (TreeNode **child = tree->children ; *child != NULL ; child++)
                free_tree(*child);
        free(tree->children);
    }
    free(tree);
}

double rave_urgency(TreeNode *node)
{
    double v = node->v + node->pv;
    double expectation = (node->w + node->pw)/v;
    if (node->av==0) return expectation;

    double rave_expectation = (double) node->aw / (double) node->av;
    double beta = node->av / (node->av + v + (double)v*node->av/RAVE_EQUIV);
    return beta * rave_expectation + (1-beta) * expectation;
}

double winrate(TreeNode *node)
// Return the winrate (number of wins divided by the number of visits)
{
    double wr;
    if (node->v>0) wr = (double) node->w / (double) node->v;
    else           wr = -0.1;
    return wr;
}

void find_two_most_visited_children(TreeNode *tree, TreeNode **bests, int *v)
// find the two children that have been most visited
{
    int vmax=-1, vmax2=-1;
    TreeNode *c=NULL, *c2=NULL;

    if (tree->children != NULL) {
        for (TreeNode **child = tree->children ; *child != NULL ; child++) {
            if ((*child)->v > vmax2) {
                if ((*child)->v > vmax) {
                    vmax2 = vmax;
                    c2 = c;
                    vmax = (*child)->v;
                    c = *child;
                }
                else {
                    vmax2 = (*child)->v;
                    c2 = *child;
                }
            }
        }
    }
    bests[0] = c;
    bests[1] = c2;
    v[0] = vmax;
    v[1] = vmax2; 
}

TreeNode* best_move(TreeNode *tree, TreeNode **except)
// best move is the most simulated one (avoiding nodes in except list)
{
    int vmax=-1;
    TreeNode *best=NULL;

    if (tree->children == NULL) return NULL;

    for (TreeNode **child = tree->children ; *child != NULL ; child++) {
        if ((*child)->v > vmax) {
            int update = 1;
            if (except != NULL) 
                for (TreeNode **n=except ; *n!=NULL ; n++)
                    if (*child == *n) update=0;
            if (update) {
                vmax = (*child)->v;
                best = (*child);
            }
        }
    }
    return best;
}

void dump_subtree(TreeNode *node, double thres, char *indent, FILE *f
                                                                , int recurse);
TreeNode* most_urgent(TreeNode **children, int nchildren, int disp)
// Return the most urgent child to play
{
    int    k=0;
    double urgency, umax=0;
    TreeNode *urgent = children[0];

    // Randomize the order of the nodes
    SHUFFLE(TreeNode *, children, nchildren);

    for (TreeNode **child = children ; *child != NULL ; child++) {
        if (disp)
            dump_subtree(*child, N_SIMS/50, "", stderr, 0);
        urgency = rave_urgency(*child);
        if (urgency > umax) {
            umax = urgency;
            urgent = *child;
        }
        k++;
    }
    return urgent;
}

int tree_descend(Position *pos, TreeNode *tree, int amaf_map[], int disp
                                                            , TreeNode **nodes)
// Descend through the tree to a leaf
{
    int last=0, passes = 0;
    Point move;
    //tree->v += 1;
    nodes[last] = tree;
   
    while (nodes[last]->children != NULL && passes <2) {
        if (disp) print_pos(pos, stderr, NULL);
        // Pick the most urgent child
        TreeNode *node = most_urgent(nodes[last]->children, 
                                            nodes[last]->nchildren, disp);
        nodes[++last] = node;
        move = node->move;
        if (disp) { fprintf(stderr, "chosen "); ppoint(move); }

        if (move == PASS_MOVE) {
            passes++;
            pass_move(pos);
        }
        else {
            passes = 0;
            play_move(pos, move);
            if (amaf_map[move] == 0) //Mark the point with 1 for black
                amaf_map[move] = (board_color_to_play(pos) == BLACK ? -1 : 1);
        }

        if (node->children == NULL && node->v >= EXPAND_VISITS)
            expand(pos, node);
    }
    return last;
}

void tree_update(Position *pos, TreeNode **nodes, int last
                                    , int amaf_map[], double score, int disp)
// Store simulation result in the tree (nodes is the tree path)
{
    int amaf_map_value = 1;
    if (board_color_to_play(pos) == WHITE) {
        amaf_map_value = -1;
        score = -score;  // score > 0 means the player in the first node wins
    }
    for (int k=0 ; k<=last ; k++) {
        TreeNode *n= nodes[k];
        if(disp) {
            char str[8]; str_coord(n->move, str);
            fprintf(stderr, "updating %s %d\n", str, score<0.0); 
        }
        n->v += 1;         // TODO put it in tree_descend when parallelize
        n->w += score<0.0; // score is for to-play, node stats for just-played
        
        // Update the node children AMAF stats with moves we made 
        // with their color
        if (n->children != NULL) {
            for (TreeNode **child = n->children ; *child != NULL ; child++) {
                if ((*child)->move == 0) continue;
                if (amaf_map[(*child)->move] == amaf_map_value) {
                    if (disp) {
                        char str[8];
                        str_coord((*child)->move, str);
                        fprintf(stderr, "  AMAF updating %s %d\n", str,score>0);
                    }
                    (*child)->aw += score > 0; // reversed perspective
                    (*child)->av += 1;
                }
            }
        }
        score = -score;
        amaf_map_value = -amaf_map_value;
    }
}

float nplayouts, nplayouts_per_second=-1.0;
float start_playouts_sec, stop_playouts_sec;
int   nplayouts_real;
void update_speed()
// Update the number of playouts per seconds
{
    stop_playouts_sec=(float) clock() / (float) CLOCKS_PER_SEC;
    nplayouts_per_second = nplayouts_real / 
                            (stop_playouts_sec-start_playouts_sec); 
}

float best2, bestr, bestwr;
void collect_infos(TreeNode *tree, int n, TreeNode *best,
        TreeNode *workspace[], Position *pos)
{
    char str[10], str2[10], str_r[10];
    TreeNode *second, *reply;

    nplayouts_real += n;
    update_speed();

    workspace[0] = best; workspace[1] = 0;
    second = best_move(tree, workspace);
    bestwr= winrate(best);
    if (second != NULL) {
        best2 = bestwr / winrate(second);
        str_coord(second->move, str2);
    }
    else {
        best2 = 0.0;
        str2[0] = 0;
    }
    reply = best_move(best, NULL);
    if (reply != NULL) {
        bestr = bestwr - winrate(reply);
        str_coord(reply->move, str_r);
    }
    else {
        bestr = 0.0;
        str_r[0] = 0;
    }
    str_coord(best->move, str);
    sprintf(buf, "%12u %5.1f %3s (%3s) %3s %5.2f %6.3f %6.3f %7.1f"
               , idum, board_delta_komi(pos), str, str2, str_r
               , best2, bestr, bestwr, nplayouts_per_second);
    log_fmt_s('S', buf, NULL);
}

void print_tree_summary(TreeNode *tree, int sims, FILE *f);
Point tree_search(Position *pos, TreeNode *tree, int n, int owner_map[],
                                              int score_count[] , int disp)
// Perform MCTS search from a given position for a given number of iterations
{
    double s;
    int *amaf_map=michi_calloc(BOARDSIZE, sizeof(int)), i, last, visit[2]; 
    Point    bestmove;
    Position *workpos=michi_malloc(sizeof(Position));
    // FORK CHANGE: heap, not frame. Two kilobytes of pointers on the same
    // deepest path as expand()'s position above.
    TreeNode **nodes=michi_calloc(500, sizeof(TreeNode*));
    TreeNode *best, *bests[2];

    // Initialize the root node if necessary
    if (tree->children == NULL) expand(pos, tree);

    int live_gfx = strcmp(Live_gfx,"None") != 0;

    for (i=0 ; i<n/2 && !michi_out_of_time() ; i++) {
        if (live_gfx && (i % Live_gfx_interval) == Live_gfx_interval-1)
            display_live_gfx(pos, tree, owner_map);

        *workpos = *pos;
        memset(amaf_map, 0, BOARDSIZE*sizeof(int));
        if (i>0 && i % REPORT_PERIOD == 0) print_tree_summary(tree, i, stderr); 
        last = tree_descend(workpos, tree, amaf_map, disp, nodes);
        s = mcplayout(workpos, amaf_map, owner_map, score_count, disp);
        tree_update(pos, nodes, last, amaf_map, s, disp);
        // Early stop test
        best = best_move(tree, NULL);
        double best_wr = winrate(best);
        if ( (best_wr > FASTPLAY5_THRES && i>n*0.05)
              || (best_wr > FASTPLAY20_THRES && i>n*0.2)) {
            best = best_move(tree, NULL);
            sprintf(buf,"%10u tree_search() breaks at %d (%.3f)",
                                                    idum, i, winrate(best));
            log_fmt_s('I', buf, NULL);
            nplayouts_real += i;
            update_speed();
            goto finished;
        }
    } 

    for ( ; i<n && !michi_out_of_time() ; i++) {
        if (live_gfx && (i % Live_gfx_interval) == Live_gfx_interval-1)
            display_live_gfx(pos, tree, owner_map);
        *workpos = *pos;
        memset(amaf_map, 0, BOARDSIZE*sizeof(int));
        if (i>0 && i % REPORT_PERIOD == 0) 
            print_tree_summary(tree, i, stderr); 
        last = tree_descend(workpos, tree, amaf_map, disp, nodes);
        s = mcplayout(workpos, amaf_map, owner_map, score_count, disp);
        tree_update(pos, nodes, last, amaf_map, s, disp);

        find_two_most_visited_children(tree, bests, visit);
        //printf("%d %d %d %d  %.4lf %.4lf\n", i, n, visit[0], visit[1]
        //                        , winrate(bests[0]), winrate(bests[1]));
        if (visit[1] + n - i < visit[0]) { 
            sprintf(buf, "early abort save %d / %d simulations", n-i, n);
            log_fmt_s('S', buf, NULL);
            break;          // 2nd most visited child will not be able to win
        }
    }

    best = best_move(tree, NULL);
    collect_infos(tree, i, best, nodes, pos);

finished:
    if (verbosity > 0) {
        dump_subtree(tree, N_SIMS/50, "", stderr, 1);
        print_tree_summary(tree, i, stderr);
    }

    free(amaf_map); free(workpos); free(nodes);
    if (best->move == PASS_MOVE && board_last_move(pos) == PASS_MOVE)
        bestmove = PASS_MOVE;
    else if (((double) best->w / (double) best->v) < RESIGN_THRES) 
        bestmove = RESIGN_MOVE;
    else
        bestmove = best->move;
    return bestmove;
}

//-------------------------- Print of the MCTS tree ---------------------------
void dump_subtree(TreeNode *node, double thres, char *indent, FILE *f
                                                                , int recurse)
// print this node and all its children with v >= thres.
{
    char str[8], str_winrate[8], str_rave_winrate[8];
    str_coord(node->move, str);
    if (node->v) sprintf(str_winrate, "%.3f", winrate(node)); 
    else         sprintf(str_winrate, "nan");
    if (node->av) sprintf(str_rave_winrate, "%.3f", (double)node->aw/node->av); 
    else          sprintf(str_rave_winrate, "nan");
    fprintf(f,"%s+- %s %5s "
              "(%6d/%-6d, prior %3d/%-3d, rave %6d/%-6d=%5s, urgency %.3f)\n"
           ,indent, str, str_winrate, node->w, node->v, node->pw, node->pv
           ,node->aw, node->av, str_rave_winrate, rave_urgency(node));
    if (recurse) {
        char new_indent[BUFLEN];
        sprintf(new_indent,"%s   ", indent);
        for (TreeNode **child = node->children ; *child != NULL ; child++)
            if ((*child)->v >= thres)
                dump_subtree((*child), thres, new_indent, f, 0);
    }
}

void dump_node(TreeNode *node, FILE *f)
// Dump node
{
    char str[8];
    int k=1;
    fprintf(f,"child move   winrate      prior       rave      urgency nch\n");
    fprintf(f,"self  %4s %5d/%-5d %5d/%-5d %5d/%-5d %8.5lf %3d\n"
            , str_coord(node->move,str)
            , node->w, node->v, node->pw,node->pv, node->aw,node->av
            , rave_urgency(node), node->nchildren);
    if (node->nchildren) {
        for (TreeNode **c = node->children ; *c != NULL ; c++) {
            TreeNode*n = *c;
            fprintf(f,"%5d %4s %5d/%-5d %5d/%-5d %5d/%-5d %8.5lf %3d\n"
                    , k++, str_coord(n->move,str)
                    , n->w, n->v, n->pw, n->pv, n->aw, n->av
                    , rave_urgency(n), n->nchildren);
        }
    }
}

void compute_principal_variation(TreeNode *tree, Point moves[6])
// Return the best sequence of moves (according to the tree)
{
    TreeNode *node = tree;
    slist_clear(moves);
    for (int k=0 ; k<5 ; k++) {
        node = best_move(node, NULL);
        if (node == NULL) break;
        slist_push(moves, node->move);
    }
}

void compute_best_moves(TreeNode *tree, char sep[2]
                                        , TreeNode *best_node[5], char *can)
// Build a string with the coordinates and winrate of the 5 best moves
{
    char str[8], tmp[32];

    can[0] = 0;

    for (int k=0 ; k<5 ; k++) { 
        best_node[k] = best_move(tree, best_node);
        if (best_node[k] != NULL) {
            str_coord(best_node[k]->move, str);
            if (best_node[k]->v)
                sprintf(tmp, " %s%c%.3f%c", str
                        , sep[0], winrate(best_node[k]), sep[1]);
            else
                sprintf(tmp, " %s%c%s%c", str, sep[0], "nan", sep[1]);
            strcat(can, tmp);
        }
    }
}

void print_tree_summary(TreeNode *tree, int sims, FILE *f)
{
    char can[128];
    Point moves[6];
    TreeNode *best_node[5]={0,0,0,0,0};

    compute_principal_variation(tree, moves);
    compute_best_moves(tree, "()", best_node, can);

    fprintf(f,"[%4d] winrate %.3f | seq %s| can %s\n", sims,
            winrate(best_node[0]), slist_str_as_point(moves), can);   
}

//--------------------------- Print of the Position ---------------------------
void make_pretty(Position *pos, char pretty_board[BOARDSIZE], int *capB
                                                                , int *capW);

void print_pos(Position *pos, FILE *f, int *owner_map)
// Print visualization of the given board position
{
    char pretty_board[BOARDSIZE], strko[8];
    int  capB, capW;

    make_pretty(pos, pretty_board, &capB, &capW);
    fprintf(f,"Move: %-3d   Black: %d caps   White: %d caps   Komi: %.1f",
            board_nmoves(pos), capB, capW, board_komi(pos)); 
    if (board_ko(pos))
        fprintf(f,"   ko: %s", str_coord(board_ko(pos), strko)); 
    fprintf(f,"\n");

    int size = board_size(pos), skipped=N-size+1;
    int k=skipped*(N+1), k1=skipped*(N+1);
    for (int row=skipped ; row<=N ; row++) {
        if (board_last_move(pos) == k+1) fprintf(f, " %-2d(", N-row+1); 
        else                             fprintf(f, " %-2d ", N-row+1);
        k++; k1++;
        for (int col=1 ; col<=size ; col++, k++) {
            fprintf(f, "%c", pretty_board[k]);
            if (board_last_move(pos) == k+1)    fprintf(f, "(");
            else if (board_last_move(pos) == k) fprintf(f, ")");
            else                                fprintf(f, " ");
        }
        if (owner_map) {
            fprintf(f, "   ");
            for (int col=1 ; col<=size ; col++, k1++) {
                char c;
                if      ((double)owner_map[k1] > 0.6*nplayouts_real) c = 'X';
                else if ((double)owner_map[k1] > 0.3*nplayouts_real) c = 'x';
                else if ((double)owner_map[k1] <-0.6*nplayouts_real) c = 'O';
                else if ((double)owner_map[k1] <-0.3*nplayouts_real) c = 'o';
                else                                                 c = '.';
                fprintf(f, " %c", c);
            }
        }
        k += skipped-1; k1 += skipped-1;
        fprintf(f, "\n");
    }
    fprintf(f, "    ");
    for (int col=1 ; col<=size ; col++) fprintf(f, "%c ", colstr[col]);
    fprintf(f, "\n\n");
}
