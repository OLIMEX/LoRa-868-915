#include <Arduino.h>
#include <SPI.h>              // include libraries
#include <LoRa.h>

#define FREQUENCY 867900000
//#define FREQUENCY 915200000

//#define FREQUENCY 868500000

//#define leonardo

#ifdef megaatmega2560
const int csPin = 53;          // LoRa radio chip select
const int resetPin = 21;       // LoRa radio reset
const int irqPin = 18;         // change for your board; must be a hardware interrupt pin
#endif

#ifdef leonardo
const int csPin = 13;          // LoRa radio chip select
const int resetPin = 3;       // LoRa radio reset
const int irqPin = 1;         // change for your board; must be a hardware interrupt pin
#endif



long lastSendTime = 0;        // last send time
int interval = 2000;          // interval between sends

void setup() {
  // put your setup code here, to run once:

#ifdef leonardo
  pinMode(8, OUTPUT);
  digitalWrite(8, 0);

  pinMode(7,OUTPUT);
  digitalWrite(7,0);
  pinMode(9,OUTPUT);
  digitalWrite(9,0);
  delay(100);

  pinMode(8,OUTPUT);
  digitalWrite(8,0);
  delay(100);

#endif
  Serial.begin(115200);                   // initialize serial
  while (!Serial);

  Serial.println("LoRa Ping-Pong");

  // override the default CS, reset, and IRQ pins (optional)
  LoRa.setPins(csPin, resetPin, irqPin);// set CS, reset, IRQ pin

  if (!LoRa.begin(FREQUENCY)) {             // initialize ratio at 915 MHz
    Serial.println("LoRa init failed. Check your connections.");
    while (true);                       // if failed, do nothing
  }
	LoRa.setSignalBandwidth(125E3);
	LoRa.setSpreadingFactor(10);

 // LoRa.setSignalBandwidth(125E3);
	//LoRa.setSpreadingFactor(12);
	LoRa.setTxPower(14,0);
  Serial.println("LoRa init succeeded.");
  interval = random(14000) + 1000;    // 2-3 seconds
}


void sendMessage(String outgoing) {
  LoRa.beginPacket();                   // start packet
  //LoRa.write(outgoing.length());        // add payload length
  LoRa.print(outgoing);                 // add payload
  LoRa.endPacket();                     // finish packet and send it
}

void onReceive(int packetSize) {
  if (packetSize == 0) return;          // if there's no packet, return

  String incoming = "";

  while (LoRa.available()) {
    incoming += (char)LoRa.read();
  }

  if (incoming == "PING")
  { 
    String message = "PONG";   // send a message
    sendMessage(message);
    Serial.println("Sending Echo " + message);
    lastSendTime = millis();            // timestamp the message
    interval = random(14000) + 1000;    // 15 seconds

  }
  // if message is for this device, or broadcast, print details:
  Serial.println("Message: " + incoming);
  Serial.println("RSSI: " + String(LoRa.packetRssi()));
  Serial.println("Snr: " + String(LoRa.packetSnr()));
  Serial.println();
  
}

void loop() {
  // put your main code here, to run repeatedly:
   if (millis() - lastSendTime > interval) {
    String message = "PING";   // send a message
    sendMessage(message);
    Serial.println("Sending " + message);
    lastSendTime = millis();            // timestamp the message
    interval = random(4000) + 1000;    // 2-3 seconds
  }

  // parse for a packet, and call onReceive with the result:
  onReceive(LoRa.parsePacket());
}