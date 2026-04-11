#ifndef NN_NETLIB
#define NN_NETLIB

#include "neonucleus.h"

// should be sent by computers when they start,
// in order to mount every component accessible
#define NET_COMPDISCOVER "componentDiscover"
#define NET_COMPSWAP "componentSwapped"
#define NET_OCMESSAGE "modemMessage"
// sent when devices with components are disconnected to tell
// computers to recheck accessibility.
// If not, remove the component
#define NET_RECOMPUTE "netRecompute"
#define NET_RADIOMESSAGE "radioMessage"

typedef struct net_ModemMessageData {
	// NULL for broadcast
	const char *target;
	size_t port;
	const nn_EncodedNetworkContents *contents;
	size_t packetSize;
} net_ModemMessageData;

typedef struct net_RadioMessageData {
	double frequency;
	size_t len;
	char *data;
} net_RadioMessageData;

typedef struct net_ComponentSwappedData {
	// if NULL, no component was removed
	nn_Component *removed;
	// if NULL, no component was added
	nn_Component *added;
} net_ComponentSwappedData;

typedef struct net_NetworkLimits {
	// maximum radio lifespan in ticks
	// maxRadioLifespan * radioSpeed is the maximum effective range of radio
	size_t maxRadioLifespan;
	// radio speed in coordinates per tick
	double radioSpeed;
	// max radio packets at one instant
	// If it overflows, the oldest is forcefully removed
	size_t maxRadioPackets;
	// maximum *DIRECT* connections in a device
	size_t maxConnections;
	// maximum number of hops past which the packet gets dropped.
	// Set to 0 for infinite hops, but then it is possible to stackoverflow.
	size_t maxHops;
	// maximum amount of devices in a single channel
	size_t maxChannel;
	// TODO: use an RTree, and thus specify the capacity per node
} net_NetworkLimits;

extern const net_NetworkLimits net_defaultLimits;

typedef struct net_Network net_Network;
typedef struct net_Device net_Device;

typedef struct net_DevicePosition {
	// the dimension this lives in, in the case of Minecraft
	size_t dimensionID;
	// xyz within network
	double x, y, z;
} net_DevicePosition;

typedef struct net_RadioPacket {
	net_DevicePosition initialPosition;
	size_t tickSpawned;
	// the tick at which it is dead
	size_t expirationTick;
	char *data;
	size_t datalen;
} net_RadioPacket;

typedef enum net_MessagePropagation {
	// direct connections
	NET_MESSAGE_DIRECT,
	// entire connected web
	NET_MESSAGE_NETWORK,
	// propagate wirelessly to physically nearby devices
	NET_MESSAGE_WIRELESS,
	// only to a specific slot
	NET_MESSAGE_SLOT,
	// send over named channel
	NET_MESSAGE_CHANNEL,
} net_MessagePropagation;

typedef struct net_Message {
	// the ID; used for de-duplication.
	// Deduplication only happens if the last received packet has the same ID
	size_t id;
	// the issued sender, technically.
	// Relays may edit this field.
	net_Device *sender;
	const char *type;
	void *data;
	size_t hops;
	net_MessagePropagation propagation;
	union {
		// for NET_MESSAGE_WIRELESS
		size_t range;
		// for NET_MESSAGE_SLOT
		size_t slot;
		// for NET_MESSAGE_CHANNEL
		const char *channel;
	};
} net_Message;

// Allowed to mutate the mesasge, but please be careful. Do note that relaying gives them a pointer to a copy of the message struct after you mutate it.
// If you return false, the packet will be dropped.
// Hops is automatically incremented.
typedef bool (net_Filter)(net_Device *device, net_Device *forwarder, net_Message *message);
typedef void (net_TickFunc)(net_Device *device, size_t tickCount);

net_Network *net_createNetwork(nn_Universe *universe, const net_NetworkLimits *limits);
void net_destroyNetwork(net_Network *network);

// locks the network. Only one thread may do ANYTHING to this network or its devices
// at the same time
void net_lockNetwork(net_Device *device);
void net_unlockNetwork(net_Device *device);

