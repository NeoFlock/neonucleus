#include "../neonucleus.h"

typedef struct nn_tunnel {
	nn_Context ctx;
	nn_refc refc;
	nn_guard *lock;
	nn_tunnelTable table;
	nn_networkControl ctrl;
} nn_tunnel;

nn_tunnel *nn_newTunnel(nn_Context *context, nn_tunnelTable table, nn_networkControl control) {
	nn_Alloc *a = &context->allocator;

	nn_tunnel *t = nn_alloc(a, sizeof(nn_tunnel));
	if(t == NULL) return NULL;
	t->lock = nn_newGuard(context);
	if(t->lock == NULL) {
		nn_dealloc(a, t, sizeof(nn_tunnel));
		return NULL;
	}
	t->ctx = *context;
	t->refc = 1;
	t->table = table;
	t->ctrl = control;
	return t;
}

nn_guard *nn_getTunnelLock(nn_tunnel *tunnel) {
	return tunnel->lock;
}

void nn_retainTunnel(nn_tunnel *tunnel) {
	nn_incRef(&tunnel->refc);
}

nn_bool_t nn_destroyTunnel(nn_tunnel *tunnel) {
	if(!nn_decRef(&tunnel->refc)) return false;
	return true;
}

void nn_tunnel_destroy(void *_, nn_component *component, nn_tunnel *tunnel) {
    nn_destroyTunnel(tunnel);
}

void nn_loadTunnelTable(nn_universe *universe) {
    nn_componentTable *tunnelTable = nn_newComponentTable(nn_getAllocator(universe), "tunnel", NULL, NULL, (nn_componentDestructor *)nn_tunnel_destroy);
    nn_storeUserdata(universe, "NN:TUNNEL", tunnelTable);
}

nn_component *nn_addTunnel(nn_computer *computer, nn_address address, int slot, nn_tunnel *tunnel) {
    nn_componentTable *tunnelTable = nn_queryUserdata(nn_getUniverse(computer), "NN:TUNNEL");
    return nn_newComponent(computer, address, slot, tunnelTable, tunnel);
}
