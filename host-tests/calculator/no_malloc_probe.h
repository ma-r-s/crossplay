/* Redirects decNumber's allocator to a counter. Included with -include so it
   reaches the vendored sources without a line of them changing.

   The C linkage is load-bearing: the counter is defined in a C++ file and the
   calls come from C, so without it the two are different symbols and the link
   fails on decDivideOp. */
#ifndef CALC_NO_MALLOC_PROBE_H
#define CALC_NO_MALLOC_PROBE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* calc_probe_malloc(size_t n);
void calc_probe_free(void* p);
extern int calc_probe_calls;
#ifdef __cplusplus
}
#endif
#endif
