#include "../neonucleus.h"

typedef struct nn_modem {
    nn_Context ctx;
    nn_guard *lock;
    nn_refc refc;
    nn_modemTable table;
    nn_networkControl ctrl;
} nn_modem;

nn_modem *nn_newModem(nn_Context *context, nn_modemTable table, nn_networkControl control) {
    nn_modem *m = nn_alloc(&context->allocator, sizeof(nn_modem));
    if(m == NULL) return m;
    m->ctx = *context;
    m->lock = nn_newGuard(context);
    m->refc = 1;
    m->table = table;
    m->ctrl = control;
    return m;
}

nn_guard *nn_getModemLock(nn_modem *modem) {
    return modem->lock;
}

void nn_retainModem(nn_modem *modem) {
    nn_incRef(&modem->refc);
}

nn_bool_t nn_destroyModem(nn_modem *modem) {
    if(!nn_decRef(&modem->refc)) return false;

    if(modem->table.deinit != NULL) {
        modem->table.deinit(modem->table.userdata);
    }

    nn_Context ctx = modem->ctx;
    nn_deleteGuard(&ctx, modem->lock);
    nn_dealloc(&ctx.allocator, modem, sizeof(nn_modem));
    return true;
}

void nn_modem_destroy(void *_, nn_component *component, nn_modem *modem) {
    nn_destroyModem(modem);
}

static void nni_modem_isWireless(nn_modem *modem, void *_, nn_component *component, nn_computer *computer) {
    nn_return_boolean(computer, modem->table.wireless);
}

static void nni_modem_maxPacketSize(nn_modem *modem, void *_, nn_component *component, nn_computer *computer) {
    nn_return_integer(computer, modem->table.maxPacketSize);
}

static void nni_modem_maxOpenPorts(nn_modem *modem, void *_, nn_component *component, nn_computer *computer) {
    nn_return_integer(computer, modem->table.maxOpenPorts);
}

static void nni_modem_maxValues(nn_modem *modem, void *_, nn_component *component, nn_computer *computer) {
    nn_return_integer(computer, modem->table.maxValues);
}

static nn_bool_t nni_modem_wirelessOnly(nn_modem *modem, void *_) {
    return modem->table.wireless;
}

static void nni_modem_maxStrength(nn_modem *modem, void *_, nn_component *component, nn_computer *computer) {
    nn_return_number(computer, modem->table.maxStrength);
}

static void nni_modem_getStrength(nn_modem *modem, void *_, nn_component *component, nn_computer *computer) {
	nn_errorbuf_t err = "";

	nn_lock(&modem->ctx, modem->lock);
	double n = modem->table.getStrength(modem->table.userdata, err);
	nn_unlock(&modem->ctx, modem->lock);

	if(!nn_error_isEmpty(err)) {
		nn_setError(computer, err);
		return;
	}

	nn_return_number(computer, n);
}

static void nni_modem_setStrength(nn_modem *modem, void *_, nn_component *component, nn_computer *computer) {
	double n = nn_toNumber(nn_getArgument(computer, 0));

	if(n < 0) n = 0;
	if(n > modem->table.maxStrength) n = modem->table.maxStrength;
   
	nn_errorbuf_t err = "";

	nn_lock(&modem->ctx, modem->lock);
	n = modem->table.setStrength(modem->table.userdata, n, err);
	nn_unlock(&modem->ctx, modem->lock);

	if(!nn_error_isEmpty(err)) {
		nn_setError(computer, err);
		return;
	}

	nn_return_number(computer, n);
}

static nn_bool_t nni_modem_validSendPort(nn_intptr_t port) {
    // 9 quintillion ports just died
    if(port < 0) return false;
    // the only valid range
    if(port > NN_PORT_MAX) return false;
    if(port < 1) return false;
    // whichever it is reserved as (clean code moment)
    if(port == NN_TUNNEL_PORT) return false;
    return true;
}

static void nni_modem_isOpen(nn_modem *modem, void *_, nn_component *component, nn_computer *computer) {
    nn_intptr_t port = nn_toInt(nn_getArgument(computer, 0));
    if(!nni_modem_validSendPort(port)) {
        nn_setCError(computer, "invalid port");
        return;
    }
    nn_errorbuf_t err = "";
    nn_lock(&modem->ctx, modem->lock);
    nn_bool_t res = modem->table.isOpen(modem->table.userdata, port, err);
    nn_unlock(&modem->ctx, modem->lock);
    if(!nn_error_isEmpty(err)) {
        nn_setError(computer, err);
        return;
    }
    nn_return_boolean(computer, res);
}

