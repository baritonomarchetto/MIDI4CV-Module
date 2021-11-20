/* 
 *    MIDI4CV - arduino MIDI-to-4xCV for VCO pitch control
 *    
 *    It used Francois Best's MIDI library and Benoit Shillings's MCP4728 library
 *    https://github.com/FortySevenEffects/arduino_midi_library
 *    https://github.com/BenoitSchillings/mcp4728
 *  
 *    Translates MIDI note-on/off messages in 4 pitch control voltages.     
 *    Main hardware consists of an arduino nano and a quad,I2C 12-bit DAC (MCP4728).    
 *    
 *    A latching switch grounding pin D4 sets CV outputs at unison.
 *    
 *    Note priority, unison mode: user definable (see NOTE_PRIORITY constant).
 *    
 *    Gate retrigger: user definable (use mod wheel to set).
 *    
 *    Voice stealing, poly mode: TO BE CODED. Actually an additional note is not played.
 *    
 *    
 *    by Barito, nov 2021
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
#define NOTE_PRIORITY 0//note priority options: 0 - highest, 1 - lowest

bool retrigger;

int noteCount;
int activeSlot;
int highestNote;
int lowestNote;

struct voiceSlot {const byte gatePin; byte pNote;} 
voiceSlot[MAXOUTS] = {
{5, 0}, //PWM
{6, 0},  //PWM
{9, 0},  //PWM
{10, 0}   //PWM
};

const byte unisonPin = 4;
boolean unisonState;
int pitchbend = 0;
const byte LDACpin = 12;
byte noteShift = 24;
int noteOverflow;

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
//unison switch
pinMode(unisonPin, INPUT_PULLUP);
unisonState = digitalRead(unisonPin);
//MIDI initialization
MIDI.setHandleNoteOn(HandleNoteOn);
MIDI.setHandleNoteOff(HandleNoteOff);
MIDI.setHandlePitchBend(HandlePitchBend);
MIDI.setHandleControlChange(handleControlChange);
retrigger = 1;
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
noteCount++;
note = note-MIDIOFFSET;
if(note < MAX_INT_V){
  switch (noteCount){
    case 0:
    //do nothing
    break;
    case 1: //same pitch to all outputs (both in unison and poly mode)
      dac.analogWrite(cvIntRef[note] + pitchbend, cvIntRef[note] + pitchbend, cvIntRef[note] + pitchbend, cvIntRef[note] + pitchbend);
      digitalWrite(voiceSlot[0].gatePin, HIGH);//open gate
      voiceSlot[0].pNote = note;
      voiceSlot[1].pNote = note;
      voiceSlot[2].pNote = note;
      voiceSlot[3].pNote = note;
      lowestNote = note;
      highestNote = note;
    break;
      case 2:
        voiceSlot[2].pNote = note;
        voiceSlot[3].pNote = note;
        NoteHeightDef();
        if(unisonState == HIGH){ //UNISON DISABLED
          if(retrigger){
            digitalWrite(voiceSlot[0].gatePin, LOW); //GATE 1 CLOSED
          }  
          dac.analogWrite(2, cvIntRef[note] + pitchbend);//new pitch to latest two voices
          dac.analogWrite(3, cvIntRef[note] + pitchbend);//new pitch to latest two voices
        }
        else{ //UNISON
          if(retrigger){
            UniRetrigger(note);
          }
          UnisonDAC();
        }
        digitalWrite(voiceSlot[0].gatePin, HIGH); //GATE 1 OPEN to complete the retrigger routine
      break;
      case 3:
        voiceSlot[3].pNote = note;
        NoteHeightDef();
        if(unisonState == HIGH){ //UNISON DISABLED
          if(retrigger){
            digitalWrite(voiceSlot[0].gatePin, LOW); //GATE 1 CLOSED
          }
          dac.analogWrite(3, cvIntRef[note] + pitchbend);//third voice stolen from the second doubled voice
        }
        else{ //UNISON
          if(retrigger){
            UniRetrigger(note);
          }
          UnisonDAC();
        }
        digitalWrite(voiceSlot[0].gatePin, HIGH); //GATE 1 OPEN to complete the retrigger routine
      break;
      case 4:
        voiceSlot[1].pNote = note;
        NoteHeightDef();
        if(unisonState == HIGH){ //UNISON DISABLED
          if(retrigger){
            digitalWrite(voiceSlot[0].gatePin, LOW); //GATE 1 CLOSED
          }
          dac.analogWrite(1, cvIntRef[note] + pitchbend);//fourth voice stolen from the first doubled voice
        }
        else{ //UNISON
          if(retrigger){
            UniRetrigger(note);
          }
          UnisonDAC();
        }
        digitalWrite(voiceSlot[0].gatePin, HIGH); //GATE 1 OPEN to complete the retrigger routine
      break;
      default: //POLYPHONY EXCEEDED (4+ notes)
        /*if(unisonState == HIGH){ //UNISON DISABLED
          #ifdef RETRIGGER
            digitalWrite(voiceSlot[0].gatePin, LOW); //GATE 1 CLOSED
          #endif
          for (int a = 0; a < MAXOUTS; a++){
            if (voiceSlot[a].pNote == lowestNote){ //search for the LOWEST pitch
              voiceSlot[a].pNote == note; //replace the lowest note with the 4+ note
              dac.analogWrite(a, cvIntRef[note]);
              break;
            }
          }
          NoteHeightDef();
        }
        else{ //UNISON
          #ifdef RETRIGGER
            UniRetrigger(note);
          #endif
          UnisonDAC();
        }
        digitalWrite(voiceSlot[0].gatePin, HIGH); //GATE 1 OPEN to complete the retrigger routine
        */
      break;
    }
  }
}

