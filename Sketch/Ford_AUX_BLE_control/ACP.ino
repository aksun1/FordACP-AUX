/*
 * Original code for Yampp -  Andrew Hammond <andyh@mindcode.co.uk>
 *
 * For Arduino Mega 2560 - Krzysztof Pintscher <niou.ns@gmail.com>
 * Full schematics and connection by Krzysztof Pintscher
 * please keep our credits in header :)
 *
 * Integration with AT Commands for OVC3868 - Dale Thomas <dalethomas@me.com>
 *
 ******************************************************************
 * Revisions:
 *
 * 2013-12-17 version 0.9 - public relase
 * 2014-11-30 version 1.0 - Annotations and integration with AT file -DT
 * 2017-05-07 version 1.1 - Removed Bluetooth functionality and editted ports for use with Arduino UNO. -Anson Liu (ansonliu.com)
 *
 */

//Ford ACP variables
#include <MsTimer2.h>
typedef unsigned char u08;
typedef unsigned short u16;
typedef unsigned long  u32;

#ifndef cbi // "clear bit"
 #define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#endif
#ifndef sbi // "set bit"
 #define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
#endif
#define outp(VAL,ADRESS) ((ADRESS) = (VAL))
#define inp(VAL) (VAL)

#define ACP_UART_BAUD_RATE  9600
#define ACP_UART_BAUD_SELECT ((u32)((u32)16000000/(ACP_UART_BAUD_RATE*16L)-1))
#define ACP_LISTEN 0
#define ACP_SENDACK 1
#define ACP_MSGREADY 2
#define ACP_WAITACK 3
#define ACP_SENDING 4

const uint8_t switchPin = 7; // Pin 7 connected to pins 2 & 3 (RE/DE) on MAX485

uint8_t acp_rx[12];
uint8_t acp_tx[12];
uint8_t acp_rxindex;
uint8_t acp_txindex;
u08 acp_status;
u08 acp_txsize;
u08 acp_timeout;
u08 acp_checksum;
u08 acp_retries;
u08 acp_mode;
uint16_t acp_ltimeout;
boolean rewindState = false;
boolean ffState = false;
uint8_t wPlayTime = 0;
uint8_t currentTrack = 1;
boolean reset_timer = false;

/*
 * ACP communication setup
 */
void acp_setup() {
  outp(0xff, PORTD);
  outp(0xC0, DDRD);
  
  pinMode(switchPin, OUTPUT);
  digitalWrite(switchPin, LOW);
  delay(500);
  acp_uart_init(ACP_UART_BAUD_SELECT); // Initialize the ACP USART
  delay(1);

  MsTimer2::set(1000, PlayTime); // 1000ms period
  MsTimer2::start();
}

void acp_uart_init(unsigned short baud) {
  cli();
  outp((u08)(baud >> 8), UBRR0H);
  outp((u08) baud, UBRR0L);
  sbi(UCSR0B, RXEN0); // Receiver enable
  sbi(UCSR0B, TXEN0); // Transmitter enable
  sbi(UCSR0B, RXCIE0); // RX complete interrupt enable
  cbi(UCSR0B, UDRIE0); // USART data register empty complete enable
  sbi(UCSR0B, UCSZ02); // Character size n (setting this to one enables 9-bit character data)
  sei();
}

SIGNAL(USART_RX_vect) {
  u08 eod = (inp(UCSR0B) & _BV(RXB80));
  uint8_t ch = inp(UDR0);

  if (acp_status != ACP_LISTEN) return; // ignore incoming msgs if busy processing
  if (!eod) acp_checksum += ch;
  acp_rx[acp_rxindex++] = ch;
  if (acp_rxindex > 12) acp_reset();
  else if (eod) {
    if (acp_checksum == ch) // Valid ACP message - send ack
    {
      acp_status = ACP_SENDACK;
      acp_handler();
    } else {
      acp_reset(); // Abort message
    }
  }
  return;
}

