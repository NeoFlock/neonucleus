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

static void nni_tunnel_maxPacketSize(nn_tunnel *tunnel, void *_, nn_component *component, nn_computer *computer) {
	nn_return_integer(computer, tunnel->table.maxPacketSize);
}

static void nni_tunnel_maxValues(nn_tunnel *tunnel, void *_, nn_component *component, nn_computer *computer) {
	nn_return_integer(computer, tunnel->table.maxValues);
}

static void nni_tunnel_getChannel(nn_tunnel *tunnel, void *_, nn_component *component, nn_computer *computer) {
	nn_errorbuf_t err = "";
	char buf[NN_MAX_CHANNEL_SIZE];
	nn_lock(&tunnel->ctx, tunnel->lock);
	nn_size_t len = tunnel->table.getChannel(tunnel->table.userdata, buf, err);
	nn_unlock(&tunnel->ctx, tunnel->lock);
	if(!nn_error_isEmpty(err)) {
		nn_setError(computer, err);
		return;
	}
	nn_return_string(computer, buf, len);
}

static void nni_tunnel_getWakeMessage(nn_tunnel *tunnel, void *_, nn_component *component, nn_computer *computer) {
	nn_errorbuf_t err = "";
	char buf[NN_MAX_CHANNEL_SIZE];
	nn_lock(&tunnel->ctx, tunnel->lock);
	nn_size_t len = tunnel->table.getWakeMessage(tunnel->table.userdata, buf, err);
	nn_unlock(&tunnel->ctx, tunnel->lock);
	if(!nn_error_isEmpty(err)) {
		nn_setError(computer, err);
		return;
	}
	nn_return_string(computer, buf, len);
}

static void nni_tunnel_setWakeMessage(nn_tunnel *tunnel, void *_, nn_component *component, nn_computer *computer) {
	nn_size_t buflen;
	const char *buf = nn_toString(nn_getArgument(computer, 0), &buflen);

	if(buf == NULL) {
		nn_setCError(computer, "invalid wake message");
		return;
	}

	if(buflen > NN_MAX_WAKEUPMSG) {
		buflen = NN_MAX_WAKEUPMSG;
	}

	nn_bool_t fuzzy = nn_toBoolean(nn_getArgument(computer, 1)); // nil is false

	nn_errorbuf_t err = "";

    nn_lock(&tunnel->ctx, tunnel->lock);
	buflen = tunnel->table.setWakeMessage(tunnel->table.userdata, buf, buflen, fuzzy, err);
	nn_unlock(&tunnel->ctx, tunnel->lock);

	if(!nn_error_isEmpty(err)) {
		nn_setError(computer, err);
		return;
	}

	nn_return_string(computer, buf, buflen);
}

static void nni_tunnel_send(nn_tunnel *tunnel, void *_, nn_component *component, nn_computer *computer) {
    nn_value vals[tunnel->table.maxValues];
    nn_size_t valLen = nn_getArgumentCount(computer);

    if(valLen > tunnel->table.maxValues) {
        nn_setCError(computer, "too many values");
        return;
    }

    for(nn_size_t i = 0; i < valLen; i++) {
        vals[i] = nn_getArgument(computer, i);
    }

	nn_size_t bytesSent = nn_measurePacketSize(vals, valLen);
	if(bytesSent > tunnel->table.maxPacketSize) {
        nn_setCError(computer, "packet too big");
        return;
	}
	nn_simulateBufferedIndirect(component, bytesSent, tunnel->ctrl.packetBytesPerTick);
	double d = (double)bytesSent / tunnel->table.maxPacketSize;
	nn_addHeat(computer, d * tunnel->ctrl.heatPerFullPacket);
	nn_removeEnergy(computer, d * tunnel->ctrl.energyPerFullPacket);

    nn_errorbuf_t err = "";
    nn_lock(&tunnel->ctx, tunnel->lock);
    tunnel->table.send(tunnel->table.userdata, vals, valLen, err);
    nn_unlock(&tunnel->ctx, tunnel->lock);
    if(!nn_error_isEmpty(err)) {
        nn_setError(computer, err);
        return;
    }

    nn_return_boolean(computer, true);
}

void nn_loadTunnelTable(nn_universe *universe) {
    nn_componentTable *tunnelTable = nn_newComponentTable(nn_getAllocator(universe), "tunnel", NULL, NULL, (nn_componentDestructor *)nn_tunnel_destroy);
    nn_storeUserdata(universe, "NN:TUNNEL", tunnelTable);
    
	nn_defineMethod(tunnelTable, "maxPacketSize", (nn_componentMethod *)nni_tunnel_maxPacketSize, "maxPacketSize(): integer - Returns the maximum size of a packet");
	nn_defineMethod(tunnelTable, "maxValues", (nn_componentMethod *)nni_tunnel_maxValues, "maxValues(): integer - the maximum number of values which can be sent in a packet");
	nn_defineMethod(tunnelTable, "getChannel", (nn_componentMethod *)nni_tunnel_getChannel, "getChannel(): string - Gets the ID of the communications channel of the tunnel. Under normal conditions, there are only 2 cards in the same universe which share this.");
	nn_defineMethod(tunnelTable, "getWakeMessage", (nn_componentMethod *)nni_tunnel_getWakeMessage, "getWakeMessage(): string - Returns the current wake message");
	nn_defineMethod(tunnelTable, "setWakeMessage", (nn_componentMethod *)nni_tunnel_setWakeMessage, "setWakeMessage(msg: string[, fuzzy: boolean]): string - Sets the new wake message");
	nn_defineMethod(tunnelTable, "send", (nn_componentMethod *)nni_tunnel_send, "send(...): boolean - Sends a tunnel message. Returns whether it was sent.");
}

nn_component *nn_addTunnel(nn_computer *computer, nn_address address, int slot, nn_tunnel *tunnel) {
    nn_componentTable *tunnelTable = nn_queryUserdata(nn_getUniverse(computer), "NN:TUNNEL");
    return nn_newComponent(computer, address, slot, tunnelTable, tunnel);
}
