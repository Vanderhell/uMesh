# uMesh - Network Layer

> Addressing, discovery, routing, multi-hop forwarding

## Overview

The network layer provides:
- node addressing (`NET_ID + NODE_ID`)
- join/assign/leave discovery
- multi-hop packet forwarding
- two routing modes: distance-vector and gradient

## Addressing

`NET_ID` (8-bit) identifies the mesh network.
`NODE_ID` (8-bit) identifies a node inside the network.

Reserved node IDs:
- `0x00` broadcast
- `0x01` coordinator
- `0xFE` unassigned

## Roles

- `UMESH_ROLE_COORDINATOR`
- `UMESH_ROLE_ROUTER`
- `UMESH_ROLE_END_NODE`
- `UMESH_ROLE_AUTO` (auto election)

## Routing Modes

`UMESH_ROUTING_DISTANCE_VECTOR` (default):
- route table per destination
- good for smaller networks and bidirectional traffic
- practical range: up to ~10 nodes

`UMESH_ROUTING_GRADIENT`:
- each node tracks only one value: hop distance to coordinator
- packets to coordinator move uphill through neighbors with lower distance
- good for many-to-one sensor collection
- practical range: 10-100+ nodes

### Gradient beacon propagation

1. Coordinator sends `UMESH_CMD_GRADIENT_BEACON` with payload `{distance=0}`.
2. Node receiving beacon with distance `D` computes candidate `D+1`.
3. If candidate improves local distance, node stores it and rebroadcasts with random jitter `50-200 ms`.
4. If candidate is equal/worse, node updates neighbor info but does not rebroadcast.
5. Propagation depth is capped by `UMESH_MAX_HOP_COUNT`.

### Gradient forwarding

When routing mode is gradient and destination is coordinator:
1. choose neighbor with `neighbor_distance < my_distance`
2. prefer lowest neighbor distance
3. if tie, prefer better RSSI
4. if none exists, return `UMESH_ERR_NOT_ROUTABLE`

## Discovery commands

- `UMESH_CMD_JOIN` (`0x50`)
- `UMESH_CMD_ASSIGN` (`0x51`)
- `UMESH_CMD_LEAVE` (`0x52`)
- `UMESH_CMD_DISCOVER` (`0x53`)
- `UMESH_CMD_ROUTE_UPDATE` (`0x54`)
- `UMESH_CMD_NODE_JOINED` (`0x55`)
- `UMESH_CMD_NODE_LEFT` (`0x56`)
- `UMESH_CMD_ELECTION` (`0x57`)
- `UMESH_CMD_ELECTION_RESULT` (`0x58`)
- `UMESH_CMD_GRADIENT_BEACON` (`0x59`)
- `UMESH_CMD_GRADIENT_UPDATE` (`0x5A`)
- `UMESH_CMD_POWER_BEACON` (`0x5B`)

## Timers

- `UMESH_ROUTE_UPDATE_MS = 30000`
- `UMESH_NODE_TIMEOUT_MS = 90000`
- `UMESH_DISCOVER_TIMEOUT_MS = 2000`
- `UMESH_ELECTION_TIMEOUT_MS = 1000`
- `UMESH_GRADIENT_BEACON_MS = 30000`
- `UMESH_NEIGHBOR_TIMEOUT_MS = 30000`
