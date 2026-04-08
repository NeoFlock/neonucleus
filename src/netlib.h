#ifndef NN_NETLIB
#define NN_NETLIB

#include "neonucleus.h"

// should be sent by computers when they start,
// in order to mount every component accessible
#define NET_COMPDISCOVER "componentDiscover"
#define NET_COMPADDED "componentAdded"
#define NET_COMPREMOVED "componentRemoved"
#define NET_OCMESSAGE "modemMessage"
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

typedef struct net_NetworkLimits {
	// maximum radio lifespan in ticks
	size_t maxRadioLifespan;
	// radio speed in coordinates per second
	double radioSpeed;
	// maximum connections in a device
	size_t maxConnections;
	// maximum amount of devices in a octo-tree node.
	// For efficient wireless comms, the devices are stored in an octo-tree, the 3D version of a quadtree.
	size_t maxTreeNodeSize;
	// maximum number of hops past which the packet gets dropped.
	// Set to 0 for infinite hops, but then it is possible to stackoverflow.
	size_t maxHops;
	// maximum amount of devices in a single channel
	size_t maxChannel;
} net_NetworkLimits;

typedef struct net_Network net_Network;
typedef struct net_Device net_Device;

typedef struct net_DevicePosition {
	// the dimension this lives in, in the case of Minecraft
	size_t dimensionID;
	// xyz within network
	double x, y, z;
} net_DevicePosition;

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
void net_lockNetwork(net_Device *device);
void net_unlockNetwork(net_Device *device);

net_Device *net_createDevice(net_Network *network, const char *type, size_t slotCount, net_DevicePosition position);
void net_destroyDevice(net_Device *device);
void net_lockDevice(net_Device *device);
void net_unlockDevice(net_Device *device);

nn_Exit net_addToChannel(net_Device *device, const char *channel);
void net_removeFromChannel(net_Device *device, const char *channel);

// will automatically give the message an ID and hops count, so don't bother
// this will lock the network for its entire duration
void net_emit(net_Device *device, const net_Message *message);

void net_setDevicePosition(net_Device *device, net_DevicePosition position);
net_DevicePosition net_getDevicePosition(net_Device *device);
void net_setDeviceState(net_Device *device, void *state);
void *net_getDeviceState(net_Device *device);
// the filter is also meant to be used as a listener
void net_setDeviceFilter(net_Device *device, net_Filter *filter);
void net_setDeviceTick(net_Device *device, net_TickFunc *tick);
void net_tickDevice(net_Device *device);
void net_setDeviceComponent(net_Device *device, nn_Component *component);
nn_Component *net_getDeviceComponent(net_Device *device);
size_t net_getDeviceSlotCount(net_Device *device);

net_Device *net_getDeviceSlot(net_Device *device, size_t slot);
// returns -1 if not directly connected
int net_getSlotOfDevice(net_Device *device, net_Device *target);
// establishes a 1-WAY connection from device to target on a slot. Each slot may only have one, so previous ones are disconnected
void net_setDeviceSlot(net_Device *device, size_t slot, net_Device *target);
// adds a 1-WAY connection form device to target, but on not slot.
nn_Exit net_mountDevice(net_Device *device, net_Device *target);
// removes ALL 1-WAY connections from device to target
void net_removeDeviceOneWay(net_Device *device, net_Device *target);
// disconnects the devices both-ways
void net_disconnectDevices(net_Device *deviceA, net_Device *deviceB);

typedef void (net_Visitor)(net_Network *network, void *state, net_Device *device);

// gets the network device count
size_t net_countNetworkDevices(net_Network *network);
// get the device, buffer must be big enough for all of them.
// Make sure to call net_countNetworkDevices.
// If running stuff on multiple threads, MAKE SURE THE NETWORK IS LOCKED. THIS IS VERY IMPORTANT
void net_getNetworkDevices(net_Network *network, net_Device **devices);

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
