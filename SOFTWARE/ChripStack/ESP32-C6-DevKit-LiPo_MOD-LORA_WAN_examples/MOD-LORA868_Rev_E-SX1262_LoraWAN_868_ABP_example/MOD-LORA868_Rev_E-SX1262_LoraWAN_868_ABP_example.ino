/*
 * RA-01SH (SX1262) LoRaWAN ABP Test — ESP32
 * ==========================================
 *
 * Production test for the RA-01SH module (SX1262 chip) with
 * LoRaWAN 1.0.3 ABP activation, frequency correction for crystals
 * with non-standard ppm error, and automatic retry on missed ACK.
 *
 * Libraries: RadioLib, Preferences (ESP32 core)
 */

#include <RadioLib.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

// ───────────────────────── Pins ─────────────────────────
#define LORA_NSS    8
#define LORA_BUSY   5
#define LORA_DIO1   4
#define LORA_RST    RADIOLIB_NC   // no hardware reset - see XTAL note below
#define LORA_SCK    0
#define LORA_MISO   14
#define LORA_MOSI   1
#define BUTTON_PIN  9
#define LED_PIN     15

#define TX_POWER    14   // dBm

// ─────────────────── Frequency correction ────────────────────
// SX126x chips with a plain XTAL crystal (not TCXO) can have a
// systematic frequency error of tens of kHz, depending on the
// crystal's load capacitance. The value below must be calibrated
// individually for each board/batch by comparing the requested
// frequency against the actual measured frequency (e.g. via the
// gateway's 'foff' log) until the residual offset drops to a few
// hundred Hz.
//
// Example calibration values for different load capacitances:
//   10 pF  ->  +20250 Hz
//   15 pF  ->  +40000 Hz
//   5.6 pF ->  +4900 Hz
//   4.7 pF ->  -4900 Hz
#define FREQ_CORRECTION_HZ   (0.0f)

// ────────────────── LoRaWAN ABP credentials ─────────────────
uint32_t devAddr     = 0x019D07EA;
uint8_t  nwkSKey[]   = {0x60, 0xCA, 0xCF, 0xAF, 0x55, 0xCD, 0x12, 0x63,
                        0x62, 0xF8, 0x20, 0x41, 0x61, 0xEE, 0x27, 0xA4};
uint8_t  appSKey[]   = {0xDA, 0x15, 0xD8, 0x81, 0x86, 0x92, 0xBD, 0xEE,
                        0xB8, 0xBD, 0x5F, 0xFE, 0xEE, 0xDE, 0x10, 0x32};

// ───────────────── Test retry settings ───────────────
#define MAX_ATTEMPTS       3      // confirmed uplink attempts before reporting failure
#define RETRY_DELAY_MS     2000   // delay between attempts

// ───────────────────────────────────────────────────────────
// Override of setFrequency() so the frequency correction is
// applied to every call made by the LoRaWAN stack — TX, RX1 and
// RX2 — without touching RadioLib itself. setFrequency() is
// declared virtual in SX126x/PhysicalLayer, so the override
// applies automatically everywhere the stack calls it through a
// polymorphic pointer.
class SX1262Corrected : public SX1262 {
  public:
    SX1262Corrected(Module* mod) : SX1262(mod) {}

    int16_t setFrequency(float freq) override {
      return SX1262::setFrequency(freq + (FREQ_CORRECTION_HZ / 1e6f));
    }
};

SX1262Corrected radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
LoRaWANNode      node(&radio, &EU868);

