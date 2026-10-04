// board.h -- Implementation of Go rules and data for Go heuristics
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <math.h>
#include <time.h>
#include "non_portable.h"
//========================= Definition of Data Structures =====================
// --------------------------- Board Constants --------------------------------
// N is the compile-time MAXIMUM board size -- the size actually played is
// pos->size, set at runtime -- so this one build serves 9x9, 13x13 and 19x19.
// 19 is upstream's own value. The fork ran 13 until 19x19 was offered (GitHub
// #282); the arrays sized by it live in PSRAM, see michi_alloc.
#define N          19
#define W         (N+2)
#define BOARDSIZE ((N+1)*W+1)
#define BOARD_IMIN (N+1)
#define BOARD_IMAX (BOARDSIZE-N-1)
#define LARGE_BOARDSIZE ((N+14)*(N+7))

#if N < 11
    #define LIBS_SIZE  4
    #define MAX_BLOCKS 64
#elif N < 15
    #define LIBS_SIZE  8
    #define MAX_BLOCKS 128
#else
    #define LIBS_SIZE 12
    #define MAX_BLOCKS 256
#endif

#define BUFLEN 4096
#define INVALID_VERTEX  999
//------------------------------- Data Structures -----------------------------
typedef unsigned char Byte;
typedef unsigned char Block;
typedef unsigned int  Code4;  // 4 bits code that defines a neighbors selection 
typedef unsigned int  Info;
typedef unsigned int  Libs;   // 32 bits unsigned integer
typedef unsigned int  Point;
typedef unsigned long long ZobristHash;
typedef Info* Slist;
typedef enum {
    PASS_MOVE, RESIGN_MOVE, 
    COMPUTER_BLACK, COMPUTER_WHITE, COMPUTER_BOTH
} Code;
typedef enum {EMPTY=0, OUT=1, WHITE=2, BLACK=3, BOTH=4} Color;
typedef enum {UNKNOWN, OWN_BY_BLACK, OWN_BY_WHITE, ALIVE, DEAD} Status;

typedef struct { // ---------------------- Go Position ------------------------
// Given a board of size NxN (N=9, 19, ...), we represent the position
// as a (N+1)*(N+2)+1 string, with '.' (empty), 'X' (to-play player)
// 'x' (other player), and whitespace (off-board border to make rules
// implementation easier).  Coordinates are just indices in this string.
    Color color[BOARDSIZE];   // string that hold the state of the board
    Byte  env4[BOARDSIZE];    // color encoding for the 4 neighbors
    Byte  env4d[BOARDSIZE];   // color encoding for the 4 diagonal neighbors
    Block block[BOARDSIZE];   // block id of the block at point pt (0 if EMPTY)
    Byte  nlibs[MAX_BLOCKS];          // number of liberties of block b
    Byte  bsize[MAX_BLOCKS];          // number of stones of block b
    Libs  libs[MAX_BLOCKS][LIBS_SIZE];// list of liberties of block b(bitfield) 
    Byte  libs_size;                  // actual size of the libs (32 bits words)

    int   size;               // current size of the board
    int   n;                  // move number
    Color to_play;            // player that move
    Point ko, ko_old;         // position of the ko (0 if no ko)
    Point last, last2, last3; // position of the last move and the move before 
    float komi;               // komi for the game
    float delta_komi;         // used for dynamic komi management
    int   caps[2];            // number of stones captured 
    Byte  undo_capture;       // helper for undo_move()
} Position;         // Go position

typedef struct {
    int        value;
    int        in_use;
    int        mark[BOARDSIZE];
} Mark;

// -------------------------------- Global Data -------------------------------
extern Byte bit[8];
extern Mark *mark1, *mark2;
extern FILE *flog;                          // FILE to log messages
extern int  c1,c2;                          // counters for messages
extern int  verbosity;                      // 0, 1 or 2
extern char large_board[LARGE_BOARDSIZE];
extern int  large_coord[BOARDSIZE]; // mapping: c in board -> cl in large_board

