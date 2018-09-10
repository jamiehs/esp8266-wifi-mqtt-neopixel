# Neopixel (WS2812b) Light Panel/Strip

## Primary Features:

* WiFi & MQTT configuration via a captive portal
* Designed to be discoverable by Home Assistant
* RGB + Brightness picker in Home Assistant
* Node MCU + Arduino flashable

## Todo:

* Rebroadcast the configuration periodically&mdash;this will save you from having to power cycle the light panel when rebooting the host machine that Home Assistant lives on.
* Fix issue where sometimes the initial configuration appears as a malformed JSON snippet; this may be unique to my system/broker.