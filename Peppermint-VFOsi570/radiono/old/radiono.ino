/*
 * Radiono - The Minima's Main Arduino Sketch
 * Copyright (C) 2013 Ashar Farhan
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of  MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * This sketch contains dpp_mod (Dimitar Pavlov - LZ1DPN) modification from May 2016, 
 * many aspects of source and minima trx schematic are different from original Ashar Farhan source and schematic.
 * 
 */

#define __ASSERT_USE_STDERR
#include <assert.h>

/*
 * Wire is only used from the Si570 module but we need to list it here so that
 * the Arduino environment knows we need it.
 */

#include <Wire.h>
#include <LiquidCrystal.h>
#include <rotary.h>

#include <avr/io.h>
#include "Si570.h"
#include "debug.h"

#define RADIONO_VERSION "0.4mod"

/*
 The 16x2 LCD is connected as follows:
    LCD's PIN   Raduino's PIN  PURPOSE      ATMEGA328's PIN
    4           13             Reset LCD    19
    6           12             Enable       18
    11          10             D4           17
    12          11             D5           16
    13           9             D6           15
    14           8             D7           14
*/

#define SI570_I2C_ADDRESS 0x55
// #define IF_FREQ   (19996400l)  // USB BFO, this is for usb, we should probably have the USB and LSB frequencies separately
#define CW_TIMEOUT (600l) // in milliseconds, this is the parameter that determines how long the tx will hold between cw key downs

// When RUN_TESTS is 1, the Radiono will automatically do some software testing when it starts.
// Please note, that those are not hardware tests! - Comment this line to save some space.
#define RUN_TESTS 1

unsigned long IF_FREQ = 0L;	//default for 7 mhz
unsigned long IF_USB = 0L;
unsigned long IF_LSB = 0L;
unsigned long IF_CW = 0L;  // center freq +100 Hz

unsigned long frequency = 7000000L;   // default freq = 14 mhz
//unsigned long frequency2 = 0L;
unsigned long vfoA=7000000L, vfoB=14000000L, ritA, ritB;
unsigned long cwTimeout = 0;
int byteRead = 0;

Si570 *vfo;
LiquidCrystal lcd(13, 12, 11, 10, 9, 8);
//Rotary r = Rotary(A4,A5); // sets the pins the rotary encoder uses.  Must be interrupt pins.
//#define RBUTTON (A0);
int count = 0;
char b[20], c[20], printBuff[32];

/* tuning pot stuff */
unsigned char refreshDisplay = 0;
unsigned int stepSize = 100;
int tuningPosition = 0;
unsigned char locked = 0; //the tuning can be locked: wait until it goes into dead-zone before unlocking it

/* the digital controls */

#define LSB (2)
#define TX_RX (3)
#define TX_ON (7)
#define CW_KEY (4)
#define BAND_HI (6)
#define FBUTTON (A3)
#define ANALOG_TUNING (A2)
#define ANALOG_KEYER (A1)
#define VFO_A 0
#define VFO_B 1

char inTx = 0;
char keyDown = 0;
char isLSB = 1;
char isRIT = 0;
char vfoActive = VFO_A;
/* modes */
unsigned char isManual = 1;
unsigned ritOn = 0;

/* dds ddschip(DDS9850, 5, 6, 7, 125000000LL); */

/* display routines */
void printLine1(char const *c){
  if (strcmp(c, printBuff)){
    lcd.setCursor(0, 0);
    lcd.print(c);
    strcpy(printBuff, c);
    count++;
  }
}

void printLine2(char const *c){
  lcd.setCursor(0, 1);
  lcd.print(c);
}

void displayFrequency(unsigned long f){
  int mhz, khz, hz;

  mhz = f / 1000000l;
  khz = (f % 1000000l)/1000;
  hz = f % 1000l;
  sprintf(b, "[%02d.%03d.%03d]", mhz, khz, hz);
  printLine1(b);
}

