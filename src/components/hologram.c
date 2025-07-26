#include "hologram.h"

// safety:
// For valid indexes to be valid,
// stuff must be from 0 to limit - 1
nn_size_t nn_positionToIndex(nn_hologram *h, unsigned x, unsigned y, unsigned z) {
	return x + y * h->width_x + z * h->width_x * h->height;
}

void nn_hologram_clear(nn_hologram *h) {
	
}