//================================== Code =====================================
//--------------------------- Functions in board.c ----------------------------
void   board_init(void);
void   board_finish(void);
void   block_compute_libs(Position *pos, Block b, Slist libs, int max_libs);
int    cmpint(const void *i, const void *j);
void   compute_big_eye(Position *pos, Point pt, Slist points);
void   compute_block(Position *pos, Point pt, Slist stones, Slist libs,
                                                                    int nlibs);
void   compute_cfg_distances(Position *pos, Point pt, char cfg_map[BOARDSIZE]);
Byte   compute_env4(Position *pos, Point pt, int offset);
int    empty_area(Position *pos, Point pt, int dist);
char*  empty_position(Position *pos);
char   is_eye(Position *pos, Point pt);
char   is_eyeish(Position *pos, Point pt);
int    line_height(Point pt, int size);
void   make_list_last_moves_neighbors(Position *pos, Slist points);
void   make_list_neighbor_blocks_in_atari(Position *pos, Block b, Slist blocks,
                                                                     Point pt);
Position* new_position();
char*  pass_move(Position *pos);
char*  play_move(Position *pos, Point pt);
char*  undo_move(Position *pos);
//----------------------- Functions in board_util.c  --------------------------
int    all_blocks_OK(Position *pos);
int    blocks_OK(Position *pos, Point pt);
char*  block_liberties_as_string(Position *pos, Block b);
void   copy_to_large_board(Position *pos);
int    env4_OK(Position *pos);
void   fatal_error(const char *msg);
void   init_large_board(void);
// FORK CHANGE: compute_cfg_distances()'s flood-fill queue, allocated rather
// than a 25KB frame. Defined in michi.c beside the other search scratches;
// declared here because board.c does not include michi.h.
Point* michi_cfg_fringe(void);
void   log_fmt_i(char type, const char *msg, int n);
void   log_fmt_p(char type, const char *msg, Point i);
void   log_fmt_s(char type, const char *msg, const char *s);
void*  michi_calloc(size_t nmemb, size_t size);
void*  michi_malloc(size_t size);
Point  parse_coord(char *s);
Point  parse_sgf_coord(char *s, int size);
void   print_board(Position *pos, FILE *f);
void   ppoint(Point pt);
void   slist_print_as_point(Slist l);
void   slist_print_as_int(Slist l);
char*  slist_str_as_point(Slist l);
char*  slist_from_str_as_point(Slist l, char *str);
void   slist_write_as_int(Slist l, FILE *f);
void   slist_write_as_point(Slist l, FILE *f);
char*  str_coord(Point pt, char str[5]);
char*  str_sgf_coord(Point pt, char str[8], int size);
//-------------------- Functions inlined for performance ----------------------
#ifndef _MSC_VER
    #define __INLINE__ static inline
#else
    #define __INLINE__ static __inline
#endif
// 4 bits codes giving the neighbors of a given color
// bit 0(LSB): North, 1:East, 2:South, 3:West bit set(1) if the neighbor match
__INLINE__ Code4 select_black(Byte env4) {return (env4>>4) & env4;}
__INLINE__ Code4 select_empty(Byte env4) {return (~((env4>>4) | env4)) & 0xf;}
__INLINE__ Code4 select_white(Byte env4) {return (env4>>4) & (~env4);}
__INLINE__ int   code4_popcnt(Code4 code) {return popcnt_u32(code);}
// Access to board structure attribute
__INLINE__ int   block_nlibs(Position *pos, Block b)  {return pos->nlibs[b];}
__INLINE__ int   block_size(Position *pos, Block b)   {return pos->bsize[b];}
__INLINE__ Color board_color_to_play(Position *pos) {return pos->to_play;}
__INLINE__ float board_delta_komi(Position *pos) {return pos->delta_komi;}
__INLINE__ float board_komi(Position *pos) {return pos->komi;}
__INLINE__ Point board_ko(Position *pos) {return pos->ko;}
__INLINE__ Point board_ko_old(Position *pos) {return pos->ko_old;}
__INLINE__ int   board_last_move(Position *pos) {return pos->last;}
__INLINE__ Point board_last2(Position *pos) {return pos->last2;}
__INLINE__ Point board_last3(Position *pos) {return pos->last3;}
__INLINE__ Info  board_nmoves(Position *pos) {return (Info) pos->n;}
__INLINE__ int   board_nstones(Position *pos) 
            {return pos->n - pos->caps[0] - pos->caps[1];} // ignore PASS moves
