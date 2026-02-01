// Includes all the C files to compile the entire library in one Compilation Unit
// this is better than LTO, but it requires more RAM and compute.

#include "nn_utils.h"

#if defined(__STDC_NO_THREADS__) || defined(NN_WINDOWS)
#include "tinycthread.c"
#endif

#include "nn_utils.c"
#include "nn_model.c"
