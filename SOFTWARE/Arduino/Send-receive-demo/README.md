Demo software for Olimex MOD-LoRa868-ANT/MOD-LoRa868-EANT and Olimex ESP32-EVB

Demo uses sandeepmistry's LoRa library and examples:

https://github.com/sandeepmistry/arduino-LoRa

Configuration for ESP32-EVB comes with ESP32 package for Arduino:

https://github.com/espressif/arduino-esp32

Required hardware:

- Two (2) MOD-LoRa868-ANT/MOD-LoRa868-EANT modules

- Two (2) ESP32-EVB boards

- Two (2) micro USB cables

Remember LoRa modules would NOT work without an external antenna!

Required software:

- Arduino IDE, get it from here:

https://www.arduino.cc/en/software

- ESP32 package for Arduino IDE, install it following this advice:

https://espressif-docs.readthedocs-hosted.com/projects/arduino-esp32/en/latest/installing.html

- Arduino LoRa library by Sandeep Mistry, install following the advice here:

https://github.com/OLIMEX/LoRa-868-915/tree/main/SOFTWARE/Arduino

Once set, remember to select Olimex ESP32-EVB from board selector and the COM port where 
the board is located.

Each MOD-LoRa868 gets connected directly to the UEXT of each of the 
ESP32-EVB boards. One couple gets programmed with LoRaSender.ino,
the other with - LoRaReceiver.ino.

Connect both boards to free USB ports.

Open serial terminal software (like PuTTY) on each board's virtual COM port. Baud is 9600.

One board sends hello packets in a 5 second interval. 

The other receives packets "hello #number" and reports signal strength.

Refer to picture test.ping.

The demo can be used with MOD-LoRa915-ANT or other boards with SPI:

- If you decide to use MOD-LoRa915-ANT instead, you need to edit the code,
change LoRa.begin(868E6) to LoRa.begin(915E6) in both .ino files.

- If you decide to use another board that has free SPI make sure to change
defines at start so that it corresponds to your board; notice that
if MISO, MOSI, CLK are different to what is defined for your board
you might need to edit the library.