void updateDisplay(){
  char const *vfoStatus[] = { "ERR", "RDY", "BIG", "SML" };

  sprintf(b, "%08ld", frequency);
//    sprintf(c, "%s:%.2s.%.4s %s", vfoActive == VFO_A ? " A" : " B" , b, b+2, ritOn ? " CW" : "SSB");
  sprintf(c, "%s:%.2s.%.4s%s", vfoActive == VFO_A ? " A" : " B" , b, b+2, "00 MHz");
// sprintf(c, "%s:%.2s.%.5s%s", vfoActive == VFO_A ? " A" : " B" , b,  b+2, "0  ");
  printLine1(c);
  sprintf(c, "%s %s %s", isLSB ? "LSB" : "USB", inTx ? " TX" : " RX", vfoStatus[vfo->status]);
  printLine2(c);
}

void setup() {
  // Initialize the Serial port so that we can use it for debugging
  Serial.begin(115200);

  debug("Radiono starting - Version: %s", RADIONO_VERSION);
  lcd.begin(16, 2);

#ifdef RUN_TESTS
  run_tests();
#endif

  printBuff[0] = 0;
  printLine1("Radiono ");
  lcd.print(RADIONO_VERSION);

  // The library automatically reads the factory calibration settings of your Si570
  // but it needs to know for what frequency it was calibrated for.
  // Looks like most HAM Si570 are calibrated for 56.320 Mhz.
  // If yours was calibrated for another frequency, you need to change that here
  vfo = new Si570(SI570_I2C_ADDRESS, 56320000);

  if (vfo->status == SI570_ERROR) {
    // The Si570 is unreachable. Show an error for 3 seconds and continue.
    printLine2("Si570 comm error");
    delay(10000);
  }

  // This will print some debugging info to the serial console.
  vfo->debugSi570();

  //set the initial frequency
  vfo->setFrequency(26150000L);

  //set up the pins
  pinMode(LSB, OUTPUT);  
  digitalWrite(LSB, HIGH);  // enable relay of BFO to LSB mode
  
  pinMode(TX_RX, OUTPUT);
//  digitalWrite(TX_RX, HIGH);
  digitalWrite(TX_RX, LOW);  // enable relay to RX mode
  
  pinMode(TX_ON, INPUT);
//  digitalWrite(TX_ON, HIGH);
  digitalWrite(TX_ON, LOW);

  pinMode(CW_KEY, OUTPUT);
  digitalWrite(CW_KEY, LOW); // enable ready CW KEYER

  //set the side-tone off, put the transceiver to receive mode
//  digitalWrite(CW_KEY, 0);
//  digitalWrite(TX_RX, 1); //old way to enable the built-in pull-ups
//  digitalWrite(TX_RX, HIGH);
//  digitalWrite(TX_RX, 0); //old way to enable the built-in pull-ups
//  digitalWrite(TX_RX, LOW);
//  digitalWrite(FBUTTON, 1);
}

void setSideband(){
  if (frequency >= 10000000L)
  {
    isLSB = 0;
    digitalWrite(LSB, 0);
    IF_FREQ=IF_USB;
  }
  else{
    digitalWrite(LSB, 1);
    isLSB = 1;
    IF_FREQ=IF_LSB;
  }
  if (ritOn == 1) {
//    IF_FREQ=IF_CW;
  }
}

// set band for LPF filter

void setBandswitch(){
  if (frequency >= 15000000L)
  {
    digitalWrite(BAND_HI, 1);
  }
  else {
    digitalWrite(BAND_HI, 0);
  }
}

void readTuningPot(){
    tuningPosition = analogRead(2) - 512;
}

