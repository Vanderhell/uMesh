/*
 * umesh_src.c — compile all µMesh sources into this sketch.
 *
 * Arduino only compiles files that are in the sketch directory.
 * This file includes every µMesh .c translation unit via relative paths
 * so the linker can resolve all symbols without a library install step.
 *
 * The UMESH_PORT_ESP32 guard selects the ESP32 PHY port.
 */

#include "../../../../src/common/crc.c"
#include "../../../../src/common/ring.c"
#include "../../../../src/phy/phy.c"
#include "../../../../src/mac/frame.c"
#include "../../../../src/mac/cca.c"
#include "../../../../src/mac/mac.c"
#include "../../../../src/sec/keys.c"
#include "../../../../src/sec/sec.c"
#include "../../../../src/net/routing.c"
#include "../../../../src/net/discovery.c"
#include "../../../../src/net/net.c"
#include "../../../../src/umesh.c"
#include "../../../../port/esp32/phy_esp32.c"