static void nni_modem_open(nn_modem *modem, void *_, nn_component *component, nn_computer *computer) {
    nn_intptr_t port = nn_toInt(nn_getArgument(computer, 0));
    if(!nni_modem_validSendPort(port)) {
        nn_setCError(computer, "invalid port");
        return;
    }
    nn_errorbuf_t err = "";
    nn_lock(&modem->ctx, modem->lock);
    nn_bool_t res = modem->table.open(modem->table.userdata, port, err);
    nn_unlock(&modem->ctx, modem->lock);
    if(!nn_error_isEmpty(err)) {
        nn_setError(computer, err);
        return;
    }
    nn_return_boolean(computer, res);
}

static void nni_modem_close(nn_modem *modem, void *_, nn_component *component, nn_computer *computer) {
    nn_value portVal = nn_getArgument(computer, 0);
    nn_intptr_t port = portVal.tag == NN_VALUE_NIL ? NN_PORT_CLOSEALL : nn_toInt(portVal);
    if(!nni_modem_validSendPort(port) && port != NN_PORT_CLOSEALL) {
        nn_setCError(computer, "invalid port");
        return;
    }
    nn_errorbuf_t err = "";
    nn_lock(&modem->ctx, modem->lock);
    nn_bool_t res = modem->table.close(modem->table.userdata, port, err);
    nn_unlock(&modem->ctx, modem->lock);
    if(!nn_error_isEmpty(err)) {
        nn_setError(computer, err);
        return;
    }
    nn_return_boolean(computer, res);
}

static void nni_modem_getPorts(nn_modem *modem, void *_, nn_component *component, nn_computer *computer) {
    nn_size_t ports[modem->table.maxOpenPorts];
    nn_errorbuf_t err = "";
    nn_lock(&modem->ctx, modem->lock);
    nn_size_t portCount = modem->table.getPorts(modem->table.userdata, ports, err);
    nn_unlock(&modem->ctx, modem->lock);
    if(!nn_error_isEmpty(err)) {
        nn_setError(computer, err);
        return;
    }

    nn_value arr = nn_return_array(computer, portCount);
    for(nn_size_t i = 0; i < portCount; i++) {
        nn_values_set(arr, i, nn_values_integer(ports[i]));
    }
}

static void nni_modem_send(nn_modem *modem, void *_, nn_component *component, nn_computer *computer) {
    // we pinky promise it won't do a fucky wucky
    nn_address addr = (nn_address)nn_toCString(nn_getArgument(computer, 0));
    if(addr == NULL) {
        nn_setCError(computer, "invalid address");
        return;
    }
    nn_value portVal = nn_getArgument(computer, 1);
    nn_intptr_t port = portVal.tag == NN_VALUE_NIL ? NN_PORT_CLOSEALL : nn_toInt(portVal);
    if(!nni_modem_validSendPort(port) && port != NN_PORT_CLOSEALL) {
        nn_setCError(computer, "invalid port");
        return;
    }
    nn_value vals[modem->table.maxValues];
    nn_size_t valLen = nn_getArgumentCount(computer) - 2;

    if(valLen > modem->table.maxValues) {
        nn_setCError(computer, "too many values");
        return;
    }

    for(nn_size_t i = 0; i < valLen; i++) {
        vals[i] = nn_getArgument(computer, i + 2);
    }

    nn_errorbuf_t err = "";
    nn_lock(&modem->ctx, modem->lock);
    nn_bool_t res = modem->table.send(modem->table.userdata, addr, port, vals, valLen, err);
    nn_unlock(&modem->ctx, modem->lock);
    if(!nn_error_isEmpty(err)) {
        nn_setError(computer, err);
        return;
    }

    nn_return_boolean(computer, res);
}

static void nni_modem_broadcast(nn_modem *modem, void *_, nn_component *component, nn_computer *computer) {
    nn_value portVal = nn_getArgument(computer, 0);
    nn_intptr_t port = portVal.tag == NN_VALUE_NIL ? NN_PORT_CLOSEALL : nn_toInt(portVal);
    if(!nni_modem_validSendPort(port) && port != NN_PORT_CLOSEALL) {
        nn_setCError(computer, "invalid port");
        return;
    }
    nn_value vals[modem->table.maxValues];
    nn_size_t valLen = nn_getArgumentCount(computer) - 1;

    if(valLen > modem->table.maxValues) {
        nn_setCError(computer, "too many values");
        return;
    }

    for(nn_size_t i = 0; i < valLen; i++) {
        vals[i] = nn_getArgument(computer, i + 1);
    }

    nn_errorbuf_t err = "";
    nn_lock(&modem->ctx, modem->lock);
    nn_bool_t res = modem->table.send(modem->table.userdata, NULL, port, vals, valLen, err);
    nn_unlock(&modem->ctx, modem->lock);
    if(!nn_error_isEmpty(err)) {
        nn_setError(computer, err);
        return;
    }

    nn_return_boolean(computer, res);
}

