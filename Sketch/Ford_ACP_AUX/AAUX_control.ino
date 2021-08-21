enum InlineControlCommand {
  noCommand,
  cancelCommand,
  playPause,
  nextTrack,
  prevTrack,
  fastForwardTrack,
  rewindTrack,
  activateSiri
};

const uint8_t inlineControlPin = 6; // Pin 6 connected to transistor for inline control

InlineControlCommand lastCommand;

#include <SoftwareSerial.h>

SoftwareSerial mySerial(8, 9); // RX, TX

/*
 * Simulate inline control based on value of lastCommand.
 */
void inline_control_handler() {
  //The shortest duration of digitalWrite that is sensed by iPhone SE is ~60ms.
  if (lastCommand != noCommand) {
    switch(lastCommand) {
      case playPause:
         mySerial.write("AT+CB\r\n");
        break;
      case nextTrack:
         mySerial.write("AT+CC\r\n");
        break;
      case prevTrack:
        mySerial.write("AT+CD\r\n");
        break;
      case fastForwardTrack:
         mySerial.write("AT+CC\r\n");

        break;
      case rewindTrack:
        mySerial.write("AT+CD\r\n");

        break;
      case activateSiri:
        digitalWrite(inlineControlPin, HIGH);
        delay(2000);
        digitalWrite(inlineControlPin, LOW);
        break;
    }
    lastCommand = noCommand;
  }
}

/*
 * Playback inline control setup
 */
void inline_control_setup() {
  //Setup inline control pin
  mySerial.begin(9600);
}
