#include <RadioLib.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

// ── SPI & Radio Pins (ESP32-C6 / MOD-LORA915) ────────
#define LORA_NSS    8
#define LORA_DIO0   5
#define LORA_DIO1   4
#define LORA_RST    -1
#define LORA_SCK    0
#define LORA_MISO   14
#define LORA_MOSI   1

#define BUTTON_PIN  9
#define LED_PIN     15
#define TX_POWER    14      // dBm

// ── ABP Credentials (must match ChirpStack device profile) ──
uint32_t devAddr     = 0x01020304;
uint8_t nwkSEncKey[] = {0xFE, 0xEF, 0x06, 0x0B, 0xDF, 0xA4, 0xBA, 0x3C,
                         0x84, 0x82, 0xA2, 0xBA, 0x70, 0xAA, 0x2F, 0x37};
uint8_t appSKey[]    = {0xFE, 0xEF, 0x06, 0x0B, 0xDF, 0xA4, 0xBA, 0x3C,
                         0x84, 0x82, 0xA2, 0xBA, 0x70, 0xAA, 0x2F, 0x37};

// Sub-band 2 (channels 8-15, 903.9–905.3 MHz) - must match gateway config
SX1276 radio = new Module(LORA_NSS, LORA_DIO0, LORA_RST, LORA_DIO1);
LoRaWANNode node(&radio, &US915, 2);

uint32_t testCounter = 0;

