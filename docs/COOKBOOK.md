# uMesh Cookbook

Practical snippets for the current public API. This file stays documentation-only and does not claim hardware validation.

## Verified scope labels

- Host verified: POSIX builds and tests described in `VERIFICATION.md`
- CI verified: GitHub Actions workflow present in `.github/workflows/ci.yml`
- ESP32 compile verified: only when a compile/integration job is explicitly recorded
- Hardware not verified: RF range, throughput, latency, current draw, coexistence, and regulatory compliance are not claimed here

## Minimal initialization

```c
#include <umesh.h>

static void on_error(umesh_result_t err) {
    (void)err;
}

int main(void) {
    static umesh_ctx_t ctx;
    static const uint8_t master_key[UMESH_KEY_SIZE] = {
        0x00, 0x11, 0x22, 0x33,
        0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xAA, 0xBB,
        0xCC, 0xDD, 0xEE, 0xFF
    };

    umesh_cfg_t cfg = {0};
    cfg.net_id = 0x42;
    cfg.node_id = 0x02;
    cfg.role = UMESH_ROLE_END_NODE;
    cfg.security = UMESH_SEC_NONE;
    cfg.channel = UMESH_DEFAULT_CHANNEL;
    cfg.tx_power = UMESH_TX_POWER_DEFAULT;
    cfg.on_error = on_error;

    if (umesh_init_ctx(&ctx, &cfg) != UMESH_OK) {
        return 1;
    }
    return 0;
}
```

## Caller-owned contexts

Use `umesh_ctx_t` when you want more than one instance in one process.

```c
umesh_ctx_t coord_ctx = {0};
umesh_ctx_t router_ctx = {0};

umesh_cfg_t coord_cfg = {0};
coord_cfg.net_id = 0x42;
coord_cfg.node_id = UMESH_ADDR_COORDINATOR;
coord_cfg.role = UMESH_ROLE_COORDINATOR;
coord_cfg.security = UMESH_SEC_NONE;

umesh_cfg_t router_cfg = coord_cfg;
router_cfg.node_id = 0x03;
router_cfg.role = UMESH_ROLE_ROUTER;

umesh_init_ctx(&coord_ctx, &coord_cfg);
umesh_init_ctx(&router_ctx, &router_cfg);
```

If you still need the compatibility wrappers, bind a context first:

```c
umesh_bind_ctx(&coord_ctx);
umesh_init(&coord_cfg);
```

## Security-enabled init with `security_epoch`

Provide a unique `security_epoch` when security is enabled.

```c
static const uint8_t master_key[UMESH_KEY_SIZE] = {
    0x10, 0x21, 0x32, 0x43,
    0x54, 0x65, 0x76, 0x87,
    0x98, 0xA9, 0xBA, 0xCB,
    0xDC, 0xED, 0xFE, 0x0F
};

umesh_cfg_t cfg = {0};
cfg.net_id = 0x42;
cfg.node_id = 0x02;
cfg.role = UMESH_ROLE_ROUTER;
cfg.security = UMESH_SEC_FULL;
cfg.master_key = master_key;
cfg.security_epoch = 7;

if (umesh_init_ctx(&ctx, &cfg) != UMESH_OK) {
    /* fail closed if nonce uniqueness cannot be established */
}
```

## Security-disabled init

```c
umesh_cfg_t cfg = {0};
cfg.net_id = 0x42;
cfg.node_id = 0x02;
cfg.role = UMESH_ROLE_END_NODE;
cfg.security = UMESH_SEC_NONE;

umesh_init_ctx(&ctx, &cfg);
```

## Send and receive flow

```c
static void on_receive(umesh_pkt_t *pkt) {
    /* Treat pkt as callback-scoped data. Copy it if you need it later. */
    (void)pkt;
}

umesh_on_receive_ctx(&ctx, on_receive);

const uint8_t payload[] = {0x01, 0x02, 0x03};
umesh_send_ctx(&ctx, 0x05, UMESH_CMD_PING, payload, sizeof(payload), 0);

/* In simulation or polling integrations, advance time explicitly. */
umesh_tick_ctx(&ctx, 1000);
```

## ACK-required send behavior

```c
umesh_result_t r = umesh_send_ctx(
    &ctx,
    0x05,
    UMESH_CMD_PING,
    payload,
    sizeof(payload),
    UMESH_FLAG_ACK_REQ);

if (r == UMESH_ERR_NO_ACK) {
    /* retry or report failure */
}
```

## Routing table setup

Routing is selected through configuration; routes are learned by the stack.

```c
umesh_cfg_t cfg = {0};
cfg.net_id = 0x42;
cfg.node_id = 0x03;
cfg.role = UMESH_ROLE_ROUTER;
cfg.routing = UMESH_ROUTING_DISTANCE_VECTOR;

umesh_init_ctx(&ctx, &cfg);
```

For gradient mode:

```c
cfg.routing = UMESH_ROUTING_GRADIENT;
cfg.role = UMESH_ROLE_AUTO;
cfg.election_ms = 1000;
cfg.scan_ms = 2000;
```

## Three-node forwarding concept

Forwarding preserves the original source and final destination.

```c
/* A -> B -> C:
 * final destination stays C,
 * link destination changes at each hop.
 */
umesh_send_ctx(&node_a, 0x04, UMESH_CMD_PING, payload, sizeof(payload), 0);
```

The forwarding node should only rewrite the next-hop link fields; it should not turn transit traffic into a new application packet.

## JOIN and coordinator flow

```c
static void on_node_joined(uint8_t node_id) {
    (void)node_id;
}

umesh_cfg_t coord_cfg = {0};
coord_cfg.net_id = 0x42;
coord_cfg.node_id = UMESH_ADDR_COORDINATOR;
coord_cfg.role = UMESH_ROLE_COORDINATOR;
coord_cfg.on_node_joined = on_node_joined;

umesh_cfg_t auto_cfg = coord_cfg;
auto_cfg.node_id = UMESH_ADDR_UNASSIGNED;
auto_cfg.role = UMESH_ROLE_AUTO;

umesh_init_ctx(&coordinator, &coord_cfg);
umesh_init_ctx(&child, &auto_cfg);
```

The coordinator owns assignment. The requester accepts only the assignment intended for its token and identity.

## Election and AUTO behavior

```c
umesh_cfg_t cfg = {0};
cfg.net_id = 0x42;
cfg.node_id = UMESH_ADDR_UNASSIGNED;
cfg.role = UMESH_ROLE_AUTO;
cfg.scan_ms = 2000;
cfg.election_ms = 1000;

umesh_init_ctx(&ctx, &cfg);
umesh_trigger_election_ctx(&ctx);
```

AUTO mode waits for the configured coordinator timeout; it should not self-elect just because a short scan window expired.

## POSIX simulation notes

- Use multiple `umesh_ctx_t` instances in one host process.
- Drive time with `umesh_tick_ctx(&ctx, now_ms)`.
- Keep the simulation deterministic by using fixed inputs and explicit timing.
- Use this path for host verification and routing/security/MAC behavior, not for RF claims.

## ESP32 notes

- Treat `port/esp32/` and `port/esp32/smoke/` as compile and integration guidance only unless physical validation is separately documented.
- Do not infer range, throughput, latency, current draw, coexistence, or regulatory compliance from a compile-only check.
- Keep the ESP32 port aligned with the public headers and the `UMESH_PORT` build selection.
