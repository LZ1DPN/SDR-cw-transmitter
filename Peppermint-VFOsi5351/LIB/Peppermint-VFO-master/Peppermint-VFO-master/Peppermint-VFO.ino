// The Peppermint Bark BITX40 VFO
// Bruce MacKinnon KC1FSZ 10-April-2017
// See https://www.qrz.com/db/KC1FSZ
//
// This code allows the radio to emulate an FT-817.  I chose this radio 
// because it is fairly basic and the command protocol is well documented
// here: http://www.ka7oei.com/ft817_meow.html.
//
// Set your CAT software to 9600,N,8,1
//
#include "si5351.h"
#include "Wire.h"
#include <SPI.h>
#include <Adafruit_SSD1306.h>
#include <ClickEncoder.h>
#include <TimerOne.h>

#define ENCODER_LEFT 3
#define ENCODER_RIGHT 2
#define ENCODER_PUSH 4
#define FUNCTION_BUTTON 5

// Connection to OLED controller
Adafruit_SSD1306 display(4);
// Connection to PLL synthesizer
Si5351 si5351;
// Connection to rotary encoder
ClickEncoder encoder(ENCODER_LEFT,ENCODER_RIGHT,ENCODER_PUSH,4);

// ----- FREQUENCY STEP MENU ---------------------------------------------------------------

const unsigned int freqStepMenuSize = 7;

const long long freqStepMenu[freqStepMenuSize] = {
  1L,
  10L,
  100L,
  500L,
  1000L,
  10000L,
  100000L
};

const char* freqStepMenuText[freqStepMenuSize] = {
  "1Hz",
  "10Hz",
  "100Hz",
  "500Hz",
  "1kHz",
  "10kHz",
  "100kHz"
};

// This is the current step that is being used
unsigned int freqStepIndex = 3;

// ----- MODES ---------------------------------------------------------------

const unsigned int modeMenuSize = 3;

const char* modeMenuText[] = {
  "VFO",
  "CAL",
  "AGC"
};

// 40m band limitations
const unsigned long minDisplayFreq = 7000000L;
const unsigned long minDisplayFreqA = 7125000L;
const unsigned long maxDisplayFreq = 7300000L;

// The frequency that is displayed 
unsigned long displayFreq = 7200000L;
// The IF frequency 
const unsigned long ifFreq = 12000000L;
// The adjustment (calibration - your number will differ)
unsigned long adjFreq = 1600L;
// Menu mode
unsigned int mode = 0;
unsigned int subMode = 0;
// Indicates that we need to refresh the display
bool displayDirty = true;
unsigned long lastDisplayStamp = 0;
// NOT IMPLEMENTED YET
bool keyed = false;

// Scanning related.
// This controls the mode: 0 means not scanning, +1 means scan up, -1 means scan down
int scanMode = 0;
// This is the last time we made a scan jump
long lastScanStamp = 0;
// This controls how fast we scan
long scanDelayMs = 150;

long lastSerialReadStamp = 0;
byte cmdBuf[5];
int cmdBufPtr = 0;

int vol = 0;
// AGC related
long agc = 0;
// This is the last time we sampled the AGC
long lastAgcStamp = 0;
// This controls how often we check the AGC level
long agcDelayMs = 1000;

// This is used by the hardware timer to manage timing of the rotary 
// encoder.
void serviceCb() {
  encoder.service();
}

void setup() {

  // Diagnostic LED
  pinMode(13,OUTPUT);
  // Function button
  pinMode(FUNCTION_BUTTON,INPUT_PULLUP);
  
  Serial.begin(9600);
 
  si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0);
  si5351.set_pll(SI5351_PLL_FIXED, SI5351_PLLA);  

  // Initialize with the I2C addr (for the 128x64)
  display.begin(SSD1306_SWITCHCAPVCC,0x3C);
  display.clearDisplay();

  Timer1.initialize(1000);
  Timer1.attachInterrupt(serviceCb); 

  // Initial setting
  setFreq(7200000L);

  analogWrite(A14,0);
}

// Draws the frequency line on the display
void updateDisplayFreq(unsigned long freq) {
  unsigned long f = freq;
  unsigned long f0 = f / 1000000;
  unsigned long f1 = (f - (f0 * 1000000)) / 1000;
  unsigned long f2 = f - f0 * 1000000 - f1 * 1000;
  char buf[10];
  display.setCursor(10,20);
  display.print(f0);
  display.setCursor(30,20);
  sprintf(buf,"%03d",f1);
  display.print(buf);
  display.setCursor(70,20);
  sprintf(buf,"%03d",f2);  
  display.print(buf);
}

