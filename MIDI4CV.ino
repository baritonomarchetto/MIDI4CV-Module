/* 
 *    MIDI4CV - arduino MIDI-to-4xCV for VCO pitch control
 *    
 *    It used Francois Best's MIDI library and Benoit Shillings's MCP4728 library
 *    https://github.com/FortySevenEffects/arduino_midi_library
 *    https://github.com/BenoitSchillings/mcp4728
 *  
 *    Translates MIDI note-on/off messages in up to 4 pitch control voltages.     
 *    Main hardware consists of an arduino nano and a quad,I2C 12-bit DAC (MCP4728).    
 *    A 2 poles DIP-switch connected to pin D8 and D7 determines the actual number of voices (1-4).    
 *    There's a single gate output for paraphonic envelopes triggering.
 *    A latching switch grounding pin D4 sets CV outputs at unison.
 *    Voice stealing, poly mode: TO BE CODED. Actually an additional note is not played.
 *    Note priority, unison mode: TO BE CODED. Actually is set to "latest".
 *    
 *    by Barito, sept 2021
 */
 
#include <MIDI.h>
#include <Wire.h>
#include "mcp4728.h"

mcp4728 dac = mcp4728(0); // initiate mcp4728 object, Device ID = 0

#define MAXOUTS  4
#define MAX_EXT_V  61
#define MAX_INT_V  50
#define MIDI_CHANNEL 1
#define MIDIOFFSET 24

//pNote goes to zero when the slot is empty
struct voiceSlot {const byte gatePin; byte pNote; boolean busy;} 
voiceSlot[MAXOUTS] = {
{5, 0, 0}, //PWM
{6, 0, 0},  //PWM
{9, 0, 0},  //PWM
{10, 0, 0}   //PWM
};

const byte sw0Pin = 8;
const byte sw1Pin = 7;
const byte unisonPin = 4;
boolean unisonState;
byte actVoices;
int pitchbend;
const byte LDACpin = 12;
byte noteShift = 24;

MIDI_CREATE_DEFAULT_INSTANCE();

// DAC's V/oct tabulated values (5V external Vref)
// 5V -> 5 octaves -> 5*12 notes -> 60+1 values
const int cvExtRef[MAX_EXT_V] = {
 0,  68, 137,  205,  273,  341,  410,  478,  546,  614,  683,  751,
 819,  887,  956,  1024, 1092, 1160, 1229, 1297, 1365, 1433, 1502, 1570,
 1638, 1706, 1775, 1843, 1911, 1979, 2048, 2116, 2184, 2252, 2321, 2389,
 2457, 2525, 2594, 2662, 2730, 2798, 2867, 2935, 3003, 3071, 3140, 3208,
 3276, 3344, 3413, 3481, 3549, 3617, 3686, 3754, 3822, 3890, 3959, 4027, 4095
};

// DAC's V/oct tabulated values (2048 mV internal Vref)
const int cvIntRef[MAX_INT_V] = {
 0,  83, 167,  250,  333,  417,  500,  583,  667,  750,  833,  917,
 1000,  1083,  1167,  1250, 1333, 1417, 1500, 1583, 1667, 1750, 1833, 1917,
 2000,  2083,  2167,  2250, 2333, 2417, 2500, 2583, 2667, 2750, 2833, 2917,
 3000,  3083,  3167,  3250, 3333, 3417, 3500, 3583, 3667, 3750, 3833, 3917,
 4000,  4083
}; //4095 MAX

