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
//#include <rotary.h>
#include <avr/io.h>
#include "Si570.h"
#include "debug.h"
#define RADIONO_VERSION "20m-SSB"
#define SI570_I2C_ADDRESS 0x55
#define CW_TIMEOUT (600l) // in milliseconds, this is the parameter that determines how long the tx will hold between cw key downs

// When RUN_TESTS is 1, the Radiono will automatically do some software testing when it starts.
// Please note, that those are not hardware tests! - Comment this line to save some space.
#define RUN_TESTS 1

unsigned long IF_FREQ = 0L;	
//unsigned long IF_USB = 0L;
unsigned long frequency = 7018000L;
unsigned long vfoA = 7018000L, vfoB=7018000L;
unsigned long TXO = frequency;
unsigned long cwTimeout = 0;
unsigned ritOn = 0;
int byteRead = 0;
int f_change = 0;

Si570 *vfo;
//Rotary r = Rotary(A4,A5); // sets the pins the rotary encoder uses.  Must be interrupt pins.
int count = 0;

/* tuning pot stuff */
unsigned char refreshDisplay = 0;
unsigned int stepSize = 100;
int tuningPosition = 0;
unsigned char locked = 0; //the tuning can be locked: wait until it goes into dead-zone before unlocking it
int_fast32_t increment = 50; // starting VFO update increment in HZ. tuning step
String hertz = "50 Hz";
int  hertzPosition = 0;
int var_i = 0;

/* the digital controls */
#define TX_RX (3)
#define TX_ON (7)
#define FBUTTON (A3)
#define ANALOG_TUNING (A2)
#define VFO_A 0
#define VFO_B 1

char inTx = 0;
char vfoActive = VFO_A;
/* modes */
unsigned char isManual = 1;

/* dds ddschip(DDS9850, 5, 6, 7, 125000000LL); */

void setup(){
  // Initialize the Serial port so that we can use it for debugging
  Serial.begin(115200);

  debug("Radiono starting - Version: %s", RADIONO_VERSION);

#ifdef RUN_TESTS
  run_tests();
#endif

  vfo = new Si570(SI570_I2C_ADDRESS, 56320000);

  if (vfo->status == SI570_ERROR) {
    Serial.println("Si570 comm error");
    delay(10000);
  }

  // This will print some debugging info to the serial console.
  vfo->debugSi570();

  //set the initial frequency
  vfo->setFrequency(26150000L);

  pinMode(TX_RX, OUTPUT);
  digitalWrite(TX_RX, LOW);  // enable relay to RX mode
  
  pinMode(TX_ON, INPUT);
  digitalWrite(TX_ON, LOW);
} 
//end setup

// !!!!! *****  START

void loop(){
//  readTuningPot();
//  checkTuning();
//  checkTX();
//  checkButton();
//	if (frequency >= 4350000){frequency=4350000;}; // UPPER VFO LIMIT
//	if (frequency <= 4000000){frequency=4000000;}; // LOWER VFO LIMIT

if (f_change == 1){
	vfo->setFrequency(frequency);
  f_change=0;
}
  
//        Serial.println(frequency + IF_FREQ);
//        Serial.println(frequency);

		  
///    SERIAL COMMUNICATION - remote computer control for DDS - 1, 2, 3, 4, 5, 6 - worked 
   /*  check if data has been sent from the computer: */
if (Serial.available()) {
    /* read the most recent byte */
    byteRead = Serial.read();
  if(byteRead == 49){     // 1 - up freq
    frequency = frequency + increment;
    Serial.println(frequency + IF_FREQ);
    f_change=1;
    }
  if(byteRead == 50){   // 2 - down freq
    frequency = frequency - increment;
    Serial.println(frequency + IF_FREQ);
    f_change=1;
    }

  if(byteRead == 52){   // 4 - print VFO state in serial console
    Serial.println("VFO_VERSION 20m-SSB");
    Serial.println(frequency + IF_FREQ);
    Serial.println(IF_FREQ);
    Serial.println(10000000);
    Serial.println(increment);
    Serial.println(hertz);
    }
  if(byteRead == 53){   // 5 - scan freq forvard 40kHz 
             var_i=0;           
             while(var_i<=35000){
                var_i++;
                frequency = frequency + 10;
                Serial.println(frequency + IF_FREQ);
                f_change=1;
//                showFreq();
                if (Serial.available()) {
                    if(byteRead == 53){
                      break;                       
                      }
                }
             }        
   }

   if(byteRead == 54){   // 6 - scan freq back 40kHz  
             var_i=0;           
             while(var_i<=35000){
                var_i++;
                frequency = frequency - 10;
//                sendFrequency(rx);
                Serial.println(frequency + IF_FREQ);
                f_change=1;
//                showFreq();
                if (Serial.available()) {
                    if(byteRead == 54){
                        break;                       
                    }
                }
             }        
   }
   if(byteRead == 55){     // 1 - up freq
    TXO = TXO + increment;
//    sendFrequency(rx);
//    Serial.println(rxbfo);
   }
  if(byteRead == 56){   // 2 - down freq
    TXO = TXO - increment;
//    sendFrequency(rx);
//    Serial.println(rxbfo);
  }
}   
//end serial com


}   
// !!!!!! ****** END loop

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
//
  // abort program execution.
  abort();
}
#endif

void checkTuning(){
  if (-50 < tuningPosition && tuningPosition < 50){
    if (locked){
      locked = 0;
    }
    delay(50);
    return;
  }

  //if the tuning is locked and we are outside the safe band, then we don't move the freq.
  if (locked){
    return;
  }
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
//      updateDisplay();
      delay(300);
    }
    else if (tuningPosition < 500){
      frequency += 1000000;
//      updateDisplay();
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
//      updateDisplay();
      delay(300);
    }
    else if (tuningPosition > -500){
      frequency -= 1000000;
//      updateDisplay();
      delay(300);
    }
  }
  delay(50);
  Serial.println(frequency + IF_FREQ);
//  Serial.println(frequency);
}
// end check tuning

void checkTX(){
  if (digitalRead(TX_ON) == 0 && inTx == 0){
    //put the  TX_RX line to transmit
      pinMode(TX_RX, OUTPUT);
      digitalWrite(TX_RX, 1);
      //give the relays a few ms to settle the T/R relays
      delay(50);
//    refreshDisplay++;
    inTx = 1;
  }

  if (digitalRead(TX_ON) == 1 && inTx == 1){
     //put the  TX_RX line to rx
      pinMode(TX_RX, OUTPUT);
      digitalWrite(TX_RX, 0);
      //give the relays a few ms to settle the T/R relays
      delay(50);
//    refreshDisplay++;
    inTx = 0;
  }
}

/// end check tx

int btnDown(){
  if (analogRead(FBUTTON) < 300){
    return 1;
  }
  else{
    return 0;
  }
}
// end btn
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
//    refreshDisplay++;
  }

  while (btnDown() == 1){
     delay(50);
  }
}
// end ...

void readTuningPot(){
    tuningPosition = analogRead(2) - 512;
}
// end read