// ── NVS Session Persistence ───────────────────────────
// ABP frame counters (fCntUp) must survive reboots,
// otherwise the network server will reject uplinks as replay attacks.
Preferences store;
uint8_t nonceBuf[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
uint8_t sessionBuf[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];

void saveSession() {
  uint8_t* noncePtr = node.getBufferNonces();
  uint8_t* sessPtr  = node.getBufferSession();
  store.begin("lora", false);
  store.putBytes("nonces", noncePtr, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
  store.putBytes("session", sessPtr, RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
  store.end();
  Serial.print(F("  Session saved, fCntUp: "));
  Serial.println(node.getFCntUp());
}

void clearSession() {
  store.begin("lora", false);
  store.clear();
  store.end();
  Serial.println(F(""));
  Serial.println(F("  ⚠️  NVS session cleared!"));
  Serial.println(F("  Restart the board for changes to take effect."));
  Serial.println(F(""));
}

// ───────────────────────────────────────────────────────

void printSeparator() {
  Serial.println(F("========================================"));
}

String errorDescription(int code) {
  switch (code) {
    case -1116: return "Timeout - packet was not sent";
    case -1118: return "CRC error - noisy RF link";
    case -1105: return "Transmission error";
    case -1106: return "Reception error";
    case -1101: return "Invalid configuration";
    case -13:   return "Invalid output power";
    case RADIOLIB_ERR_RX_TIMEOUT: return "Timeout - ACK not received";
    case 0:     return "No downlink/ACK from gateway";
    default: {
      String s = "Unknown error (code: ";
      s += code;
      s += ")";
      return s;
    }
  }
}

void printResult(bool passed, const char* failStep, int failCode) {
  printSeparator();
  if (passed) {
    Serial.println(F(""));
    Serial.println(F("  ✅ ✅ ✅  SUCCESS  ✅ ✅ ✅"));
    Serial.println(F("  Board passed the test!"));
    Serial.println(F("  UPLINK:   OK"));
    Serial.println(F("  DOWNLINK: OK (ACK received)"));
  } else {
    Serial.println(F(""));
    Serial.println(F("  ❌ ❌ ❌  FAILURE  ❌ ❌ ❌"));
    Serial.print(F("  Failed step: ")); Serial.println(failStep);
    Serial.print(F("  Reason: ")); Serial.println(errorDescription(failCode));
    Serial.println(F(""));
    Serial.println(F("  Possible issues:"));
    if (strcmp(failStep, "INIT") == 0) {
      Serial.println(F("  - Check MOD-LORA915 hardware connection"));
      Serial.println(F("  - Check SPI connections"));
    } else if (strcmp(failStep, "UPLINK") == 0) {
      Serial.println(F("  - Check MOD-LORA915 antenna"));
      Serial.println(F("  - Check RF hardware"));
    } else if (strcmp(failStep, "ACK/DOWNLINK") == 0) {
      Serial.println(F("  - Board transmits but does not receive"));
      Serial.println(F("  - Check MOD-LORA915 RX circuit"));
      Serial.println(F("  - Check ChirpStack configuration"));
    }
  }
  printSeparator();
}

// ── Radio & LoRaWAN Initialization ────────────────────
bool initRadio() {
  Serial.print(F("Initializing radio... "));

  int state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.println(F("ERROR!"));
    Serial.print(F("  Code: ")); Serial.println(state);
    return false;
  }

  // LoRaWAN 1.0.3 / Regional Parameters ClassA
  state = node.beginABP(devAddr, nullptr, nullptr, nwkSEncKey, appSKey);
  // For LoRaWAN 1.1.0 / RP002-1.0.5 use instead:
  // state = node.beginABP(devAddr, nwkSEncKey, nwkSEncKey, nwkSEncKey, appSKey);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.println(F("ERROR during ABP init!"));
    Serial.print(F("  Code: ")); Serial.println(state);
    return false;
  }

  // Restore saved session from NVS (preserves frame counters across reboots)
  store.begin("lora", true);
  size_t nonceLen = store.getBytes("nonces", nonceBuf, sizeof(nonceBuf));
  size_t sessLen  = store.getBytes("session", sessionBuf, sizeof(sessionBuf));
  store.end();

  if (nonceLen == sizeof(nonceBuf) && sessLen == sizeof(sessionBuf)) {
    node.setBufferNonces(nonceBuf);
    node.setBufferSession(sessionBuf);
    Serial.println(F("  Session restored from NVS"));
  } else {
    Serial.println(F("  No saved session - starting new"));
  }

  state = node.activateABP();
  if (state != RADIOLIB_ERR_NONE &&
      state != RADIOLIB_LORAWAN_SESSION_RESTORED &&
      state != RADIOLIB_LORAWAN_NEW_SESSION) {
    Serial.println(F("ERROR during ABP activation!"));
    Serial.print(F("  Code: ")); Serial.println(state);
    return false;
  }

  Serial.print(F("  fCntUp: ")); Serial.println(node.getFCntUp());

  // US915 sub-band 2 settings
  node.setADR(false);
  node.setDatarate(0);         // SF10/125kHz
  node.setRx2Dr(8);            // RX2 = SF12/500kHz (US915 default)
  node.setTxPower(14);
  radio.setOutputPower(TX_POWER, true);

  Serial.println(F("OK"));
  return true;
}

// ── Send confirmed uplink and wait for ACK ────────────
bool runTest() {
  testCounter++;
  Serial.println(F(""));
  printSeparator();
  Serial.print(F("  TEST #")); Serial.println(testCounter);
  printSeparator();

  Serial.print(F("Sending confirmed uplink... "));

  uint8_t payload[] = {'T', 'E', 'S', 'T'};
  uint8_t downlinkData[64];
  size_t downlinkLen = 0;

  radio.setOutputPower(TX_POWER, true);

  // Port 1, confirmed = true
  int state = node.sendReceive(payload, sizeof(payload), 1,
                               downlinkData, &downlinkLen, true);

  // Always save session after TX - fCntUp has already been incremented
  saveSession();

  if (state > 0) {
    // Positive value = RX window number where ACK was received
    Serial.println(F("OK"));
    Serial.print(F("ACK received in RX window: ")); Serial.println(state);
    digitalWrite(LED_PIN, HIGH);
    delay(1000);
    digitalWrite(LED_PIN, LOW);
    Serial.print(F("RSSI: ")); Serial.print(radio.getRSSI()); Serial.println(F(" dBm"));
    Serial.print(F("SNR:  ")); Serial.print(radio.getSNR());  Serial.println(F(" dB"));
    if (downlinkLen > 0) {
      Serial.print(F("Downlink data: "));
      for (size_t i = 0; i < downlinkLen; i++) Serial.print((char)downlinkData[i]);
      Serial.println();
    }
    printResult(true, "", 0);
    return true;
  } else if (state == 0) {
    Serial.println(F("OK"));
    Serial.println(F("Waiting for ACK... NO RESPONSE"));
    printResult(false, "ACK/DOWNLINK", 0);
    return false;
  } else if (state == RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.println(F("OK"));
    Serial.println(F("Waiting for ACK... TIMEOUT"));
    printResult(false, "ACK/DOWNLINK", state);
    return false;
  } else {
    Serial.println(F("ERROR"));
    printResult(false, "UPLINK", state);
    return false;
  }
}

// ── Interactive Menu ──────────────────────────────────
void waitForEnter(bool firstTime) {
  Serial.println(F(""));
  if (firstTime) {
    Serial.println(F("╔════════════════════════════════════════╗"));
    Serial.println(F("║      MOD-LORA915 Test v1.1             ║"));
    Serial.println(F("╚════════════════════════════════════════╝"));
    Serial.println(F("  Mode: ABP (no Join)"));
    Serial.println(F("  NVS persistence: ENABLED"));
  }
  Serial.println(F("  Press ENTER or button to run test"));
  Serial.println(F("  Type 'R' + ENTER to reset session"));
  Serial.println(F(""));

  String cmdBuf = "";

  while (Serial.available()) Serial.read();
  while (digitalRead(BUTTON_PIN) == LOW) delay(10);

  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        cmdBuf.trim();
        if (cmdBuf.equalsIgnoreCase("R") || cmdBuf.equalsIgnoreCase("RESET")) {
          clearSession();
          Serial.println(F("  Press ENTER or button to run test"));
          Serial.println(F("  Type 'R' + ENTER to reset session"));
          Serial.println(F(""));
          cmdBuf = "";
          continue;
        }
        break;
      } else {
        cmdBuf += c;
      }
    }
    if (digitalRead(BUTTON_PIN) == LOW) {
      delay(50);
      if (digitalRead(BUTTON_PIN) == LOW) {
        Serial.println(F("  (Button pressed)"));
        while (digitalRead(BUTTON_PIN) == LOW) delay(10);
        break;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("START");
  esp_task_wdt_deinit();        // Disable watchdog for long RX waits
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  if (!initRadio()) {
    Serial.println(F("CRITICAL ERROR: Radio failed to initialize!"));
    while(true) delay(1000);
  }
  waitForEnter(true);
}

void loop() {
  runTest();
  waitForEnter(false);
}