// This does the rendering of the display
void updateDisplay() {

  display.clearDisplay();
  display.setTextColor(WHITE);

  // Top line - logo and mode
  display.setTextSize(1);
  display.setCursor(0,0);
  display.print("KC1FSZ VFO");

  if (mode == 0) { 

    if (scanMode != 0) {
      display.setCursor(100,0);
      display.print("SCAN");
    } else {
      // Render the mode
      display.setCursor(100,0);
      display.print(modeMenuText[mode]);
    }

    // Frequency line 
    display.setTextSize(2);
    updateDisplayFreq(displayFreq);

    // Second line is the step size
    display.setTextSize(1);
    display.setCursor(5,43);
    display.print(freqStepMenuText[freqStepIndex]);

    // Show the marker
    if (subMode == 0) {
      display.drawPixel(0,20,WHITE);
      display.drawPixel(0,21,WHITE);
      display.drawPixel(0,22,WHITE);
      display.drawPixel(0,23,WHITE);
    } 
    else if (subMode == 1) {
      display.drawPixel(0,43,WHITE);
      display.drawPixel(0,44,WHITE);
      display.drawPixel(0,45,WHITE);
      display.drawPixel(0,46,WHITE);
    }
  }

  else if (mode == 1) {

    // Render the mode
    display.setCursor(100,0);
    display.print(modeMenuText[mode]);

    // Adjustment line
    display.setTextSize(2);
    updateDisplayFreq(adjFreq);    

    // Second line is the step size
    display.setTextSize(1);
    display.setCursor(5,43);
    display.print(freqStepMenuText[freqStepIndex]);

    // Show the marker
    if (subMode == 0) {
      display.drawPixel(0,20,WHITE);
      display.drawPixel(0,21,WHITE);
      display.drawPixel(0,22,WHITE);
      display.drawPixel(0,23,WHITE);
    } else if (subMode == 1) {
      display.drawPixel(0,43,WHITE);
      display.drawPixel(0,44,WHITE);
      display.drawPixel(0,45,WHITE);
      display.drawPixel(0,46,WHITE);
    }
  }

  else if (mode == 2) { 
    // Render the mode
    display.setCursor(100,0);
    display.print(modeMenuText[mode]);
    // Adjustment line
    display.setTextSize(2);
    display.setCursor(10,20);
    display.print(vol);
  }
  
  // Third line (AGC)
  display.setTextSize(1);
  display.setCursor(0,53);
  display.print(agc);

  display.fillRect(12,54,agc * 2.5,5,WHITE);
  
  display.display();
}

// Takes a frequency and limits it to the band
unsigned long limitFreq(unsigned long f) {
  if (f < minDisplayFreq) {
    return minDisplayFreq;
  } else if (f > maxDisplayFreq) {
    return maxDisplayFreq;
  } else {
    return f;
  }
}

// This function sets (and remembers) the VFO frequency
void setFreq(unsigned long freq) {
  displayFreq = freq;
  unsigned long a = ifFreq - (displayFreq + adjFreq);
  si5351.set_freq(a * 100,SI5351_PLL_FIXED, SI5351_CLK0);
  displayDirty = true;  
}

// The next 4 functions are needed to implement the CAT protocol, which
// uses 4-bit BCD formatting.
//
byte setHighNibble(byte b,byte v) {
  // Clear the high nibble
  b &= 0x0f;
  // Set the high nibble
  return b | ((v & 0x0f) << 4);
}

byte setLowNibble(byte b,byte v) {
  // Clear the low nibble
  b &= 0xf0;
  // Set the low nibble
  return b | (v & 0x0f);
}

byte getHighNibble(byte b) {
  return (b >> 4) & 0x0f;
}

byte getLowNibble(byte b) {
  return b & 0x0f;
}

// Takes a number and produces the requested number of decimal digits, staring
// from the least significant digit.  
//
void getDecimalDigits(unsigned long number,byte* result,int digits) {
  for (int i = 0; i < digits; i++) {
    // "Mask off" (in a decimal sense) the LSD and return it
    result[i] = number % 10;
    // "Shift right" (in a decimal sense)
    number /= 10;
  }
}

