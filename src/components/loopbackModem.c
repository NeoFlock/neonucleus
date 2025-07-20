#include "../neonucleus.h"

typedef struct nn_modemLoop {
    nn_Context ctx;
    nn_debugLoopbackNetworkOpts opts;
    nn_size_t *openPorts;
    nn_size_t strength;
    char wakeup[NN_MAX_WAKEUPMSG];
    nn_size_t wakeupLen;
} nn_modemLoop;

void nn_loopModem_deinit(nn_modemLoop *loop) {
    nn_Context ctx = loop->ctx;

	nn_deallocStr(&ctx.allocator, loop->opts.address);
    nn_dealloc(&ctx.allocator, loop->openPorts, loop->opts.maxOpenPorts * sizeof(nn_size_t));
    nn_dealloc(&ctx.allocator, loop, sizeof(nn_modemLoop));
}

nn_bool_t nn_loopModem_isOpen(nn_modemLoop *loop, nn_size_t port, nn_errorbuf_t err) {
    for(nn_size_t i = 0; i < loop->opts.maxOpenPorts; i++) {
        if(loop->openPorts[i] == port) {
			return true;
		}
	}

	return false;
}

nn_bool_t nn_loopModem_open(nn_modemLoop *loop, nn_size_t port, nn_errorbuf_t err) {
    int slot = -1;
    for(nn_size_t i = 0; i < loop->opts.maxOpenPorts; i++) {
        if(loop->openPorts[i] == NN_PORT_CLOSEALL) {
            slot = i;
            break;
		}
	}

    if(slot == -1) {
        nn_error_write(err, "too many open ports");
        return false;
    }

	loop->openPorts[slot] = port;

	return true;
}

nn_bool_t nn_loopModem_close(nn_modemLoop *loop, nn_size_t port, nn_errorbuf_t err) {
	if(port == NN_PORT_CLOSEALL) {
		for(nn_size_t i = 0; i < loop->opts.maxOpenPorts; i++) loop->openPorts[i] = NN_PORT_CLOSEALL;
		return true;
	}

    for(nn_size_t i = 0; i < loop->opts.maxOpenPorts; i++) {
        if(loop->openPorts[i] == port) {
			loop->openPorts[i] = NN_PORT_CLOSEALL;
            return true;
		}
	}

	nn_error_write(err, "port already closed");
	return false;
}

nn_size_t nn_loopModem_getPorts(nn_modemLoop *loop, nn_size_t *ports, nn_errorbuf_t err) {
	nn_size_t len = 0;
    for(nn_size_t i = 0; i < loop->opts.maxOpenPorts; i++) {
        if(loop->openPorts[i] != NN_PORT_CLOSEALL) {
			ports[len] = loop->openPorts[i];
			len++;
		}
	}
	return len;
}

nn_bool_t nn_loopModem_send(nn_modemLoop *loop, nn_address address, nn_size_t port, nn_value *values, nn_size_t valuec, nn_errorbuf_t err) {
	if(address == NULL) {
		// broadcasting, set it to our address
		address = loop->opts.address;
	}

	// error is discarded as packet loss
	nn_pushNetworkMessage(loop->opts.computer, loop->opts.address, address, port, loop->strength, values, valuec);

	return true;
}

double nn_loopModem_getStrength(nn_modemLoop *loop, nn_errorbuf_t err) {
	return loop->strength;
}

double nn_loopModem_setStrength(nn_modemLoop *loop, double n, nn_errorbuf_t err) {
	loop->strength = n;
	return n;
}

nn_size_t nn_loopModem_getWakeMessage(nn_modemLoop *loop, char *msg, nn_errorbuf_t err) {
	nn_memcpy(msg, loop->wakeup, loop->wakeupLen);
	return loop->wakeupLen;
}

nn_size_t nn_loopModem_setWakeMessage(nn_modemLoop *loop, const char *msg, nn_size_t msglen, nn_bool_t fuzzy, nn_errorbuf_t err) {
	loop->wakeupLen = msglen;
	nn_memcpy(loop->wakeup, msg, loop->wakeupLen);
	return loop->wakeupLen;
}

nn_modem *nn_debugLoopbackModem(nn_Context *context, nn_debugLoopbackNetworkOpts opts, nn_networkControl control) {
    opts.address = nn_strdup(&context->allocator, opts.address);
    nn_modemLoop *m = nn_alloc(&context->allocator, sizeof(nn_modemLoop));
    m->ctx = *context;
    m->opts = opts;
    m->strength = opts.maxStrength;
    m->wakeupLen = 0;
    m->openPorts = nn_alloc(&context->allocator, opts.maxOpenPorts * sizeof(nn_size_t));
    for(nn_size_t i = 0; i < opts.maxOpenPorts; i++) {
        m->openPorts[i] = NN_PORT_CLOSEALL; // used as a NULL port
    }
    nn_modemTable table = {
        .userdata = m,
        .deinit = (void *)nn_loopModem_deinit,

        .wireless = opts.isWireless,
        .maxValues = opts.maxValues,
        .maxOpenPorts = opts.maxOpenPorts,
        .maxPacketSize = opts.maxPacketSize,

		.isOpen = (void *)nn_loopModem_isOpen,
		.open = (void *)nn_loopModem_open,
		.close = (void *)nn_loopModem_close,
		.getPorts = (void *)nn_loopModem_getPorts,

		.send = (void *)nn_loopModem_send,

        .maxStrength = opts.maxStrength,
		.getStrength = (void *)nn_loopModem_getStrength,
		.setStrength = (void *)nn_loopModem_setStrength,

		.setWakeMessage = (void *)nn_loopModem_setWakeMessage,
		.getWakeMessage = (void *)nn_loopModem_getWakeMessage,
    };
    return nn_newModem(context, table, control);
}
