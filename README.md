# µMesh

> **Lightweight open mesh protokol nad raw 802.11 pre ESP32.**
> Bez routera. Bez infraštruktúry. Bez proprietárnych závislostí.

[![Language: C99](https://img.shields.io/badge/language-C99-blue.svg)](#)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](#)
[![Platform](https://img.shields.io/badge/platform-ESP32%20%7C%20ESP32--S3%20%7C%20ESP32--C3-lightgrey.svg)](#)
[![Status: v1.0.0](https://img.shields.io/badge/status-v1.0.0-brightgreen.svg)](#)

---

## Čo je µMesh?

µMesh je kompletný **sieťový protokol stack** postavený nad raw IEEE 802.11 frames na ESP32. Využíva WiFi hardware — anténu, rádiový čip — ale **obchádza celý WiFi protokol.** Žiadny router, žiadna asociácia, žiadna infraštruktúra.

Výsledok: peer-to-peer a mesh komunikácia medzi ESP32 zariadeniami kdekoľvek, okamžite, bez akejkoľvek konfigurácie siete.

---

## Prečo nie ESP-NOW?

ESP-NOW je skvelý nápad — ale má zásadné limity:

| | ESP-NOW | **µMesh** |
|---|---|---|
| Otvorený protokol | ✗ Proprietárny | **✓ MIT licencia** |
| Mesh routing | ✗ Len 1 hop | **✓ Multi-hop** |
| Adresovanie | ✗ Len MAC | **✓ NET_ID + NODE_ID** |
| Discovery | ✗ Manuálne | **✓ Automatické** |
| Security | ~ Základné | **✓ AES-128 + HMAC** |
| Command layer | ✗ Nie | **✓ Opcode tabuľka** |
| Portovateľnosť | ✗ Len Espressif | **✓ Abstrahovaná PHY** |
| Závislosti | Espressif SDK | **✓ Zero extra** |

---

## Prečo nie Zigbee / Thread?

```
Zigbee / Thread:
  ✗ Vyžadujú špeciálny rádiový čip (802.15.4)
  ✗ Komplexné — stovky KB kódu
  ✗ Ťažká integrácia
  ✗ Certifikácia

µMesh:
  ✓ Beží na ESP32 ktorý už máš
  ✓ Lightweight — desiatky KB
  ✓ #include a ide
  ✓ MIT licencia
```

---

## Ako to funguje

µMesh posiela vlastné pakety priamo cez WiFi hardware pomocou `esp_wifi_80211_tx()` — bez asociácie, bez AP, bez DHCP. Prijíma cez promiscuous mode. WiFi anténa sa stáva **všeobecným rádiovým vysielačom** pre náš protokol.

```
Štandardný ESP32 WiFi stack:
  Aplikácia → lwIP → WiFi stack → 802.11 → Anténa

µMesh:
  Aplikácia → µMesh stack → raw 802.11 frame → Anténa
                            (obchádza celý WiFi)
```

---

## Rýchly štart

```c
#include "umesh.h"

static const uint8_t KEY[16] = { 0x2B, 0x7E, ... };

umesh_cfg_t cfg = {
    .net_id     = 0x01,
    .node_id    = UMESH_ADDR_UNASSIGNED,  // auto-assign
    .master_key = KEY,
    .role       = UMESH_ROLE_END_NODE,
    .security   = UMESH_SEC_FULL,
    .channel    = 6,                      // WiFi kanál
};

void on_temp(umesh_pkt_t *pkt) {
    float t = *(float *)pkt->payload;
    printf("Teplota: %.1f°C\n", t);
}

void app_main(void) {
    umesh_init(&cfg);
    umesh_start();
    umesh_on_cmd(UMESH_CMD_SENSOR_TEMP, on_temp);

    float temp = 23.5f;
    umesh_send(0x02, UMESH_CMD_SENSOR_TEMP,
               &temp, sizeof(temp), UMESH_FLAG_ACK_REQ);
}
```

---

## Architektúra

```
┌─────────────────────────────────────────────┐
│              APPLICATION LAYER              │
│           umesh_send / receive              │
├─────────────────────────────────────────────┤
│             NETWORK LAYER                   │
│      adresovanie, routing, multi-hop        │
├─────────────────────────────────────────────┤
│               MAC LAYER                     │
│         CSMA/CA, ACK, backoff               │
├──────────────────┬──────────────────────────┤
│  SECURITY LAYER  │      FEC LAYER           │
│  AES-128 CTR     │    Hamming(7,4)          │
│  HMAC-SHA256     │                          │
├──────────────────┴──────────────────────────┤
│             PHYSICAL LAYER                  │
│    Raw IEEE 802.11 frames · ESP32 WiFi      │
│    esp_wifi_80211_tx · Promiscuous mode     │
└─────────────────────────────────────────────┘
```

---

## Výkonnostné parametre

```
Dosah:            ~200m (priamy)
                  ~500m+ (multi-hop, 3 skoky)
Latencia:         ~1–5 ms (1 hop)
Dátová rýchlosť:  desiatky kbps (raw 802.11)
Max uzlov:        16 v jednej sieti
Max skokov:       15
Spotreba TX:      ~80 mA @ 3.3V
Spotreba RX:      ~60 mA @ 3.3V
Spotreba sleep:   závisí od aplikácie
```

---

## Porovnanie s konkurenciou

| | µMesh | ESP-NOW | Zigbee | BLE Mesh | LoRa |
|---|---|---|---|---|---|
| Dosah | 200m+ | 200m | 100m | 30m | 10km |
| Multi-hop | ✓ | ✗ | ✓ | ✓ | ✗ |
| Extra HW | ✗ | ✗ | ✓ | ✗ | ✓ |
| Open source | ✓ | ✗ | ~ | ~ | ✓ |
| C99 embedded | ✓ | ✗ | ✗ | ✗ | ~ |
| Zero deps | ✓ | ✗ | ✗ | ✗ | ✗ |
| Mesh routing | ✓ | ✗ | ✓ | ✓ | ✗ |

---

## Hardware

µMesh beží na **akomkoľvek ESP32** ktorý už máš:

| Čip | Podpora | Poznámka |
|---|---|---|
| ESP32 (klasický) | ✓ | Plná podpora |
| ESP32-S3 | ✓ | Odporúčané |
| ESP32-C3 | ✓ | Najlacnejší (~€3) |
| ESP32-S2 | ✓ | Single-core |

**Žiadny extra hardware.** Len ESP32 + napájanie.

---

## Dokumentácia

| Dokument | Obsah |
|---|---|
| [DESIGN.md](DESIGN.md) | Architektonické rozhodnutia a filozofia |
| [PHYSICAL_LAYER.md](PHYSICAL_LAYER.md) | Raw 802.11, promiscuous mode, ESP32 WiFi API |
| [MAC_LAYER.md](MAC_LAYER.md) | CSMA/CA, ACK, backoff, kolízie |
| [NETWORK_LAYER.md](NETWORK_LAYER.md) | Routing, discovery, multi-hop |
| [SECURITY_LAYER.md](SECURITY_LAYER.md) | AES-128 CTR, HMAC, replay ochrana |
| [API_REFERENCE.md](API_REFERENCE.md) | Kompletná API referencia |
| [IMPLEMENTATION.md](IMPLEMENTATION.md) | C99 kostra, štruktúra projektu |
| [KNOWN_ISSUES.md](KNOWN_ISSUES.md) | Známe problémy a limitácie |

---

## Vzťah k micro-toolkit

µMesh prirodzene integruje knižnice z micro-toolkit — všetky voliteľné:

| Knižnica | Úloha v µMesh |
|---|---|
| `microfsm` | Stavový automat protokolu |
| `microcrypt` | AES-128, HMAC-SHA256 |
| `microlog` | Logovanie paketov, RSSI, chýb |
| `iotspool` | Store-and-forward nad µMesh |
| `microwdt` | Watchdog pre protocol stack |
| `microtest` | Unit testy |

---

## Stav projektu

```
✓ Protokol navrhnutý
✓ Dokumentácia
✓ Implementácia PHY vrstvy (raw 802.11)
✓ Implementácia MAC vrstvy
✓ Implementácia Network vrstvy
✓ Implementácia Security vrstvy
✓ ESP32 port
✓ Príklady
✓ Unit testy (9/9 passing)
```

---

## Licencia

MIT — voľné pre komerčné aj osobné použitie.