void HandleNoteOff(byte channel, byte note, byte velocity) {
noteCount--;
note = note-MIDIOFFSET;
if(note < MAX_INT_V){  
  switch (noteCount){ 
    case 0://close gate out (common between unison and poly mode)
      digitalWrite(voiceSlot[0].gatePin, LOW); //GATE 1 closed
    break;
    default:
      for (int a = 0; a < MAXOUTS; a++){ //search for an active pitch
        if (voiceSlot[a].pNote != note){
          activeSlot = a;
        }
      }
      for (int b = 0; b < MAXOUTS; b++){ //search for the pitches to be updated
        if (voiceSlot[b].pNote == note){
          voiceSlot[b].pNote = voiceSlot[activeSlot].pNote; //... reallocate it...
          NoteHeightDef(); // redefine notes height
          if(unisonState == HIGH){ //UNISON DISABLED
            /*if(retrigger){
              digitalWrite(voiceSlot[0].gatePin, LOW); //GATE 1 CLOSED
           }*/
            dac.analogWrite(b, cvIntRef[voiceSlot[activeSlot].pNote] + pitchbend);//...and set the new V out          
          }
          else { //UNISON ENABLED
            if(retrigger){
              UniRetrigger(note);
            }
            UnisonDAC();
          }
        }
       }
      digitalWrite(voiceSlot[0].gatePin, HIGH); //GATE 1 OPEN to complete the retrigger routine
    break;
   }//switch close
}
}
 
void HandlePitchBend(byte channel, int bend){
pitchbend = bend>>4;
//check for data overflow
for (int p=0; p<MAXOUTS; p++){
  noteOverflow = cvIntRef[voiceSlot[p].pNote] + pitchbend;
  if (noteOverflow <= 4095){
    //set DAC values
    dac.analogWrite(p, cvIntRef[voiceSlot[p].pNote] + pitchbend); 
  }
}
}

void handleControlChange(byte channel, byte controlNumber, byte value) {
if(controlNumber == 1){ //MOD WHEEL
  if(value < 90){
    retrigger = 0;
  }
  else {
    retrigger = 1;
  }
}
}

void NoteHeightDef(){
highestNote = 0;//reset
lowestNote = MAX_INT_V;//reset
for (int a = 0; a < MAXOUTS; a++){
  if(voiceSlot[a].pNote > highestNote){
     highestNote = voiceSlot[a].pNote;
  }
  if(voiceSlot[a].pNote < lowestNote){
     lowestNote = voiceSlot[a].pNote;
  }
}
}

void UnisonDAC(){
#if NOTE_PRIORITY == 0
  dac.analogWrite(cvIntRef[highestNote] + pitchbend,cvIntRef[highestNote] + pitchbend,cvIntRef[highestNote] + pitchbend,cvIntRef[highestNote] + pitchbend);
#elif NOTE_PRIORITY == 1
  dac.analogWrite(cvIntRef[lowestNote] + pitchbend,cvIntRef[lowestNote] + pitchbend,cvIntRef[lowestNote] + pitchbend,cvIntRef[lowestNote] + pitchbend);
#endif
}

void UniRetrigger (byte note){
//gate retrigger, unison
#if NOTE_PRIORITY == 0//highest
  if(note == highestNote){//ok, retrigger!
    digitalWrite(voiceSlot[0].gatePin, LOW); //GATE 1 closed
  }
#elif NOTE_PRIORITY == 1//lowest
  if(note == lowestNote){//ok, retrigger!
    digitalWrite(voiceSlot[0].gatePin, LOW); //GATE 1 closed
  }
#endif
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
