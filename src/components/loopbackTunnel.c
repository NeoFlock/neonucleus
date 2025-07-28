#include "../neonucleus.h"

typedef struct nn_loopTunnel_t {
	nn_Context ctx;
	nn_debugLoopbackNetworkOpts opts;
    char wakeup[NN_MAX_WAKEUPMSG];
    nn_size_t wakeupLen;
} nn_loopTunnel_t;

static void nni_debugTunnel_deinit(nn_loopTunnel_t *t) {
	nn_Alloc a = t->ctx.allocator;
	nn_deallocStr(&a, t->opts.address);
	nn_dealloc(&a, t, sizeof(nn_loopTunnel_t));
}

static nn_size_t nni_debugTunnel_getChannel(nn_loopTunnel_t *t, char *buf, nn_errorbuf_t err) {
	nn_strcpy(buf, "loopback");
	return 8;
}

static nn_size_t nni_debugTunnel_getWakeMessage(nn_loopTunnel_t *t, char *buf, nn_errorbuf_t err) {
	nn_memcpy(buf, t->wakeup, t->wakeupLen);
	return t->wakeupLen;
}

static nn_size_t nni_debugTunnel_setWakeMessage(nn_loopTunnel_t *t, const char *buf, nn_size_t buflen, nn_bool_t fuzzy, nn_errorbuf_t err) {
	if(buflen > NN_MAX_CHANNEL_SIZE) buflen = NN_MAX_CHANNEL_SIZE;
	nn_memcpy(t->wakeup, buf, buflen);
	t->wakeupLen = buflen;
	return buflen;
}
    
static void nni_debugTunnel_send(nn_loopTunnel_t *t, nn_value *values, nn_size_t valueCount, nn_errorbuf_t err) {
	nn_pushNetworkMessage(t->opts.computer, t->opts.address, "loopback", NN_TUNNEL_PORT, 0, values, valueCount);
}

nn_tunnel *nn_debugLoopbackTunnel(nn_Context *context, nn_debugLoopbackNetworkOpts opts, nn_networkControl control) {
	nn_Alloc *alloc = &context->allocator;

	nn_loopTunnel_t *t = nn_alloc(alloc, sizeof(nn_loopTunnel_t));
	t->ctx = *context;
	t->opts = opts;
	t->opts.address = nn_strdup(alloc, t->opts.address);
	t->wakeupLen = 0;

	nn_tunnelTable table = {
		.userdata = t,
		.deinit = (void *)nni_debugTunnel_deinit,

		.maxValues = opts.maxValues,
		.maxPacketSize = opts.maxPacketSize,
		.getChannel = (void *)nni_debugTunnel_getChannel,
		.getWakeMessage = (void *)nni_debugTunnel_getWakeMessage,
		.setWakeMessage = (void *)nni_debugTunnel_setWakeMessage,
		.send = (void *)nni_debugTunnel_send,
	};

	return nn_newTunnel(context, table, control);
}
