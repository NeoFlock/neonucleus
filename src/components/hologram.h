#ifndef NN_HOLOGRAM_H
#define NN_HOLOGRAM_H

#include "../neonucleus.h"

typedef struct nn_hologram { 
    nn_Context ctx;
    nn_guard *lock;
    nn_refc refc;

	int pallette_len;
	int* pallette_array;

	int width_x;
	int width_z;
	int height;

	float minScale;
	float maxScale;
	float scale;
	int depth;

	float min_translationX;
	float max_translationX;
	float translationX;

	float min_translationY;
	float max_translationY;
	float translationY;

	float min_translationZ;
	float max_translationZ;
	float translationZ;

	int* grid; // I don't know what to call this
} nn_hologram;

#endif