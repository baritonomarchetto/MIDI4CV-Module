/* 
 *    MIDI4CV - MIDI TEST
 *  
 *    Built-in LED turns on for 1 second at every MIDI note received
 */
 
#include <MIDI.h>

MIDI_CREATE_DEFAULT_INSTANCE();

void setup(){
    pinMode(LED_BUILTIN, OUTPUT);
    MIDI.begin(MIDI_CHANNEL_OMNI); // Launch MIDI and listen to all channels
}

void loop(){
if (MIDI.read())                    // If we have received a message
{
    digitalWrite(LED_BUILTIN, HIGH);
    delay(1000);                    // Wait for a second
    digitalWrite(LED_BUILTIN, LOW);
    delay(1000);                    // Wait for a second
}
}
