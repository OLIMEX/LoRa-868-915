# MOD-LORA868 + The Things Network (TTN) Guide

## Overview

This guide demonstrates a complete LoRaWAN uplink/downlink test using an **Olimex MOD-LORA868** node connected to **The Things Network (TTN)** via any TTN-compatible EU868 gateway.

The same Arduino sketch used with a local ChirpStack setup works with TTN without any code changes — you only need to update the ABP keys to match your TTN device configuration.

## Hardware Required

- ESP32-C6-DevKit-LiPo
- MOD-LORA868 (plugged into the UEXT connector)
- 868 MHz antenna for MOD-LORA868
- USB-C cable
- A TTN-compatible EU868 gateway (e.g. ThinkNode G1) within range, registered and connected to TTN

> **⚠️ Important:** Always attach the 868 MHz antenna to MOD-LORA868 before powering on. Transmitting without an antenna can damage the RF stage.

## TTN Setup

### 1. Create an Account

Register at [https://www.thethingsnetwork.org/](https://www.thethingsnetwork.org/) if you don't already have an account.

### 2. Make Sure You Have a Gateway

You need a TTN-compatible EU868 gateway registered and online in your area. Gateway registration and setup are covered in your gateway's documentation and in the [TTN documentation](https://www.thethingsindustries.com/docs/gateways/).

### 3. Create an Application

1. Open the TTN Console: [https://eu1.cloud.thethings.network/console/](https://eu1.cloud.thethings.network/console/)
2. Go to **Applications** → **+ Create application**
3. Enter an Application ID (e.g. `mod-lora868-abp`) and click **Create application**

### 4. Register the Device

1. Inside your application, go to **End devices** → **+ Register end device**
2. Select **Enter end device specifics manually**
3. Configure as follows:
   - **Frequency plan:** Europe 863-870 MHz (SF9 for RX2 - recommended)
   - **LoRaWAN version:** LoRaWAN Specification 1.1.0
   - **Regional Parameters version:** RP002 Regional Parameters 1.0.4
   - **Activation mode:** ABP (Activation by Personalization)
4. Generate or enter the **DevEUI**, then click **Register end device**
5. After registration, go to the device page. Under **Session information** you will find:
   - **Device address**
   - **FNwkSIntKey** / **SNwkSIntKey** / **NwkSEncKey**
   - **AppSKey**

### 5. Update the Arduino Sketch

Copy the keys from TTN into the sketch, replacing the placeholder values:

```cpp
uint32_t devAddr     = 0x260be6c4;
uint8_t nwkSEncKey[] = {0x60, 0xCA, 0xCF, 0xAF, 0x55, 0xCD, 0x12, 0x63,
                         0x62, 0xF8, 0x20, 0x41, 0x61, 0xEE, 0x27, 0xA3};
uint8_t appSKey[]    = {0xDA, 0x15, 0xD8, 0x81, 0x86, 0x92, 0xBD, 0xEE,
                         0xB8, 0xBD, 0x5F, 0xFE, 0xEE, 0xDE, 0x10, 0x31};
```

In TTN, **FNwkSIntKey**, **SNwkSIntKey**, and **NwkSEncKey** are all the same key for ABP devices — this matches the sketch where `nwkSEncKey` is passed for all three parameters in the `beginABP()` call.

## Node Firmware Setup

### Requirements

- Arduino IDE 2.3.7 (or compatible)
- ESP32 board package installed (with ESP32-C6 support)
- [RadioLib](https://github.com/jgromes/RadioLib) library v7.5.0

### Board Settings

In Arduino IDE, select:

- **Board:** ESP32C6 Dev Module
- **USB CDC On Boot:** Disabled
- All other settings can remain at their defaults.

### Upload

1. Plug MOD-LORA868 into the UEXT connector on the ESP32-C6-DevKit-LiPo.
2. Attach a 868 MHz antenna to MOD-LORA868.
3. Connect the board to your PC via the USB-C port labeled **USB-UART1** — this is also where the Serial Monitor output will appear.
4. Open the sketch in Arduino IDE, compile and upload.
5. Open Serial Monitor at **115200 baud**.

## Running the Test

After upload you should see:

```
START
Initializing radio... OK

╔════════════════════════════════════════╗
║      MOD-LORA868 Test v1.2             ║
╚════════════════════════════════════════╝
  Mode: ABP (no Join)
  NVS persistence: ENABLED
  Press ENTER or button to run test
  Type 'R' + ENTER to reset session
```

Press **ENTER** in the Serial Monitor (or press the button on the board) to send a confirmed uplink. A successful round-trip looks like this:

```
========================================
  TEST #1
========================================
Sending confirmed uplink... OK
ACK received in RX window: 1
RSSI: -55.00 dBm
SNR:  9.25 dB
========================================

  ✅ ✅ ✅  SUCCESS  ✅ ✅ ✅
  Board passed the test!
  UPLINK:   OK
  DOWNLINK: OK (ACK received)

========================================
```

You can repeat the test as many times as needed. The frame counter is saved in NVS and persists across reboots.

**Resetting the session:** if the frame counter gets out of sync with TTN (e.g. after re-registering the device), type `R` + ENTER in the Serial Monitor to clear the saved session, then restart the board.

## Verifying in TTN

Open the TTN Console, navigate to your application and device. Under the **Live data** tab you should see the incoming uplinks with the `TEST` payload and the corresponding downlink ACKs.

## Troubleshooting

| Symptom | Possible Cause |
|---|---|
| `CRITICAL ERROR: Radio failed to initialize!` | MOD-LORA868 not seated properly in UEXT connector; check SPI wiring |
| `Waiting for ACK... TIMEOUT` | No gateway in range, or gateway not connected to TTN; check antenna connections; verify gateway status in TTN Console |
| Frame counter errors in TTN | Session out of sync — type `R` + ENTER to reset, then restart the board. In TTN you may also need to reset the device session from the device page. |
| Uplink appears in TTN but no ACK | Check that confirmed downlinks are not blocked by fair use policy; TTN has a daily downlink limit |