// Takes a frequency and writes it into the CAT command buffer in BCD form.
//
void writeFreq(unsigned long freq,byte* cmd) {
  // Convert the frequency to a set of decimal digits. We are taking 9 digits
  // so that we can get up to 999 MHz. But the protocol doesn't care about the
  // LSD (1's place), so we ignore that digit.
  byte digits[9];
  getDecimalDigits(freq,digits,9);
  // Start from the LSB and get each nibble 
  cmd[3] = setLowNibble(cmd[3],digits[1]);
  cmd[3] = setHighNibble(cmd[3],digits[2]);
  cmd[2] = setLowNibble(cmd[2],digits[3]);
  cmd[2] = setHighNibble(cmd[2],digits[4]);
  cmd[1] = setLowNibble(cmd[1],digits[5]);
  cmd[1] = setHighNibble(cmd[1],digits[6]);
  cmd[0] = setLowNibble(cmd[0],digits[7]);
  cmd[0] = setHighNibble(cmd[0],digits[8]);  
}

// This function takes a frquency that is encoded using 4 bytes of BCD
// representation and turns it into an long measured in Hz.
//
// [12][34][56][78] = 123.45678? Mhz
//
unsigned long readFreq(byte* cmd) {
    // Pull off each of the digits
    byte d7 = getHighNibble(cmd[0]);
    byte d6 = getLowNibble(cmd[0]);
    byte d5 = getHighNibble(cmd[1]);
    byte d4 = getLowNibble(cmd[1]); 
    byte d3 = getHighNibble(cmd[2]);
    byte d2 = getLowNibble(cmd[2]); 
    byte d1 = getHighNibble(cmd[3]);
    byte d0 = getLowNibble(cmd[3]); 
    return  
      (unsigned long)d7 * 100000000L +
      (unsigned long)d6 * 10000000L +
      (unsigned long)d5 * 1000000L + 
      (unsigned long)d4 * 100000L + 
      (unsigned long)d3 * 10000L + 
      (unsigned long)d2 * 1000L + 
      (unsigned long)d1 * 100L + 
      (unsigned long)d0 * 10L; 
}

// This is where the CAT commands are actually handled
//
void processCATCommand(byte* cmd) {
  // Set frequency
  if (cmd[4] == 0x01) {
    unsigned long freq = readFreq(cmd); 
    setFreq(freq);       
  }
  // Get frequency
  else if (cmd[4] == 0x03) {
    byte resBuf[5];
    // Put the frequency into the buffer
    writeFreq(displayFreq,resBuf);
    // Put the mode into the buffer
    resBuf[4] = 0x00;
    Serial.write(resBuf,5);
  }
  // PTT On
  else if (cmd[4] == 0x08) {
    byte resBuf[0];
    if (!keyed) {
      resBuf[0] = 0;
    } else {
      resBuf[0] = 0xf0;
    }
    Serial.write(resBuf,1);
    keyed = true;
    digitalWrite(13,HIGH);
  }
  // Read TX keyed state
  else if (cmd[4] == 0x10) {
    byte resBuf[0];
    if (!keyed) {
      resBuf[0] = 0;
    } else {
      resBuf[0] = 0xf0;
    }
    Serial.write(resBuf,1);
  }
  // PTT Off
  else if (cmd[4] == 0x88) {
    byte resBuf[0];
    if (keyed) {
      resBuf[0] = 0;
    } else {
      resBuf[0] = 0xf0;
    }
    Serial.write(resBuf,1);
    keyed = false;
    digitalWrite(13,LOW);
  }
  // Read receiver status
  else if (cmd[4] == 0xe7) {
    byte resBuf[0];
    resBuf[0] = 0x09;
    Serial.write(resBuf,1);
  }  
  // Read receiver status
  else if (cmd[4] == 0xf7) {
    byte resBuf[0];
    resBuf[0] = 0x00;
    if (keyed) {
      resBuf[0] = resBuf[0] | 0xf0;
    }
    Serial.write(resBuf,1);
  }
}