static void nni_modem_getWake(nn_modem *modem, void *_, nn_component *component, nn_computer *computer) {
	char msg[NN_MAX_WAKEUPMSG];
	nn_errorbuf_t err = "";

    nn_lock(&modem->ctx, modem->lock);
	nn_size_t len = modem->table.getWakeMessage(modem->table.userdata, msg, err);
    nn_unlock(&modem->ctx, modem->lock);

	if(!nn_error_isEmpty(err)) {
		nn_setError(computer, err);
		return;
	}

	nn_return_string(computer, msg, len);
}

static void nni_modem_setWake(nn_modem *modem, void *_, nn_component *component, nn_computer *computer) {
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

    nn_lock(&modem->ctx, modem->lock);
	buflen = modem->table.setWakeMessage(modem->table.userdata, buf, buflen, fuzzy, err);
	nn_unlock(&modem->ctx, modem->lock);

	if(!nn_error_isEmpty(err)) {
		nn_setError(computer, err);
		return;
	}

	nn_return_string(computer, buf, buflen);
}

void nn_loadModemTable(nn_universe *universe) {
    nn_componentTable *modemTable = nn_newComponentTable(nn_getAllocator(universe), "modem", NULL, NULL, (nn_componentDestructor *)nn_modem_destroy);
    nn_storeUserdata(universe, "NN:MODEM", modemTable);

    nn_method_t *method;
    nn_defineMethod(modemTable, "isWireless", (nn_componentMethod *)nni_modem_isWireless, "isWireless(): boolean - Returns whether this modem has wireless capabilities");
    nn_defineMethod(modemTable, "maxPacketSize", (nn_componentMethod *)nni_modem_maxPacketSize, "maxPacketSize(): integer - Returns the maximum size of a packet");
    nn_defineMethod(modemTable, "maxOpenPorts", (nn_componentMethod *)nni_modem_maxOpenPorts, "maxOpenPorts(): integer - Returns the maximum number of ports that can be open at the same time");
    nn_defineMethod(modemTable, "maxValues", (nn_componentMethod *)nni_modem_maxValues, "maxValues(): integer - Returns the maximum number of values which can be sent in a packet");
    nn_defineMethod(modemTable, "isOpen", (nn_componentMethod *)nni_modem_isOpen, "isOpen(port: integer): boolean - Returns whether a port is open (allows receiving)");
    nn_defineMethod(modemTable, "open", (nn_componentMethod *)nni_modem_open, "open(port: integer): boolean - Opens a port, returns whether it was successful");
    nn_defineMethod(modemTable, "close", (nn_componentMethod *)nni_modem_close, "close([port: integer]): boolean - Closes a port, or nil for all ports");
    nn_defineMethod(modemTable, "getPorts", (nn_componentMethod *)nni_modem_getPorts, "close([port: integer]): boolean - Closes a port, or nil for all ports");
    nn_defineMethod(modemTable, "send", (nn_componentMethod *)nni_modem_send, "send(address: string, port: integer, ...): boolean - Sends a message to the specified address at the given port. It returns whether the message was sent, not received");
    nn_defineMethod(modemTable, "broadcast", (nn_componentMethod *)nni_modem_broadcast, "broadcast(port: integer, ...): boolean - Broadcasts a message at the given port. It returns whether the message was sent, not received");
    nn_defineMethod(modemTable, "setWakeMessage", (nn_componentMethod *)nni_modem_setWake, "setWakeMessage(msg: string[, fuzzy: boolean]): string - Sets the wake-up message. This will be compared with the first value of modem messages to turn on computers. Set it to nothing to disable this functionality.");
    nn_defineMethod(modemTable, "getWakeMessage", (nn_componentMethod *)nni_modem_getWake, "getWakeMessage(): string - Returns the current wake-up message");

    // wireless stuff
    method = nn_defineMethod(modemTable, "maxStrength", (nn_componentMethod *)nni_modem_maxStrength, "maxStrength(): number - Returns the maximum strength of the device");
    nn_method_setCondition(method, (nn_componentMethodCondition_t *)nni_modem_wirelessOnly);
    
	method = nn_defineMethod(modemTable, "getStrength", (nn_componentMethod *)nni_modem_getStrength, "getStrength(): number - Returns the current strength of the device");
    nn_method_setCondition(method, (nn_componentMethodCondition_t *)nni_modem_wirelessOnly);
	
	method = nn_defineMethod(modemTable, "setStrength", (nn_componentMethod *)nni_modem_setStrength, "setStrength(value: number): number - Returns the current strength of the device");
    nn_method_setCondition(method, (nn_componentMethodCondition_t *)nni_modem_wirelessOnly);
}

nn_component *nn_addModem(nn_computer *computer, nn_address address, int slot, nn_modem *modem) {
    nn_componentTable *modemTable = nn_queryUserdata(nn_getUniverse(computer), "NN:MODEM");
    return nn_newComponent(computer, address, slot, modemTable, modem);
}
