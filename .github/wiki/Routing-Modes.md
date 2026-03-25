# Routing Modes

µMesh supports two routing modes configured by `umesh_cfg_t.routing`.

## 1) Distance Vector (`UMESH_ROUTING_DISTANCE_VECTOR`)

How it works:

- Nodes exchange route updates periodically
- Each node keeps best next-hop route to known destinations
- Metric combines hop cost and link quality

Best for:

- General mesh traffic
- Mixed source/destination patterns
- Stable small-to-medium topologies

Pros:

- Mature default mode
- Handles arbitrary destination routing

Tradeoffs:

- Convergence depends on route update intervals

## 2) Gradient (`UMESH_ROUTING_GRADIENT`)

How it works:

- Coordinator emits gradient beacons
- Each node computes distance-to-coordinator
- Uplink traffic forwards to neighbor with lower distance

Best for:

- Sensor networks (many nodes -> one coordinator)
- Telemetry collection and low-complexity uplink

Pros:

- Very simple forwarding logic for upstream traffic
- Good fit for periodic sensor payloads

Tradeoffs:

- Optimized for coordinator-destined traffic
- Needs beacon convergence before routing is ready

## Which one should I choose?

- Choose **Distance Vector** for general-purpose mesh communication.
- Choose **Gradient** for sensor aggregation to one coordinator.

## Related settings

- `gradient_beacon_ms`
- `gradient_jitter_max_ms`
- `umesh_gradient_distance()`
- `umesh_gradient_refresh()`