void loop() {
  
  // Service serial (CAT) interface
  if (Serial.available()) {
    if ((millis() - lastSerialReadStamp) > 500) {
      cmdBufPtr = 0;
    }
    cmdBuf[cmdBufPtr++] = Serial.read();
    lastSerialReadStamp = millis();
    // Check to see if we have a full read
    if (cmdBufPtr == 5) {
      processCATCommand(cmdBuf);
    }
  }

  // Service the AGC if necessary
  if (millis() > lastAgcStamp + agcDelayMs) {      
    // Read the AGC line
    agc = analogRead(0) / 10;
    // Reset the timer
    lastAgcStamp = millis();
    displayDirty = true;
  }
  
  // TODO: CONSIDER TIMER CONTROL
  // Periodic display update (if needed)
  if (displayDirty && millis() > lastDisplayStamp + 100) {
    lastDisplayStamp = millis();    
    displayDirty = false;
    updateDisplay();
  }

  // Sample the encoder 
  int16_t encoderValue = encoder.getValue();
  ClickEncoder::Button encoderButton = encoder.getButton();
  // Sample the mode button
  int button5 = digitalRead(FUNCTION_BUTTON);

  if (mode == 0) {
    if (subMode == 0) {
      // If the encoder was turned 
      if (encoderValue != 0) {
        unsigned long f = limitFreq(displayFreq -
          (unsigned long long)encoderValue * freqStepMenu[freqStepIndex]);
        setFreq(f);
        // If we are in scan mode then kick out of it immediately
        scanMode = 0;
      }
      // No encoder value 
      else {
        // If the function button is pressed then enter scan mode
        if (button5 == LOW) {
          scanMode = 1;
        }
      }
    } else if (subMode == 1) {
      // Adjust the step
      if (encoderValue != 0) {
        int newIndex = (int)freqStepIndex - encoderValue;
        if (newIndex < 0) {
          newIndex = 0;
        } else if ((unsigned int)newIndex >= freqStepMenuSize) {
          newIndex = freqStepMenuSize - 1;
        }
        freqStepIndex = newIndex;
        displayDirty = true;
      }    
    }
  
    if (encoderButton == 5) {
      if (subMode == 0) {
        subMode = 1;
      } else if (subMode == 1) {
        subMode = 2;
      } else {
        subMode = 0;
      }
      displayDirty = true;
    } else if (encoderButton == 6) {
      mode++;
      if (mode >= modeMenuSize) {
        mode = 0;
      }
      displayDirty = true;
    }
  } 

  else if (mode == 1) {
    
    if (subMode == 0) {
      // Adjust the frequency by the selected step size
      if (encoderValue != 0) {
        adjFreq -= (unsigned long long)encoderValue * freqStepMenu[freqStepIndex];
        setFreq(displayFreq);
      }
    } else if (subMode == 1) {
      if (encoderValue != 0) {
        int newIndex = (int)freqStepIndex - encoderValue;
        if (newIndex < 0) {
          newIndex = 0;
        } else if ((unsigned int)newIndex >= freqStepMenuSize) {
          newIndex = freqStepMenuSize - 1;
        }
        freqStepIndex = newIndex;
        displayDirty = true;
      }    
    }
  
    if (encoderButton == 5) {
      if (subMode == 0) {
        subMode = 1;
      } else {
        subMode = 0;
      }
      displayDirty = true;
    } else if (encoderButton == 6) {
      mode++;
      if (mode >= modeMenuSize) {
        mode = 0;
      }
      displayDirty = true;
    }
  } 
  
  else if (mode == 2) {
    // If the encoder was turned 
    if (encoderValue != 0) {
      vol -= encoderValue;
      analogWrite(A14,vol);
      displayDirty = true;
    }    
    if (encoderButton == 6) {
      mode++;
      if (mode >= modeMenuSize) {
        mode = 0;
      }
      displayDirty = true;
    }
  }
  
  // Handle scanning.  If we are in VFO mode and scanning is enabled and the scan interval
  // has expired then step the VFO frequency.
  //  
  if (mode == 0 &&
      scanMode != 0 && 
      millis() > (lastScanStamp + scanDelayMs)) {
    // Record the time so that we can start another cycle
    lastScanStamp = millis();
    // Bump the frequency by the configured step
    unsigned long f = displayFreq +
      (unsigned long long)scanMode * freqStepMenu[freqStepIndex];
    // Look for wrap-around
    if (f > maxDisplayFreq) {
      f = minDisplayFreqA;
    }
    setFreq(f);
  }
}

