//this demo will work out-of-the-box (without any changes)
//only with the Olimex 2xMOD-LoRa868-ANT + 2xESP32-EVB
//for other boards remember to edit pins and frequency
//remember you need to have antenna on your LoRa module


//this part of the demo sends a hello message with consecutive 
//number in 1 second intervals


#include <SPI.h>
#include <LoRa.h>

#define ss 17
#define rst 16
#define dio0 4

int counter = 0;

void setup() {
  delay(1000);
  Serial.begin(115200);
  while (!Serial);

  Serial.println("LoRa Sender");
  LoRa.setPins(ss, rst, dio0);
//change between 866E6, 868E6 and 915E6 depending
//whether you use LoRa868 or LoRa915
  if (!LoRa.begin(866E6))
  //if (!LoRa.begin(868E6))
  //if (!LoRa.begin(915E6))
  {
    Serial.println("Starting LoRa failed!");
    while (1);
  }
  
  // LoRa settings, uncomment and edit if needed
  //LoRa.setSignalBandwidth(125E3);
  //LoRa.setSpreadingFactor(12);

  // VERY IMPORTANT FOR SENDER! Set the PA boost to 0 (by default it's PA_OUTPUT_PA_BOOST_PIN = 1)
  // or else the range will be very low
  LoRa.setTxPower(14,0);  
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

  delay(1000);
}