__INLINE__ void  board_set_color_to_play(Position *pos, Color c) 
                                                    { pos->to_play = c;}
__INLINE__ void  board_place_stone(Position *pos, Point pt, Color c) {
    board_set_color_to_play(pos, c);
    play_move(pos, pt);
    pos->n--;
}
__INLINE__ void  board_set_captured_neighbors(Position *pos, int code4) 
                                                   {pos->undo_capture = code4;}
__INLINE__ void  board_set_delta_komi(Position *pos, float dkm) 
                                                {pos->delta_komi = dkm;}
__INLINE__ void  board_set_komi(Position *pos, float km) {pos->komi = km;}
__INLINE__ void  board_set_ko(Position *pos, Point ko) {pos->ko = ko;}
__INLINE__ void  board_set_ko_old(Position *pos, Point ko) {pos->ko_old = ko;}
__INLINE__ void  board_set_last(Position *pos, Point pt) {pos->last = pt;}
__INLINE__ void  board_set_last2(Position *pos, Point pt) {pos->last2 = pt;}
__INLINE__ void  board_set_last3(Position *pos, Point pt) {pos->last3 = pt;}
__INLINE__ void  board_set_nmoves(Position *pos, int n) {pos->n = n;}
__INLINE__ void  board_set_size(Position *pos, int sz) {pos->size = sz;}
__INLINE__ int   board_size(Position *pos) {return pos->size;}
__INLINE__ int   board_captured_neighbors(Position *pos) 
                                                   {return pos->undo_capture;}
__INLINE__ Color color_other(Color c)                 {return c ^ 1;}
__INLINE__ Block point_block(Position *pos, Point pt) {return pos->block[pt];}
__INLINE__ Color point_color(Position *pos, Point pt) {return pos->color[pt];}
__INLINE__ Byte  point_env4(Position *pos, Point pt)  {return pos->env4[pt];}
__INLINE__ Byte  point_env4d(Position *pos, Point pt) {return pos->env4d[pt];}
__INLINE__ int   point_env8(Position *pos, Point pt)  
                               {return (pos->env4d[pt] << 8) + pos->env4[pt];}
__INLINE__ int   point_is_stone(Position *pos, Point pt)  
                                {return (point_color(pos, pt) & 2) != 0;}
__INLINE__ int   point_nlibs(Position *pos, Point pt) 
                            {return code4_popcnt(select_empty(pos->env4[pt]));}
//-------------------------- Definition of Useful Macros ----------------------
#define FORALL_POINTS(pos,i) for(Point i=BOARD_IMIN ; i<BOARD_IMAX ; i++)
#define FORALL_NEIGHBORS(pos, pt, k, n) \
    for(k=0,n=pt+delta[0] ; k<4 ; n=pt+delta[++k])
#define FORALL_DIAGONAL_NEIGHBORS(pos, pt, k, n) \
    for(k=4,n=pt+delta[4] ; k<8 ; n=pt+delta[++k])
#define FORALL_IN_SLIST(l, item) \
    for(int _k=1,_n=l[0],item=l[1] ; _k<=_n ; item=l[++_k])