void acp_txenable(boolean enable) {
  if (enable) PORTD |= (1 << PD7); //sets a high state on pin PD7
  else PORTD &= ~(1 << PD7); //sets the low state on pin PD7
  asm volatile("nop");
}

void acp_sendack(void) {
  while (!(inp(PORTD) & 0x01)) // Wait till RX line goes high (idle)
    cbi(UCSR0B, RXCIE0);
  acp_txenable(true);
  sbi(UCSR0A, TXC0); // Set TXC to clear it!!
  cbi(UCSR0B, TXB80);
  outp(0x06, UDR0);
  while (!(inp(UCSR0A) & _BV(TXC0))); // wait till buffer clear
  acp_txenable(false);
}

void acp_reset(void) {
  acp_retries = 0;
  acp_timeout = 0;
  acp_checksum = 0;
  acp_rxindex = 0;
  acp_txindex = 0;
  acp_txsize = 0;
  acp_status = ACP_LISTEN;
}

void acp_sendmsg(void) {
  u08 i;
  while (!(inp(PORTD) & 0x01)) // Wait till RX line goes high (idle)
    cbi(UCSR0B, RXCIE0);
  acp_txenable(true);
  _delay_us(104 * 17);
  for (i = 0; i <= acp_txsize; i++) {
    while (!(inp(UCSR0A) & _BV(UDRIE0))); // wait till tx buffer empty
    cbi(UCSR0B, TXB80);
    if (i == acp_txsize) {
      sbi(UCSR0A, TXC0); // set TXC to clear it!
      sbi(UCSR0B, TXB80); // set 9th bit at end of message
    }
    outp(acp_tx[i], UDR0);
  }
  while (!(inp(UCSR0A) & _BV(TXC0))); // wait till last character tx complete
  acp_txenable(false); // switch back to rx mode immediately
  acp_status = ACP_WAITACK; // wait for ACK
  sbi(UCSR0B, RXCIE0);
}

void acp_handler() {
  if (acp_status == ACP_LISTEN) {
    if (++acp_ltimeout == 1000) acp_ltimeout = 0;
  }
  if (acp_status == ACP_SENDACK) {
    acp_sendack();
    acp_status = ACP_MSGREADY;
  } else if (acp_status == ACP_WAITACK) acp_reset(); // HU does not seem to return an ACK
  if (acp_status == ACP_MSGREADY) {
    acp_status = ACP_SENDING;
    acp_process(); // Process message
  } else if (acp_status == ACP_SENDING) {
    acp_timeout++;
    if (!acp_timeout) {
      acp_reset();
      acp_txenable(false);
    }
  }
}