void checkTuning(){

  if (-50 < tuningPosition && tuningPosition < 50){
    //we are in the middle, so, let go of the lock
    if (locked)
      locked = 0;
    delay(50);
    return;
  }

  //if the tuning is locked and we are outside the safe band, then we don't move the freq.
  if (locked)
    return;

  //dead region between -50 and 50
  if (100 < tuningPosition){
    if (tuningPosition < 100)
      frequency += 3;
    else if (tuningPosition < 150)
      frequency += 10;
    else if (tuningPosition < 200)
      frequency += 30;
    else if (tuningPosition < 250)
      frequency += 100;
    else if (tuningPosition < 300)
      frequency += 300;
    else if (tuningPosition < 350)
      frequency += 1000;
    else if (tuningPosition < 400)
      frequency += 3000;
    else if (tuningPosition < 450){
      frequency += 100000;
      updateDisplay();
      delay(300);
    }
    else if (tuningPosition < 500){
      frequency += 1000000;
      updateDisplay();
      delay(300);
    }
  }

  if (-100 > tuningPosition){
    if (tuningPosition > -100)
      frequency -= 3;
    else if (tuningPosition > -150)
      frequency -= 10;
    else if (tuningPosition > -200)
      frequency -= 30;
    else if (tuningPosition > -250)
      frequency -= 100;
    else if (tuningPosition > -300)
      frequency -= 300;
    else if (tuningPosition > -350)
      frequency -= 1000;
    else if (tuningPosition > -400)
      frequency -= 3000;
    else if (tuningPosition > -450){
      frequency -= 100000;
      updateDisplay();
      delay(300);
    }
    else if (tuningPosition > -500){
      frequency -= 1000000;
      updateDisplay();
      delay(300);
    }
  }
  delay(50);
  refreshDisplay++;
//  Serial.println(frequency + IF_FREQ);
  Serial.println(frequency);
}


void checkTX(){

  //we don't check for ptt when transmitting cw
  if (cwTimeout > 0)
    return;

  if (digitalRead(TX_ON) == 0 && inTx == 0){
    //put the  TX_RX line to transmit
      pinMode(TX_RX, OUTPUT);
      digitalWrite(TX_RX, 1);
      //give the relays a few ms to settle the T/R relays
      delay(50);
    refreshDisplay++;
    inTx = 1;
  }

  if (digitalRead(TX_ON) == 1 && inTx == 1){
     //put the  TX_RX line to rx
      pinMode(TX_RX, OUTPUT);
      digitalWrite(TX_RX, 0);
      //give the relays a few ms to settle the T/R relays
      delay(50);
    refreshDisplay++;
    inTx = 0;
  }
}


/*CW is generated by keying the bias of a side-tone oscillator.
nonzero cwTimeout denotes that we are in cw transmit mode.
*/

void checkCW(){

  if (keyDown == 0 && analogRead(ANALOG_KEYER) < 50){
    //switch to transmit mode if we are not already in it
    if (inTx == 0){
      //put the  TX_RX line to transmit
      pinMode(TX_RX, OUTPUT);
      digitalWrite(TX_RX, 1);
      //give the relays a few ms to settle the T/R relays
      delay(50);
    }
    inTx = 1;
    keyDown = 1;
    digitalWrite(CW_KEY, 1); //start the side-tone
    refreshDisplay++;
  }

  //reset the timer as long as the key is down
  if (keyDown == 1){
     cwTimeout = CW_TIMEOUT + millis();
  }

  //if we have a keyup
  if (keyDown == 1 && analogRead(ANALOG_KEYER) > 150){
    keyDown = 0;
    digitalWrite(CW_KEY, 0);
    cwTimeout = millis() + CW_TIMEOUT;
  }

  //if we have keyuup for a longish time while in cw tx mode
  if (inTx == 1 && cwTimeout < millis()){
    //move the radio back to receive
    digitalWrite(TX_RX, 0);
    //set the TX_RX pin back to input mode
//    pinMode(TX_RX, INPUT);
//    digitalWrite(TX_RX, 1); //pull-up!
    inTx = 0;
    cwTimeout = 0;
    refreshDisplay++;
  }
}

int btnDown(){
  if (analogRead(FBUTTON) < 300)
    return 1;
  else
    return 0;
}

void checkButton(){
  int i, t1, t2;
  //only if the button is pressed
  if (!btnDown())
    return;

  //if the btn is down while tuning pot is not centered, then lock the tuning
  //and return
  if (tuningPosition < -50 || tuningPosition > 50){
    if (locked)
      locked = 0;
    else
      locked = 1;
    delay(200);
    return;
  }

  t1 = t2 = i = 0;

  while (t1 < 30 && btnDown() == 1){
    delay(50);
    t1++;
  }

  while (t2 < 10 && btnDown() == 0){
    delay(50);
    t2++;
  }

  //if the press is momentary and there is no secondary press
  if (t1 < 10 && t2 > 6){
    if (ritOn)
      ritOn = 0;
    else
      ritOn = 1;
//    printLine2("RIT on! ");
    refreshDisplay = 1;
  }
  //there has been a double press
  else if (t1 < 10 && t2 <= 6) {
    if (vfoActive == VFO_B){
      vfoActive = VFO_A;
      vfoB = frequency;
      frequency = vfoA;
    }
    else{
      vfoActive = VFO_B;
      vfoA = frequency;
      frequency = vfoB;
    }
     refreshDisplay++;
//     printLine2("VFO swap! ");
  }
  else if (t1 > 10){
//    printLine2("VFOs reset!");
    vfoA= vfoB = frequency;
    refreshDisplay++;
  }

  while (btnDown() == 1){
     delay(50);
  }
}