void setup(){
//GATES initialization
for (int g = 0; g < MAXOUTS; g++){
pinMode(voiceSlot[g].gatePin, OUTPUT);
digitalWrite(voiceSlot[g].gatePin, LOW);
}
//switch initialization and gate outputs number setting
pinMode(sw0Pin, INPUT_PULLUP);
pinMode(sw1Pin, INPUT_PULLUP);
if(digitalRead(sw0Pin) == HIGH && digitalRead(sw1Pin) == HIGH) {actVoices = 4;}
else if(digitalRead(sw0Pin) == HIGH && digitalRead(sw1Pin) == LOW) {actVoices = 3;}
else if(digitalRead(sw0Pin) == LOW && digitalRead(sw1Pin) == HIGH) {actVoices = 2;}
else {actVoices = 1;}
//unison switch
pinMode(unisonPin, INPUT_PULLUP);
unisonState = digitalRead(unisonPin);
//MIDI initialization
MIDI.setHandleNoteOn(HandleNoteOn);
MIDI.setHandleNoteOff(HandleNoteOff);
MIDI.setHandlePitchBend(HandlePitchBend);
MIDI.begin(MIDI_CHANNEL);
//DAC initialization
pinMode(LDACpin, OUTPUT); 
digitalWrite(LDACpin, LOW);
dac.begin();  // initialize i2c interface
//dac.vdd(5000); // set VDD(mV) of MCP4728 for correct conversion between LSB and Vout
dac.setPowerDown(0, 0, 0, 0); // set Power-Down ( 0 = Normal , 1-3 = shut down most channel circuit, no voltage out) (1 = 1K ohms to GND, 2 = 100K ohms to GND, 3 = 500K ohms to GND)
dac.setVref(1,1,1,1); // set to use internal voltage reference (2.048V)
dac.setGain(1, 1, 1, 1); // set the gain of internal voltage reference ( 0 = gain x1, 1 = gain x2 )
//dac.setVref(0, 0, 0, 0); // set to use external voltage reference (Vdd)
dac.analogWrite(0, 0, 0, 0);
}

void HandleNoteOn(byte channel, byte note, byte velocity) {
note = note-MIDIOFFSET;
if(note < MAX_INT_V){
  if(unisonState == HIGH){ //UNISON DISABLED
    if (digitalRead(voiceSlot[0].gatePin) == LOW){ //waiting first note ...
      digitalWrite(voiceSlot[0].gatePin, HIGH); //GATE 1
      voiceSlot[0].busy = true;
      for (int a = 0; a < actVoices; a++){ //all notes allocated, but not busy
        voiceSlot[a].pNote = note;
        dac.analogWrite(a, cvIntRef[note]);
      }
    }
    else { //gate is open, some note is playing ...
      for (int b = 0; b < actVoices; b++){
        if (voiceSlot[b].busy == false){//if the slot is not busy ...
          voiceSlot[b].busy = true;
          voiceSlot[b].pNote = note;
          dac.analogWrite(b, cvIntRef[note]);    
          break;    
        }
      }
    }
  }
  else {//UNISON ENABLED
    dac.analogWrite(cvIntRef[note], cvIntRef[note], cvIntRef[note], cvIntRef[note]);
    for (int i = 0; i < actVoices; i++){
      voiceSlot[i].pNote = note;
    }
    digitalWrite(voiceSlot[0].gatePin, HIGH); //GATE 1
  }
  }
}

void HandleNoteOff(byte channel, byte note, byte velocity) {
note = note-MIDIOFFSET;
if(note < MAX_INT_V){
  if(unisonState == HIGH){ //UNISON DISABLED
  for (int a = 0; a < actVoices; a++){
    if (note == voiceSlot[a].pNote){ //if the note is one of those playing ...
      voiceSlot[a].busy = false; //slot is now free for new alloction
      for (int b = 0; b < actVoices; b++){ //search for a note still in use and fatten it
        if (voiceSlot[b].busy == true){ //if there's a note playing
          voiceSlot[a].pNote = voiceSlot[b].pNote; //keep track of pithes 
          dac.analogWrite(a, cvIntRef[voiceSlot[a].pNote]); //update pitch    
          break;
        }
      }
    }
  }
  if(voiceSlot[0].busy == false && voiceSlot[1].busy == false && voiceSlot[2].busy == false && voiceSlot[3].busy == false){ //all slot are empty -> no key press
    digitalWrite(voiceSlot[0].gatePin, LOW);
  }
  }
  else { //UNISON ENABLED
    if (note == voiceSlot[0].pNote){
      for (int c = 0; c < actVoices; c++){
        voiceSlot[c].busy = false; //all slots are now empty
      }
      digitalWrite(voiceSlot[0].gatePin, LOW);
    }
  }
  }
}

void HandlePitchBend(byte channel, int bend){
pitchbend = bend>>4;
for (int p=0; p<actVoices; p++){
  dac.analogWrite(p, cvIntRef[voiceSlot[p].pNote] + pitchbend);
}
}

void loop(){
MIDI.read();
SwitchRead();
}

void SwitchRead(){
if(digitalRead(unisonPin) != unisonState){
  unisonState = !unisonState;
}
}