void acp_process(void) {

  acp_timeout = 0;
  acp_tx[0] = 0x71; // medium/low priority
  acp_tx[1] = 0x9b;
  acp_tx[2] = 0x82; // message from "CD Changer" (bluetooth interface)
  acp_tx[3] = acp_rx[3];

  if (acp_rx[2] == 0x80) // Message from Head Unit
  {
    if (acp_rx[1] == 0x9a || acp_rx[1] == 0x9b) // CD Changer functional address
    {
      switch (acp_rx[3]) {
      case 0xC8: // Handshake 3 - CD Changer now recognized
        acp_tx[4] = acp_rx[4];
        acp_chksum_send(5);
        break;

      case 0xFC: // Handshake 2
        acp_tx[4] = acp_rx[4];
        acp_chksum_send(5);
        break;

      case 0xE0: // Handshake 1
        acp_tx[4] = 0x04;
        acp_chksum_send(5);
        break;

      case 0xFF: // Current disc status request - responses are
        acp_tx[4] = 0x00;
        // 00 - Disc OK
        // 01 - No disc in current slot
        // 02 - No discs
        // 03 - Check disc in current slot
        // 04 - Check all discs!
        acp_chksum_send(5);
        break;

      case 0x42: // [<- Tune]
        /*
        switch(callStatus){
        case false:
        callStatus = true;
        at_process(8);     // Answer Call
        break;
        case true:
        callStatus = false;
        at_process(10);    // End Call
        break;
        }
        */
        acp_chksum_send(5);

        break;

      case 0xC2:
        //radio number buttons?
      /*
        //acp_tx[4] = 0x00;
        switch (acp_rx[4]) {
          case 1:
            lastCommand = playPause;
            break;
          case 2:
            lastCommand = activateSiri;
            break;
          case 3:
            lastCommand = prevTrack;
            break;
          case 4:
            lastCommand = nextTrack;
            break;
          case 5:
            switch (rewindState) {
            case false:
              rewindState = true;
              // Start Rewind
              lastCommand = rewindTrack;
              break;
            case true:
              rewindState = false;
              // Stop Fast Forward / Rewind
              lastCommand = cancelCommand;
              break;
            }
            break;
          case 6:
            switch (ffState) {
            case false:
              ffState = true;
              // Start Fast Forward
              lastCommand = fastForwardTrack;
              break;
            case true:
              ffState = false;
              // Stop Fast Forward / Rewind
              lastCommand = cancelCommand;
              break;
            }
            break;
        }
*/
        acp_chksum_send(5);
        break;
      case 0xD0:
        if (acp_rx[1] == 0x9a) // Command to change disc
        {
          //u08 disc = plist_change(acp_rx[4]);
          acp_tx[4] = 1 & 0x7F;
          acp_chksum_send(5);
          //if(disc & 0x80)
          //acp_nodisc();
          break;
        } else // Request current disc
        {
          //acp_tx[4] = get_disc();
        }
        if (acp_rx[3] != 0xD0) acp_chksum_send(5);
        break;

      case 0xC1: // Command
        acp_mode = acp_rx[4];

        switch (acp_mode) {
        case 0x00: // Switch from CD ie. FM, AM, Tape or power button.
          // there is no opposite to this. turning power back on only sends "play"
          //disconnect audio source
          break;
        case 0x40: // Switch to CD ie. Audio On (vehichle is turned on) or CD button
          // Connect Audio Source
          break;
        case 0x41: // Scan Button
          // Mute/Unmute Mic
          break;
        case 0x42: // FF Button
          switch (ffState) {
          case false:
            ffState = true;
            // Start Fast Forward
            lastCommand = fastForwardTrack;
            break;
          case true:
            ffState = false;
            // Stop Fast Forward / Rewind
            lastCommand = cancelCommand;
            break;
          }
          break;
        case 0x44: // Rew Button
          switch (rewindState) {
          case false:
            rewindState = true;
            // Start Rewind
            lastCommand = rewindTrack;
            break;
          case true:
            rewindState = false;
            // Stop Fast Forward / Rewind
            lastCommand = cancelCommand;
            break;
          }
          break;
        case 0x50: // Shuffle Button
          lastCommand = activateSiri;
          break;
        case 0x60: // Comp Button
          // Play/Pause Music
          lastCommand = playPause;
          break;
        }

        acp_mode = (acp_mode & 0x40);
        acp_tx[4] = acp_mode;
        acp_chksum_send(5);
        break;

      case 0xC3:
        change_track(true); // Next Track
        acp_tx[4] = BCD(currentTrack);
        acp_chksum_send(5);

        lastCommand = nextTrack;
        break;

      case 0x43:
        change_track(false); // Prev Track
        acp_tx[4] = BCD(currentTrack);
        acp_chksum_send(5);

        lastCommand = prevTrack;
        break;

      default:
        acp_reset(); // unknown - ignore
      }
    } else acp_reset(); // Ignore all other acp messages
  }
}

void acp_chksum_send(unsigned char buffercount) {
  u08 i;
  u08 checksum = 0;
  for (i = 0; i < buffercount; i++) checksum += acp_tx[i];
  acp_txsize = buffercount;
  acp_tx[acp_txsize] = checksum;
  acp_sendmsg();
}
