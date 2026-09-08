/*
 * RA-01SH (SX1262) LoRaWAN ABP Test — ESP32 — US915
 * ==================================================
 *
 * Production test for the RA-01SH module (SX1262 chip) with
 * LoRaWAN 1.0.3 ABP activation on the US915 band, sub-band 2
 * (channels 8-15 + 500kHz channel 65 — matches an 8-channel
 * gateway listening on 903.9-905.3 MHz).
 *
 * Two board-specific fixes are included:
 *
 * 1. Over-Current Protection (OCP) correction. RadioLib's chip
 *    detection reports this module's VERSION_STRING as "SX1261"
 *    (a cosmetic mismatch - the physical chip is SX1262), which
 *    leads to the OCP register defaulting to the lower SX1261
 *    limit (0x18, ~60 mA - enough for only about +15 dBm) instead
 *    of the correct SX1262 default (0x38, ~140 mA, required for
 *    the full +22 dBm HP PA output). Without this correction, the
 *    requested TX power is accepted and the PA is configured for
 *    it, but the actual RF output is silently current-limited to
 *    roughly +12-15 dBm regardless of the requested power
 *    (confirmed with a powermeter on this board).
 *
 * 2. Self-healing data rate bootstrap. On a fresh session, the
 *    network server sends an initial batch of MAC commands
 *    (channel mask setup, ADR parameters, RX timing, etc.). The
 *    device's replies to that batch, combined with any
 *    application payload, can exceed the US915 DR0 dwell-time
 *    payload limit (11 bytes). This sketch temporarily raises the
 *    data rate for the first few uplinks after a fresh session,
 *    then automatically drops back to DR0 (maximum range) once
 *    the MAC command queue has settled — and remembers that state
 *    across reboots, so a normal power-cycle does not repeat the
 *    bootstrap.
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

#define TX_POWER    22   // dBm - SX1262 hardware maximum (HP PA).
                          // Requires the OCP correction below to
                          // actually reach full output on this
                          // board - see initRadio().

// ─────────────────── US915 sub-band ────────────────────
// US915 has 8 sub-bands (64 channels total). Most 8-channel
// gateways listen on only one sub-band. RadioLib numbers
// sub-bands starting from 1; sub-band 2 activates channels 8-15
// (903.9-905.3 MHz) plus the matching 500kHz channel 65
// (904.6 MHz). Verify this value against your gateway's channel
// plan before deploying to a different gateway.
#define US915_SUBBAND   2

// ───────────── Self-healing data rate bootstrap ─────────────
// FINAL_DR is the normal operating data rate (DR0 = SF10/BW125,
// the maximum spreading factor allowed on US915's 125kHz channels
// and therefore the maximum-range setting for this band).
// BOOTSTRAP_DR is used temporarily after a fresh session, where
// its higher dwell-time payload limit safely absorbs the
// network's initial MAC command batch and the device's replies
// to it. BOOTSTRAP_UPLINKS is how many uplinks are sent at
// BOOTSTRAP_DR before switching back.
#define FINAL_DR            0
#define BOOTSTRAP_DR         1
#define BOOTSTRAP_UPLINKS   3

// ─────────────────── Frequency correction ────────────────────
// SX126x chips with a plain XTAL crystal (not TCXO) can have a
// systematic frequency error, depending on the crystal's load
// capacitance. This error is proportional (ppm-based) to the
// carrier frequency, so a value calibrated at one band does NOT
// carry over directly to another — it must be re-measured for
// each operating frequency.
//
// Calibrate by comparing the requested frequency against the
// actual measured frequency (e.g. via the gateway's 'foff' log)
// until the residual offset drops to a few hundred Hz.
#define FREQ_CORRECTION_HZ   (0.0f)

// ────────────────── LoRaWAN ABP credentials ─────────────────
uint32_t devAddr     = 0x01020304;
uint8_t  nwkSKey[]   = {0xFE, 0xEF, 0x06, 0x0B, 0xDF, 0xA4, 0xBA, 0x3C,
                        0x84, 0x82, 0xA2, 0xBA, 0x70, 0xAA, 0x2F, 0x37};
uint8_t  appSKey[]   = {0xFE, 0xEF, 0x06, 0x0B, 0xDF, 0xA4, 0xBA, 0x3C,
                        0x84, 0x82, 0xA2, 0xBA, 0x70, 0xAA, 0x2F, 0x37};

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
LoRaWANNode      node(&radio, &US915, US915_SUBBAND);

// ─────────────────── NVS session persistence ──────────────
// Frame counters must survive a board reset, otherwise the
// network server will reject subsequent uplinks as a replay
// attack. The "settled" flag remembers whether the initial MAC
// command bootstrap has already completed, so a normal power
// cycle does not repeat it.
Preferences store;
uint8_t nonceBuf[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
uint8_t sessionBuf[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];

uint32_t testCounter    = 0;
bool     macSettled     = false;
uint8_t  bootstrapCount = 0;

void saveSession() {
  store.begin("lora", false);
  store.putBytes("nonces",  node.getBufferNonces(),  RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
  store.putBytes("session", node.getBufferSession(), RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
  store.end();
  Serial.print(F("  Session saved, fCntUp: "));
  Serial.println(node.getFCntUp());
}

void saveMacSettled(bool settled) {
  store.begin("lora", false);
  store.putBool("settled", settled);
  store.end();
}

void clearSession() {
  store.begin("lora", false);
  store.clear();
  store.end();
  Serial.println(F("\n  WARNING: NVS session cleared! Restart the board.\n"));
}

// ───────────────────────── Helpers ─────────────────────────
void printSeparator() {
  Serial.println(F("========================================"));
}

String errorDescription(int code) {
  switch (code) {
    case RADIOLIB_ERR_PACKET_TOO_LONG: return "Payload too long for current data rate's dwell-time limit";
    case RADIOLIB_ERR_CRC_MISMATCH:    return "CRC error while receiving";
    case RADIOLIB_ERR_RX_TIMEOUT:      return "Timeout - no ACK received";
    case 0:                            return "No downlink/ACK from gateway";
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
    Serial.println(F("\n  \xE2\x9C\x85  SUCCESS  \xE2\x9C\x85"));
    Serial.println(F("  UPLINK:   OK"));
    Serial.println(F("  DOWNLINK: OK (ACK received)"));
  } else {
    Serial.println(F("\n  \xE2\x9D\x8C  FAILURE  \xE2\x9D\x8C"));
    Serial.print(F("  Failed step: ")); Serial.println(failStep);
    Serial.print(F("  Reason: "));      Serial.println(errorDescription(failCode));
  }
  printSeparator();
}

// ─────────────── Low-level SPI register access ──────────────
// Used only for the one-time OCP correction below.
void waitBusy() {
  while (digitalRead(LORA_BUSY));
}

uint8_t readReg(uint16_t addr) {
  waitBusy();
  digitalWrite(LORA_NSS, LOW);
  SPI.transfer(0x1D);
  SPI.transfer(addr >> 8);
  SPI.transfer(addr & 0xFF);
  SPI.transfer(0x00);
  uint8_t val = SPI.transfer(0x00);
  digitalWrite(LORA_NSS, HIGH);
  return val;
}

void writeReg(uint16_t addr, uint8_t val) {
  waitBusy();
  digitalWrite(LORA_NSS, LOW);
  SPI.transfer(0x0D);
  SPI.transfer(addr >> 8);
  SPI.transfer(addr & 0xFF);
  SPI.transfer(val);
  digitalWrite(LORA_NSS, HIGH);
}

#define REG_OCP   0x08E7

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

  // Board-specific fix: correct the Over-Current Protection
  // register to the true SX1262 default (see file header comment
  // for the full explanation).
  writeReg(REG_OCP, 0x38);

  radio.standby();
  delay(200);
  radio.setRxBoostedGainMode(true);
  radio.setDio2AsRfSwitch(true);

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
  bool   settledFlag = store.getBool("settled", false);
  store.end();

  bool sessionRestored = (nonceLen == sizeof(nonceBuf) && sessLen == sizeof(sessionBuf));

  if (sessionRestored) {
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

  // A fresh session always needs the MAC bootstrap, regardless of
  // what was previously persisted.
  macSettled     = sessionRestored && settledFlag;
  bootstrapCount = 0;

  // Margin to compensate for clock drift between the device and
  // the gateway when opening the RX1/RX2 windows.
  node.scanGuard = 300;

  Serial.print(F("  fCntUp: ")); Serial.println(node.getFCntUp());

  node.setADR(false);
  node.setTxPower(TX_POWER);
  // Note: RX2 frequency/DR for US915 is fixed by the regional
  // spec (923.3 MHz, DR8) and handled automatically by RadioLib's
  // band table — no manual setRx2Dr() call needed here, unlike
  // an EU868 dynamic-band version of this sketch.

  if (macSettled) {
    node.setDatarate(FINAL_DR);
    Serial.println(F("  MAC queue already settled - starting at final data rate"));
  } else {
    node.setDatarate(BOOTSTRAP_DR);
    Serial.println(F("  Fresh session - starting at bootstrap data rate"));
  }

  radio.setOutputPower(TX_POWER);

  Serial.print(F("OK - sub-band ")); Serial.print(US915_SUBBAND);
  Serial.println(F(", RF switch: DIO2 automatic"));
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

  // Self-healing data rate bootstrap: after a fresh session, stay
  // on BOOTSTRAP_DR for a fixed number of uplinks (regardless of
  // per-attempt success) to let the initial MAC command exchange
  // with the network server complete, then drop back to FINAL_DR
  // and remember that state across reboots.
  if (!macSettled) {
    bootstrapCount++;
    Serial.print(F("  Bootstrap uplink ")); Serial.print(bootstrapCount);
    Serial.print(F("/")); Serial.println(BOOTSTRAP_UPLINKS);

    if (bootstrapCount >= BOOTSTRAP_UPLINKS) {
      node.setDatarate(FINAL_DR);
      macSettled = true;
      saveMacSettled(true);
      Serial.println(F("  MAC queue settled - switched to final data rate"));
    }
  }

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
    Serial.println(F("========================================"));
    Serial.println(F(" RA-01SH (SX1262) LoRaWAN ABP Test US915"));
    Serial.println(F("========================================"));
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