// ─────────────────── NVS session persistence ──────────────
// Frame counters must survive a board reset, otherwise the
// network server will reject subsequent uplinks as a replay
// attack.
Preferences store;
uint8_t nonceBuf[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
uint8_t sessionBuf[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];

uint32_t testCounter = 0;

void saveSession() {
  store.begin("lora", false);
  store.putBytes("nonces",  node.getBufferNonces(),  RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
  store.putBytes("session", node.getBufferSession(), RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
  store.end();
  Serial.print(F("  Session saved, fCntUp: "));
  Serial.println(node.getFCntUp());
}

void clearSession() {
  store.begin("lora", false);
  store.clear();
  store.end();
  Serial.println(F("\n  ⚠️  NVS session cleared! Restart the board.\n"));
}

// ───────────────────────── Helpers ─────────────────────────
void printSeparator() {
  Serial.println(F("========================================"));
}

String errorDescription(int code) {
  switch (code) {
    case RADIOLIB_ERR_CRC_MISMATCH: return "CRC error while receiving";
    case RADIOLIB_ERR_RX_TIMEOUT:   return "Timeout - no ACK received";
    case 0:                         return "No downlink/ACK from gateway";
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
    Serial.println(F("\n  ✅  SUCCESS  ✅"));
    Serial.println(F("  UPLINK:   OK"));
    Serial.println(F("  DOWNLINK: OK (ACK received)"));
  } else {
    Serial.println(F("\n  ❌  FAILURE  ❌"));
    Serial.print(F("  Failed step: ")); Serial.println(failStep);
    Serial.print(F("  Reason: "));      Serial.println(errorDescription(failCode));
  }
  printSeparator();
}

// ─────────────────────── Initialization ─────────────────────
bool initRadio() {
  Serial.print(F("Initializing radio... "));

  // The module uses a plain XTAL crystal, not a TCXO. Without
  // this flag, RadioLib assumes a TCXO by default, which leads to
  // unstable/failed initialization on XTAL hardware.
  radio.XTAL = true;

  int state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.println(F("ERROR!"));
    Serial.print(F("  Code: ")); Serial.println(state);
    return false;
  }

  radio.standby();
  delay(200);
  radio.setRxBoostedGainMode(true);
  radio.setDio2AsRfSwitch(true);

  Serial.print(F("\n  Frequency correction: "));
  Serial.print(FREQ_CORRECTION_HZ);
  Serial.println(F(" Hz\n"));

  // LoRaWAN 1.0.x ABP: single network key (NwkSKey), so the first
  // two parameters (FNwkSIntKey/SNwkSIntKey from 1.1) are nullptr.
  state = node.beginABP(devAddr, nullptr, nullptr, nwkSKey, appSKey);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.println(F("ERROR during ABP init!"));
    Serial.print(F("  Code: ")); Serial.println(state);
    return false;
  }

  // Restore a saved session from NVS, if one exists
  store.begin("lora", true);
  size_t nonceLen = store.getBytes("nonces",  nonceBuf,  sizeof(nonceBuf));
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

  // Margin to compensate for clock drift between the device and
  // the gateway when opening the RX1/RX2 windows.
  node.scanGuard = 300;

  Serial.print(F("  fCntUp: ")); Serial.println(node.getFCntUp());

  node.setADR(false);
  node.setDatarate(0);   // DR0 = SF12/BW125 - maximum range
  node.setTxPower(TX_POWER);
  node.setRx2Dr(0);

  radio.setOutputPower(TX_POWER);

  Serial.println(F("OK — DR0 (SF12/125kHz), RF switch: DIO2 automatic"));
  return true;
}

// ─────────────────────────── Test ──────────────────────────
bool runTest() {
  testCounter++;
  Serial.println();
  printSeparator();
  Serial.print(F("  TEST #")); Serial.println(testCounter);
  printSeparator();

  uint8_t payload[]      = {'T', 'E', 'S', 'T'};
  uint8_t downlinkData[64];
  size_t  downlinkLen    = 0;
  int     state          = 0;
  int     attempt        = 0;

  do {
    attempt++;
    Serial.print(F("Sending confirmed uplink (attempt "));
    Serial.print(attempt); Serial.print(F("/")); Serial.print(MAX_ATTEMPTS);
    Serial.println(F(")..."));

    downlinkLen = 0;
    state = node.sendReceive(payload, sizeof(payload), 1,
                              downlinkData, &downlinkLen, true);
    saveSession();

    Serial.print(F("  sendReceive code: ")); Serial.println(state);

    if (state > 0) break;   // ACK received - done

    if (attempt < MAX_ATTEMPTS) {
      Serial.println(F("  No ACK - retrying..."));
      delay(RETRY_DELAY_MS);
    }
  } while (attempt < MAX_ATTEMPTS);

  Serial.print(F("RSSI: ")); Serial.print(radio.getRSSI()); Serial.println(F(" dBm"));
  Serial.print(F("SNR:  ")); Serial.print(radio.getSNR());  Serial.println(F(" dB"));

  if (state > 0) {
    Serial.print(F("ACK received in RX window ")); Serial.print(state);
    Serial.print(F(" (attempt ")); Serial.print(attempt); Serial.print(F("/")); Serial.print(MAX_ATTEMPTS);
    Serial.println(F(")"));

    digitalWrite(LED_PIN, HIGH);
    delay(1000);
    digitalWrite(LED_PIN, LOW);

    if (downlinkLen > 0) {
      Serial.print(F("Downlink data: "));
      for (size_t i = 0; i < downlinkLen; i++) Serial.print((char)downlinkData[i]);
      Serial.println();
    }
    printResult(true, "", 0);
    return true;
  }

  printResult(false, "ACK/DOWNLINK", state);
  return false;
}

// ─────────────────────── Interactive menu ────────────────────
void waitForEnter(bool firstTime) {
  Serial.println();
  if (firstTime) {
    Serial.println(F("╔════════════════════════════════════════╗"));
    Serial.println(F("║   RA-01SH (SX1262) LoRaWAN ABP Test    ║"));
    Serial.println(F("╚════════════════════════════════════════╝"));
    Serial.println(F("  Mode: ABP (no Join) | NVS persistence: ENABLED"));
  }
  Serial.println(F("  Press ENTER or the button to run a test"));
  Serial.println(F("  'R' + ENTER -> reset the session\n"));

  String cmdBuf = "";
  while (Serial.available()) Serial.read();
  while (digitalRead(BUTTON_PIN) == LOW) delay(10);

  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        cmdBuf.trim();
        if (cmdBuf.equalsIgnoreCase("R")) {
          clearSession();
          cmdBuf = "";
          continue;
        }
        break;
      }
      cmdBuf += c;
    }
    if (digitalRead(BUTTON_PIN) == LOW) {
      delay(50);
      if (digitalRead(BUTTON_PIN) == LOW) {
        while (digitalRead(BUTTON_PIN) == LOW) delay(10);
        break;
      }
    }
  }
}

// ────────────────────────── Arduino ─────────────────────────
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println(F("START"));
  esp_task_wdt_deinit();   // disable watchdog because of long RX waits

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);

  if (!initRadio()) {
    Serial.println(F("CRITICAL ERROR: Radio failed to initialize!"));
    while (true) delay(1000);
  }

  waitForEnter(true);
}

void loop() {
  runTest();
  waitForEnter(false);
}