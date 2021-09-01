//MCP4728 BASIC TEST SKETCH
//open the serial monitor: if ID is returned, the DAC is recognized
//DAC outs voltages are cycled at 1Hz frequency between 0V, 2.5V, 5V. 

#include <Wire.h>
#include "mcp4728.h"

mcp4728 dac = mcp4728(0); // instantiate mcp4728 object, Device ID = 0

void setup(){
Serial.begin(9600);  // initialize serial interface for ID print()
dac.begin();  // initialize i2c interface
dac.vdd(5000); // set VDD(mV) of MCP4728 for correct conversion between LSB and Vout
const byte LDACpin = 12;
pinMode(LDACpin, OUTPUT); 
digitalWrite(LDACpin, LOW);
dac.setVref(0, 0, 0, 0); // set to use external voltage reference (2.048V)
dac.setGain(1, 1, 1, 1); // set the gain of internal voltage reference ( 0 = gain x1, 1 = gain x2 )
dac.setPowerDown(0, 0, 0, 0); // set Power-Down ( 0 = Normal , 1-3 = shut down most channel circuit, no voltage out) (1 = 1K ohms to GND, 2 = 100K ohms to GND, 3 = 500K ohms to GND)
int id = dac.getId(); // return devideID of object
Serial.print("Device ID  = "); // serial print of value
Serial.println(id, DEC); // serial print of value
}


void loop()
{
dac.analogWrite(0, 0, 0, 0); // write to input register of DAC four channel (channel 0-3) together. Value 0-4095
delay(1000);
dac.analogWrite(2048, 2048, 2048, 2048); 
delay(1000);
dac.analogWrite(4095, 4095, 4095, 4095); 
delay(1000);
}