net_Device *net_createDevice(net_Network *network, const char *type, size_t slotCount, net_DevicePosition position);
void net_destroyDevice(net_Device *device);
net_Network *net_getNetworkOf(net_Device *device);

// adds to a channel, will create the channel if it does not exist
nn_Exit net_addToChannel(net_Device *device, const char *channel);
// removes from a channel, will delete the channel if it is empty
void net_removeFromChannel(net_Device *device, const char *channel);

// will automatically give the message an ID and hops count, so don't bother
// the sender must be set to the correct sender. This is primarily to allow faking senders if need be.
void net_emit(net_Device *device, const net_Message *message);

void net_setDevicePosition(net_Device *device, net_DevicePosition position);
net_DevicePosition net_getDevicePosition(net_Device *device);
void net_setDeviceState(net_Device *device, void *state);
void *net_getDeviceState(net_Device *device);
// the filter is also meant to be used as a listener
void net_setDeviceFilter(net_Device *device, net_Filter *filter);
void net_setDeviceTick(net_Device *device, net_TickFunc *tick);
void net_tickDevice(net_Device *device);
// automatically emits a COMPSWAPPED message.
void net_setDeviceComponent(net_Device *device, nn_Component *component);
nn_Component *net_getDeviceComponent(net_Device *device);
size_t net_getDeviceSlotCount(net_Device *device);

// returns whether there is a direction one-way connection from device to target
bool net_isDirectlyConnectedTo(net_Device *device, net_Device *target);
// returns -1 if not directly connected, but that can also be the slot its on
int net_getSlotOfDevice(net_Device *device, net_Device *target);
// does a depth-first-scan to see if a network device b remains accessible from a.
// It may be, due to one-way connections, that b is accessible from a but a not from b.
bool net_isNetworkDeviceAccessible(net_Device *a, net_Device *b);
// establishes a 1-WAY connection from device to target on a slot. The target may only be connected once,
// else this will return EBADSTATE.
nn_Exit net_addDeviceSlot(net_Device *device, int slot, net_Device *target);
// removes ALL 1-WAY connections from device to target.
// Also emits a RECOMPUTE for the target
void net_removeDeviceOneWay(net_Device *device, net_Device *target);
// disconnects the devices both-ways
void net_disconnectDevices(net_Device *deviceA, net_Device *deviceB);

typedef void (net_Visitor)(net_Network *network, void *state, net_Device *device);

// iterates every network device
void net_visitNetworkDevices(net_Network *network, void *state, net_Visitor *visitor);
// iterates every network device that wants to be ticked
void net_visitTickingNetworkDevices(net_Network *network, void *state, net_Visitor *visitor);
// visits all devices in a cube with sides of range*2 and origin at origin
void net_visitNetworkDevicesBetween(net_Network *network, net_DevicePosition origin, double range, void *state, net_Visitor *visitor);
// visits all devices connected to a channel
void net_visitNetworkChannel(net_Network *network, const char *devices, void *state, net_Visitor *visitor);

size_t net_getNetworkTickCount(net_Network *network);
// increase the tick count by 1
// this will also delete dead radio packets
void net_incNetworkTickCount(net_Network *network);
// set it to 0
void net_resetNetworkTickCount(net_Network *network);

// Increments the network tick count and iterates over all devices with ticking (using an internal linked list)
void net_tickNetwork(net_Network *network);

void net_spawnRadioPacket(net_Device *sourceDevice, double range, const char *data, size_t len);
size_t net_countRadioPackets(net_Network *network);
void net_getRadioPackets(net_Network *network, net_RadioPacket *packets);

// network devices

// a simple non-rewriting relay, which does not contribute to the hop counter
net_Device *net_createJoint(net_Network *network);

net_Device *net_createModem(net_Network *network, const char *address, const nn_Modem *modem);

net_Device *net_createTunnel(net_Network *network, const char *address, const nn_Tunnel *tunnel, const char *channel);

// TODO: the arguments. Also, relay configs can change
net_Device *net_createRelay(net_Network *network, const char *address);

net_Device *net_createNetSplitter(net_Network *network, const char *address);

// has slots matching the sides defined in neonucleus.h
net_Device *net_createRack(net_Network *network);

#endif