#define SWAP(T, u, v) {T _t = u; u = v; v = _t;}
// Random shuffle: Knuth. The Art of Computer Programming vol.2, 2nd Ed, p.139
#define SHUFFLE(T, l, n) for(int _k=n-1 ; _k>0 ; _k--) {  \
    int _tmp=random_int(_k+1); SWAP(T, l[_k], l[_tmp]); \
}
// Assertion check
//
// FORK CHANGE: OFF unless MICHI_ASSERTS is defined, where upstream has them on
// unless NDEBUG is. Two reasons, and the second is the one that decided it.
//
// These are upstream's internal consistency checks, and their failure path is
// exit() -- which on this device is a reboot, so an assert that fires is no
// better than the corruption it found. And they are not free: the path through
// undo_move -> all_blocks_OK -> check_block is 4,400 bytes of the search task's
// stack, on a chip where that task's whole budget is measured in tens of
// kilobytes.
//
// host-tests/go/run.sh defines MICHI_ASSERTS, so the suite keeps them. That is
// where they can actually be read, and where one of them caught the
// uninitialised large board.
#if defined(NDEBUG) || !defined(MICHI_ASSERTS)
  #define michi_assert(pos, condition)
#else
  #define michi_assert(pos, condition)                                        \
    if (!condition) {                                                         \
        print_board(pos, flog);                                               \
        sprintf(buf,"Assertion failed: file %s, line %d",__FILE__,__LINE__);  \
        log_fmt_s('E',buf,NULL);                                              \
        exit(-1);                                                             \
    }
#endif
//------------ Useful utility functions (inlined for performance) -------------
// Go programs manipulates lists or sets of (small) integers a lot. There are 
// many possible implementations that differ by the performance of the various
// operations we need to perform on these data structures.
// insert, remove, enumerate elements, test "is in ?", make the set empty ...
//
// Here are 2 simple implementations of sets
// * Slist is a simple list implementation as integer arrays (not sorted)
//   - it is slow for finding or inserting without dupplicate except for very
//     small sets.
//   - but fast to enumerate elements or insert when there is no need to check
//     for dupplicates
// * Mark
//   - it is very fast for insert, remove elements, making the set empty or to
//     test "is in ?"
//   - but slow if we need to enumerate all the elements
// These two representations are necessary for performance even with the goal
// of brevity that is a priority for michi (actually the code is very short).
//
// Note: the pat3_set in patterns.c is a representation of a set as bitfield.

// Random Number generator (32 bits Linear Congruential Generator)
// Ref: Numerical Recipes in C (W.H. Press & al), 2nd Ed page 284
extern unsigned int idum;
__INLINE__ unsigned int qdrandom(void) {idum=(1664525*idum)+1013904223; return idum;}
__INLINE__ unsigned int random_int(int n) /* random int between 0 and n-1 */ \
           {unsigned long long r=qdrandom(); return (r*n)>>32;}

// Simple list
__INLINE__ int  slist_size(Slist l) {return l[0];}
__INLINE__ void slist_clear(Slist l) {l[0]=0;}
__INLINE__ Info slist_pop(Slist l) {return l[l[0]--];}
__INLINE__ void slist_push(Slist l, Info item) {l[l[0]+1]=item;l[0]++;}
__INLINE__ void slist_range(Slist l,int n)
        {l[0]=n; for (int k=0;k<n;k++) l[k+1]=k;}

__INLINE__ int  slist_insert(Slist l, Info item)
{
    int k, n=l[0] + 1;
    l[n] = item;
    for (k=1 ; k<=n ; k++) 
        if(l[k] == item) break;
    if(k == n) {
        l[0] = n; return 1;   // return 1 if the item has been inserted
    }
    else return 0;
}
__INLINE__ void slist_append(Slist dest, Slist src)
{
    FORALL_IN_SLIST(src, i)
        slist_push(dest, i);
}
__INLINE__ void slist_shuffle(Slist l) { Info *t=l+1; SHUFFLE(Info,t,l[0]); }
__INLINE__ void slist_sort(Slist l) {qsort(l+1,l[0],sizeof(Info),&cmpint);}

// Quick marker : idea borrowed from the Gnugo codebase
__INLINE__ void mark_init(Mark *m) {m->in_use = 1; m->value++;}
__INLINE__ void mark_release(Mark *m) {m->in_use = 0;}   // for debugging
__INLINE__ void mark(Mark *m, Info i) {m->mark[i] = m->value;}
__INLINE__ int  is_marked(Mark *m, Info i) {return m->mark[i] == m->value;}
