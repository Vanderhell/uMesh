# FAQ

## Does it work with ESP8266?

No. ESP8266 lacks `esp_wifi_80211_tx()`.

## Can I use it without a coordinator?

Yes. Use `UMESH_ROLE_AUTO`. Nodes elect a coordinator automatically.

## How many nodes can the network support?

Up to 16 nodes per network (`NET_ID`). Multiple networks can coexist on the same channel.

## Does it interfere with normal WiFi?

It shares the 2.4GHz band. Use channel 1, 6, or 11. CSMA/CA handles collisions.

## What is the maximum range?

About 200m line-of-sight with standard ESP32. Range extends with multi-hop routing.