// !!!!! *****  START

void loop(){
  
  readTuningPot();
  checkTuning();
  
  //the order of testing first for cw and then for ptt is important.
//  checkCW();
//  checkTX();
//  checkButton();
//  setSideband();
//  setBandswitch();

	if (frequency >= 7300000){frequency=7300000;}; // UPPER VFO LIMIT
	if (frequency <= 6998000){frequency=6998000;}; // LOWER VFO LIMIT

	vfo->setFrequency(frequency + IF_FREQ);
//        Serial.println(frequency + IF_FREQ);
//        Serial.println(frequency);

		  
///	  SERIAL COMMUNICATION - remote computer control for DDS - worked but not finishet yet - 1, 2, 3, 4 - worked 
   /*  check if data has been sent from the computer: */
  if (Serial.available()) {
    /* read the most recent byte */
    byteRead = Serial.read();
	if(byteRead == 49){     // 1 - up freq
		frequency = frequency + 100;
    Serial.println(frequency);
		}
	if(byteRead == 50){		// 2 - down freq
		frequency = frequency - 100;
    Serial.println(frequency);
		}
	if(byteRead == 52){		// 4 - print VFO state in serial console
		Serial.println("VFO_VERSION 10.0");
		Serial.println(frequency);
//		Serial.println(rxif);
//		Serial.println(increment);
//		Serial.println(hertz);
		}

	}

/////////////////

    if (refreshDisplay){
    updateDisplay();
    refreshDisplay = 0;
  }
}

// !!!!!! ****** END

#ifdef RUN_TESTS

bool run_tests() {
  /* Those tests check that the Si570 libary is able to understand the
   * register values provided and do the required math with them.
   */
  // Testing for thomas - si570
  {
    uint8_t registers[] = { 0xe1, 0xc2, 0xb5, 0x7c, 0x77, 0x70 };
    vfo = new Si570(registers, 56320000);
    assert(vfo->getFreqXtal() == 114347712);
    delete(vfo);
  }

  // Testing Jerry - si570
  {
    uint8_t registers[] = { 0xe1, 0xc2, 0xb6, 0x36, 0xbf, 0x42 };
    vfo = new Si570(registers, 56320000);
    assert(vfo->getFreqXtal() == 114227856);
    delete(vfo);
  }

  Serial.println("Tests successful!");
  return true;
}

// handle diagnostic informations given by assertion and abort program execution:
void __assert(const char *__func, const char *__file, int __lineno, const char *__sexp) {
  debug("ASSERT FAILED - %s (%s:%i): %s", __func, __file, __lineno, __sexp);
  Serial.flush();
  // Show something on the screen
  lcd.setCursor(0, 0);
  lcd.print("OOPS ");
  lcd.print(__file);
  lcd.setCursor(0, 1);
  lcd.print("Line: ");
  lcd.print(__lineno);
  // abort program execution.
  abort();
}

#endif


/*


//comment out:

//#define IF_FREQ (19994060l) //this is for usb, we should probably have the USB and LSB frequencies separately (orig 199970001)


//add:
#define USB_BFO_FREQ (19998936l)  //your bfo freq when in USB
#define LSB_BFO_FREQ (19993591l)  //your bfo freq when in LSB

//in the void loop routine further down comment out:
// vfo->setFrequency(frequency + IF_FREQ);

//add:

 if (frequency >= 10000000L)
  {
    vfo->setFrequency(frequency + USB_BFO_FREQ);
  }
  else{
    vfo->setFrequency(frequency + LSB_BFO_FREQ);
      }



*/
