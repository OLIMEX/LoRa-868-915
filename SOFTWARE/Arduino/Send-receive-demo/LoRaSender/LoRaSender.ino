//this demo will work out-of-the-box (without any changes)
//only with the Olimex 2xMOD-LoRa868-ANT + 2xESP32-EVB
//for other boards remember to edit pins and frequency
//remember you need to have antenna on your LoRa module


//this part of the demo sends a hello message with consecutive 
//number in 5 second intervals


#include <SPI.h>
#include <LoRa.h>

#define ss 17
#define rst 16
#define dio0 4

int counter = 0;

void setup() {
  delay(1000);
  Serial.begin(9600);
  while (!Serial);

  Serial.println("LoRa Sender");
  LoRa.setPins(ss, rst, dio0);
  if (!LoRa.begin(868E6)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }
}

void loop() {
  Serial.print("Sending packet: ");
  Serial.println(counter);

  // send packet
  LoRa.beginPacket();
  LoRa.print("hello ");
  LoRa.print(counter);
  LoRa.endPacket();

  counter++;

  delay(5000);
}
