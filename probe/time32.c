/* Build check: Box86 must be compiled with the 32-bit time_t the x86 programs use. */
#include <time.h>
_Static_assert(sizeof(time_t) == 4, "time_t must be 32 bits for Box86");
