# Diagrams (Mermaid)

Diagrams in this file are derived from the current code structure (`src/`, `port/`, `include/`). If a diagram shows a conceptual step that is not enforced by code, it is labeled explicitly.

## A) High-level stack (code structure)

```mermaid
flowchart TB
  app[Application code] --> api[Public API<br/>include/umesh.h]
  api --> core[src/umesh.c]

  core --> net[src/net/*]
  core --> sec[src/sec/*]
  net --> mac[src/mac/*]
  mac --> phy[src/phy/*]

  phy --> port_sel{Port selection}
  port_sel -->|ESP32| esp32[port/esp32/phy_esp32.c<br/>esp_wifi_80211_tx + promiscuous RX]
  port_sel -->|POSIX (testing)| posix[port/posix/phy_posix.c<br/>loopback simulation]
```

## B) TX flow (verified by code inspection)

```mermaid
sequenceDiagram
  participant A as Application
  participant U as umesh_send (src/umesh.c)
  participant N as net_route (src/net/net.c)
  participant M as mac_send (src/mac/mac.c)
  participant S as sec_encrypt_frame (src/sec/sec.c)
  participant P as phy_tx (src/phy/phy.c + port/*)

  A->>U: umesh_send(dst, cmd, payload, len, flags)
  U->>N: net_route(frame)
  N->>M: mac_send(frame)
  M->>S: (if security enabled) encrypt payload + append MIC
  M->>P: phy_tx(serialized 802.11 payload)
```

## C) RX flow (partly conceptual; see note)

```mermaid
sequenceDiagram
  participant HW as PHY RX (port/*)
  participant P as phy_rx callback
  participant M as mac_on_frame (src/mac/*)
  participant S as sec_decrypt_frame (src/sec/sec.c)
  participant N as net on_mac_rx (src/net/net.c)
  participant U as on_net_rx (src/umesh.c)
  participant A as Application callbacks

  HW->>P: raw frame received
  P->>M: parsed µMesh frame
  M->>S: verify MIC/replay + decrypt payload (non-ACK frames)
  S->>N: deliver decrypted frame + RSSI
  N->>U: deliver to core RX callback
  U->>A: dispatch per-cmd handler + generic receive handler
```

## D) Node roles (implemented)

```mermaid
stateDiagram-v2
  [*] --> Configured
  Configured --> Coordinator: UMESH_ROLE_COORDINATOR
  Configured --> Router: UMESH_ROLE_ROUTER
  Configured --> EndNode: UMESH_ROLE_END_NODE
  Configured --> Auto: UMESH_ROLE_AUTO

  Auto --> Scanning
  Scanning --> Joining
  Joining --> Connected
  Connected --> Election: (auto mode coordinator timeout / election trigger)
  Election --> Connected
```

## E) Discovery / join flow (implemented; simplified)

```mermaid
sequenceDiagram
  participant N as New node
  participant C as Coordinator

  N->>C: JOIN (broadcast or directed, via discovery)
  C->>N: ASSIGN (node_id assignment)
  N->>C: STATUS/ROUTE_UPDATE (periodic, depending on role)
```

## F) Routing (implemented)

```mermaid
flowchart TB
  send[net_route(frame)] --> mode{routing mode}
  mode -->|Distance-vector| dv[net_route_distance_vector()]
  mode -->|Gradient & dst==COORDINATOR| gr[neighbor_find_uphill()]
  dv --> mac[mac_send(frame)]
  gr --> mac
```

## G) Power modes (implemented as API; measurements NOT VERIFIED)

```mermaid
stateDiagram-v2
  [*] --> ACTIVE
  ACTIVE --> LIGHT: umesh_set_power_mode(UMESH_POWER_LIGHT)
  LIGHT --> ACTIVE: umesh_set_power_mode(UMESH_POWER_ACTIVE)
  ACTIVE --> DEEP: umesh_set_power_mode(UMESH_POWER_DEEP)
  DEEP --> ACTIVE: wakeup / deep-sleep cycle

  note right of ACTIVE
    Current draw is hardware-dependent.
    Numeric mA claims are NOT VERIFIED in this repo/run.
  end note
```
