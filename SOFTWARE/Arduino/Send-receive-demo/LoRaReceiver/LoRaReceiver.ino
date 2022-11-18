//this demo will work out-of-the-box (without any changes)
//only with the Olimex 2xMOD-LoRa868-ANT + 2xESP32-EVB
//for other boards remember to edit pins and frequency
//remember you need to have antenna on your LoRa module

#include <SPI.h>
#include <LoRa.h>

//defines below are made for ESP32-EVB
//edit them to fit your hardware

#define ss 17
#define rst 16
#define dio0 4

void setup() {
  delay(1000);
  Serial.begin(9600);
  while (!Serial);

  Serial.println("LoRa Receiver");
  LoRa.setPins(ss, rst, dio0);

//change between 868E6 and 915E6 depending
//whether you use LoRa868 or LoRa915
  if (!LoRa.begin(868E6)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }
}

void loop() {
  // try to parse packet
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    // received a packet
    Serial.print("Received packet '");

    // read packet
    while (LoRa.available()) {
      Serial.print((char)LoRa.read());
    }

    // print RSSI of packet
    Serial.print("' with RSSI ");
    Serial.println(LoRa.packetRssi());
  }
}
