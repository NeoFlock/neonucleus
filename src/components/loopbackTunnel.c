#include "../neonucleus.h"

nn_tunnel *nn_debugLoopbackTunnel(nn_Context *context, nn_debugLoopbackNetworkOpts opts, nn_networkControl control) {
	nn_tunnelTable table = {};

	return nn_newTunnel(context, table, control);
}
