#include "netlib.h"
// libm needed, rip
#include <math.h>

const net_NetworkLimits net_defaultLimits = (net_NetworkLimits) {
	.maxRadioLifespan = 6000,
	.radioSpeed = 10000,
	.maxRadioPackets = 4096,
	.maxConnections = 128,
	.maxHops = 32,
	.maxChannel = 128,
};

typedef struct net_Channel {
	char *address;
	net_Device **devices;
	size_t len;
	struct net_Channel *prevChannel;
	struct net_Channel *nextChannel;
} net_Channel;

typedef struct net_Network {
	nn_Universe *universe;
	nn_Context *ctx;
	nn_Lock *lock;
	net_NetworkLimits limits;
	net_Device *allDevices;
	net_Device *allTicked;
	net_Channel *allChannels;
	size_t nextMessageID;
	size_t nextDiscoverID;
	net_RadioPacket *radioPackets;
	size_t radioPacketCount;
} net_Network;

typedef struct net_DeviceConn {
	net_Device *target;
	int slot;
} net_DeviceConn;

typedef struct net_Device {
	net_Network *network;
	net_Channel *channel;
	net_Device *prevDevice;
	net_Device *nextDevice;
	net_Device *prevTicked;
	net_Device *nextTicked;
	void *state;
	net_Filter *filter;
	net_TickFunc *tickFunc;
	net_DeviceConn *connections;
	size_t connectionLen;
	size_t lastMessageID;
	size_t lastDiscoverID;
} net_Device;
