/*
Revision 1.0 - Main code by Richard Visokey AD7C - www.ad7c.com
Revision 2.0 - November 6th, 2013...  ever so slight revision by  VK8BN for AD9851 chip Feb 24 2014
Revision 3.0 - April, 2016 - AD9851 + ARDUINO PRO NANO + integrate cw decoder (by LZ1DPN) (uncontinued version)
Revision 4.0 - May 31, 2016  - deintegrate cw decoder and add button for band change (by LZ1DPN)
Revision 5.0 - July 20, 2016  - change LCD with OLED display + IF --> ready to control transceiver RFT SEG-100 (by LZ1DPN)
Revision 6.0 - August 16, 2016  - serial control buttons from computer with USB serial (by LZ1DPN) (1 up freq, 2 down freq, 3 step increment change, 4 print state)
									for no_display work with DDS generator
Revision 7.0 - November 30, 2016  - added some things from Ashhar Farhan's Minima TRX sketch to control transceiver, keyer, relays and other ... (LZ1DPN mod)								
Revision 8.0 - December 12, 2016  - EK1A trx end revision. Setup last hardware changes ... (LZ1DPN mod)
Revision 9.0 - January 07, 2017  - EK1A trx last revision. Remove not worked bands ... trx work well on 3.5, 5, 7, 10, 14 MHz (LZ1DPN mod)
Revision 10.0 - March 13, 2017 	 - scan function
Revision 11.0 - June 22, 2017 	 - RIT + other - other

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// Include the library code
#include <SPI.h>
#include <Wire.h>
#include <rotary.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#define OLED_RESET 12
Adafruit_SSD1306 display(OLED_RESET);

//Setup some items
#define CW_TIMEOUT (600l) // in milliseconds, this is the parameter that determines how long the tx will hold between cw key downs
unsigned long cwTimeout = 0;     //keyer var - dead operator control

#define TX_RX (5)   //mute + (+12V) relay - antenna switch relay TX/RX, and +V in TX for PA - RF Amplifier (2 sided 2 possition relay)
#define CW_KEY (4)   // KEY output pin - in Q7 transistor colector (+5V when keyer down for RF signal modulation) (in Minima to enable sidetone generator on)
#define BAND_HI (6)  // relay for RF output LPF  - (0) < 10 MHz , (1) > 10 MHz (see LPF in EK1A schematic)  
#define FBUTTON (A3)  // tuning step freq CHANGE from 1Hz to 1MHz step for single rotary encoder possition
#define ANALOG_KEYER (A1)  // KEYER input - for analog straight key
char inTx = 0;     // trx in transmit mode temp var
char keyDown = 0;   // keyer down temp vat

//AD9851 control
#define W_CLK 8   // Pin 8 - connect to AD9851 module word load clock pin (CLK)
#define FQ_UD 9   // Pin 9 - connect to freq update pin (FQ)
#define DATA 10   // Pin 10 - connect to serial data load pin (DATA)
#define RESET 11  // Pin 11 - connect to reset pin (RST) 

#define BTNDEC (A2)  // BAND CHANGE BUTTON from 1,8 to 29 MHz - 11 bands
#define pulseHigh(pin) {digitalWrite(pin, HIGH); digitalWrite(pin, LOW); }
Rotary r = Rotary(2,3); // sets the pins for rotary encoder uses.  Must be interrupt pins.
  
int_fast32_t rit=400; // RIT +600 Hz
int_fast32_t rx=(7000000 - rit); // Starting frequency of VFO
int_fast32_t rx2=1; // temp variable to hold the updated frequency
int_fast32_t rxif=6000000; // IF freq, will be summed with vfo freq - rx variable, my xtal filter now is made from 6 MHz xtals
int_fast32_t rxRIT=0;
int RITon=0;
int_fast32_t increment = 50; // starting VFO update increment in HZ. tuning step
int buttonstate = 0;   // temp var
String hertz = "50 Hz";
int  hertzPosition = 0;

byte ones,tens,hundreds,thousands,tenthousands,hundredthousands,millions ;  //Placeholders
String freq; // string to hold the frequency
int_fast32_t timepassed = millis(); // int to hold the arduino miilis since startup
int byteRead = 0;
int var_i = 0;

// buttons temp var
int BTNdecodeON = 0;   
int BTNlaststate = 0;
int BTNcheck = 0;
int BTNcheck2 = 0;
int BTNinc = 3; // set number of default band minus 1

void checkCW(){
  pinMode(TX_RX, OUTPUT);
  if (keyDown == 0 && analogRead(ANALOG_KEYER) < 50){
    //switch to transmit mode if we are not already in it
    if (inTx == 0){
      //put the TX_RX line to transmit
      digitalWrite(TX_RX, 1);
    }
    inTx = 1;
    keyDown = 1;
    rxif = rit - rxRIT;  // in tx freq +600Hz and minus +-RIT 
    sendFrequency(rx);
    digitalWrite(CW_KEY, 1); //start the side-tone
  }

  //reset the timer as long as the key is down
  if (keyDown == 1){
     cwTimeout = CW_TIMEOUT + millis();
  }

  //if we have a keyup
  if (keyDown == 1 && analogRead(ANALOG_KEYER) > 150){
    keyDown = 0;
	inTx = 0;    /// NEW
	rxif = 6000000;  /// NEW
	sendFrequency(rx);  /// NEW
    digitalWrite(CW_KEY, 0);  // stop the side-tone
    digitalWrite(TX_RX, 0);
    cwTimeout = millis() + CW_TIMEOUT;
  }

  //if we have keyuup for a longish time while in cw rx mode
  if (inTx == 1 && cwTimeout < millis()){
    //move the radio back to receive
    digitalWrite(TX_RX, 0);
	digitalWrite(CW_KEY, 0);
    inTx = 0;
    rxif = 6000000;
    sendFrequency(rx);
    cwTimeout = 0;
  }
}

// start variable setup

void setup() {

//set up the pins in/out and logic levels
pinMode(TX_RX, OUTPUT);
digitalWrite(TX_RX, LOW);
  
pinMode(FBUTTON, INPUT);  
digitalWrite(FBUTTON, 1);
  
pinMode(CW_KEY, OUTPUT);
digitalWrite(CW_KEY, LOW);
  

// Initialize the Serial port so that we can use it for debugging
  Serial.begin(115200);
  Serial.println("Start VFO ver 11.0");

  // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C address 0x3C (for oled 128x32)
  
  // Show image buffer on the display hardware.
  // Since the buffer is intialized with an Adafruit splashscreen
  // internally, this will display the splashscreen.
  display.display();

  // Clear the buffer.
  display.clearDisplay();
  
	display.clearDisplay();	
	display.setTextSize(2);
	display.setTextColor(WHITE);
	display.setCursor(0,0);
	display.println(rx-rit+1150);
	display.setTextSize(1);
	display.setCursor(0,16);
	display.print("St:");display.print(hertz);
	display.setCursor(64,16);
	display.print("rit:");display.print(rxRIT);
	display.display();
  
  pinMode(BTNDEC,INPUT);		// band change button
  digitalWrite(BTNDEC,HIGH);    // level
  pinMode(A0,INPUT); // Connect to a button that goes to GND on push - rotary encoder push button - for FREQ STEP change
  digitalWrite(A0,HIGH);  //level

  //  rotary
  PCICR |= (1 << PCIE2);
  PCMSK2 |= (1 << PCINT18) | (1 << PCINT19);
  sei();
  
//  next AD9851 communication settings
  pinMode(FQ_UD, OUTPUT);
  pinMode(W_CLK, OUTPUT);
  pinMode(DATA, OUTPUT);
  pinMode(RESET, OUTPUT); 
  pulseHigh(RESET);
  pulseHigh(W_CLK);
  pulseHigh(FQ_UD);  // this pulse enables serial mode on the AD9851 - see datasheet
 
}

///// START LOOP - MAIN LOOP

void loop() {
	checkCW();   // when pres keyer
	checkBTNdecode();  // BAND change
	
// freq change 
  if ((rx != rx2) || (RITon == 1)){
		  showFreq();
      sendFrequency(rx);
      rx2 = rx;
      }

//  step freq change + RIT ON/OFF  
  buttonstate = digitalRead(A0);
  if(buttonstate == LOW) {
        setincrement();        
    };

}	  
/// END of main loop ///
/// ===================================================== END ============================================


/// START EXTERNAL FUNCTIONS

ISR(PCINT2_vect) {
  unsigned char result = r.process();
if (result) {  
	if (RITon==0){
		if (result == DIR_CW){rx=rx+increment;}
		else {rx=rx-increment;}
	}
	if (RITon==1){
		if (result == DIR_CW){
		  rxRIT=rxRIT+50;
		  }
		else {
		  rxRIT=rxRIT-50;
	 	  }
  } 
}
}

// frequency calc from datasheet page 8 = <sys clock> * <frequency tuning word>/2^32
void sendFrequency(double frequency) {  
  int32_t freq = (frequency + rxif + rxRIT) * 4294967296./180000000;  // note 180 MHz clock on 9851. also note slight adjustment of this can be made to correct for frequency error of onboard crystal
  for (int b=0; b<4; b++, freq>>=8) {
    tfr_byte(freq & 0xFF);
  }
  tfr_byte(0x001);   // Final control byte, LSB 1 to enable 6 x xtal multiplier on 9851 set to 0x000 for 9850
  pulseHigh(FQ_UD);  // Done!  Should see output
}

// transfers a byte, a bit at a time, LSB first to the 9851 via serial DATA line
void tfr_byte(byte data){
  for (int i=0; i<8; i++, data>>=1){
    digitalWrite(DATA, data & 0x01);
    pulseHigh(W_CLK);   //after each bit sent, CLK is pulsed high
  }
}

// step increments for rotary encoder button
void setincrement(){
  if(increment == 0){increment = 1; hertz = "1Hz"; hertzPosition=0;RITon=0;} 
  else if(increment == 1){increment = 10; hertz = "10Hz"; hertzPosition=0;RITon=0;}
  else if(increment == 10){increment = 50; hertz = "50Hz"; hertzPosition=0;RITon=0;}
  else if (increment == 50){increment = 100;  hertz = "100Hz"; hertzPosition=0;RITon=0;}
  else if (increment == 100){increment = 500; hertz="500Hz"; hertzPosition=0;RITon=0;}
  else if (increment == 500){increment = 1000; hertz="1Khz"; hertzPosition=0;RITon=0;}
  else if (increment == 1000){increment = 2500; hertz="2.5Khz"; hertzPosition=0;RITon=0;}
  else if (increment == 2500){increment = 5000; hertz="5Khz"; hertzPosition=0;RITon=0;}
  else if (increment == 5000){increment = 10000; hertz="10Khz"; hertzPosition=0;RITon=0;}
  else if (increment == 10000){increment = 100000; hertz="100Khz"; hertzPosition=0;RITon=0;}
  else if (increment == 100000){increment = 1000000; hertz="1Mhz"; hertzPosition=0;RITon=0;} 
  else{increment = 0; hertz = "ritON"; hertzPosition=0; RITon=1;};  
  showFreq();
  delay(250); // Adjust this delay to speed up/slow down the button menu scroll speed.
}

// oled display functions
void showFreq(){
	display.clearDisplay();	
	display.setTextSize(2);
	display.setTextColor(WHITE);
	display.setCursor(0,0);
	display.println(rx-rit+750);
	display.setTextSize(1);
	display.setCursor(0,16);
	display.print("St:");display.print(hertz);
	display.setCursor(64,16);
	display.print("rit:");display.print(rxRIT);
	display.display();
//  delay(250);
}

//  BAND CHANGE !!! band plan - change if need 
void checkBTNdecode(){
  
BTNdecodeON = digitalRead(BTNDEC);
if(BTNdecodeON != BTNlaststate){
    
    if(BTNdecodeON == HIGH){
         delay(150);
         BTNcheck2 = 1;
         BTNinc = BTNinc + 1;
         if(BTNinc > 6){
              BTNinc = 2;
              }
    }
    
    if(BTNdecodeON == LOW){
         BTNcheck2 = 0; 
    }
    
    BTNlaststate = BTNcheck2;
    
    switch (BTNinc) {
          case 1:
            rx=1810000+rit-750;
            break;
          case 2:
            rx=3500000+rit-750;
            break;
          case 3:
            rx=5250000+rit-750;
            break;
          case 4:
            rx=7000000+rit-750;
            break;
          case 5:
            rx=10100000+rit-750;
            break;
          case 6:
            rx=14000000+rit-750;
            break;
          case 7:
            rx=18068000+rit-750;
            break;    
          case 8:
            rx=21000000+rit-750;
            break;    
          case 9:
            rx=24890000+rit-750;
            break;    
          case 10:
            rx=28000000+rit-750;
            break;
          case 11:
            rx=29100000+rit-750;
            break;    	  
          default:             
            break;
        }
    }
//delay(250);
}

//// OK END OF PROGRAM
