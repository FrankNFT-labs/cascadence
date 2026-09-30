
// Cascadence ADSR
// Based on
// ADSRduino
// https://github.com/m0xpd/ADSRduino
// a simple ADSR for the Arduino
// m0xpd
// Feb 2017
//
// see http://m0xpd.blogspot.co.uk/2017/02/signal-processing-on-arduino.html
//
//
//
// https://github.com/cctvfm/cascadence/




const int GAIN_1 = 0x1;
const int GAIN_2 = 0x0;
const int NO_SHTDWN = 1;
const int SHTDWN = 0;

//pin definitions
const int POTS[4]={0,1,2,3};
const int POT_COUNT = sizeof(POTS)/sizeof(POTS[0]);
const int gatePin = 8;
const int CLK_IN = 8;
const int SW = 7;

const int DAC_MOSI = 6;
const int DAC_SCK = 4;
const int PIN_CS = 5;

const int A = 0;
const int B = 1;

int lastanalogread[POT_COUNT];   // signed like analogRead(), so subtracting THRESHOLD near zero goes negative instead of wrapping

float alpha[2]={0.0,0.0};   // the pole location ('time constant') of the first-order difference equation, set at the start of each phase
double alpha1[2]={0.9,0.9};  // initial value for attack
double alpha2[2]={0.9,0.9};  // initial value for decay
double alpha3[2]={0.95,0.95}; // initial value for release

float envelope[2] = {0.0,0.0};  // initialise the envelope

int drive[2] = {0,0};
int sustain_Level[2] = {0,0};
int scan = 0;
boolean note_active[2] = {false,false};
boolean decay[2] = {false,false};


void update_params(int scan, boolean chan);
void checkforchange(int scan);

// subroutine to set DAC on MCP4802
void setOutput(byte channel, byte gain, byte shutdown, unsigned int val)
{
  byte lowByte = val & 0xff;
  byte highByte = ((val >> 8) & 0xff) | channel << 7 | gain << 5 | shutdown << 4;
  //bit level stuff to make the dac work.
  //channel can be 0 or 1 (A or B)
  //gain can be 0 or 1 (x1 or x2 corresponding to 2V max or 4V max)
  //shutdown is to disable the DAC output, 0 for shutdown, 1 for no shutdown
   
  digitalWrite(PIN_CS, LOW);
  shiftOut(DAC_MOSI,DAC_SCK,MSBFIRST,highByte);
  shiftOut(DAC_MOSI,DAC_SCK,MSBFIRST,lowByte);
  digitalWrite(PIN_CS, HIGH);
}
void setup() {
  
unsigned int x;

  pinMode(POTS[0], INPUT);
  pinMode(POTS[1], INPUT);
  pinMode(POTS[2], INPUT);
  pinMode(POTS[3], INPUT);
  pinMode(SW, INPUT);
  pinMode(CLK_IN,INPUT_PULLUP);


//mosi, sck, and pin_cs used for spi dac (mcp4822)
  pinMode(DAC_MOSI,OUTPUT);
  pinMode(DAC_SCK,OUTPUT);
  
  
  digitalWrite(PIN_CS,HIGH);
  pinMode(PIN_CS, OUTPUT);

  for(x=0;x<POT_COUNT;x++)
  {
    update_params(x,A); //initialize CVs
    update_params(x,B); //initialize CVs

  }

  setOutput(A, GAIN_2, NO_SHTDWN, 0);
  setOutput(B, GAIN_2, NO_SHTDWN, 0);


}

void loop() {
    boolean gate=!digitalRead(gatePin);        // read the gate input every time through the loop
    checkforchange(scan);                     // scan only one of the other inputs each pass 
    
    while(gate){                              // run the ADSR while there is a gate
      if(note_active[A]==false){                   // if a note isn't active and we're triggered, then start one!
      decay[A] = false;
      drive[A]=4096;                               // drive toward full value
      alpha[A]=alpha1[A];                             // set 'time constant' alpha1 for attack phase 
      note_active[A]=true;                         // set the note_active flag
      } 
      if(note_active[B]==false){                   // if a note isn't active and we're triggered, then start one!
      decay[B] = false;
      drive[B]=4096;                               // drive toward full value
      alpha[B]=alpha1[B];                             // set 'time constant' alpha1 for attack phase 
      note_active[B]=true;                         // set the note_active flag
      }
      
      if((decay[A]==false)&&(envelope[A]>4000)){    // if we've reached envelope >4000 during the attack, we must be at the end of attack phase
                                                          // so switch to decay...
        decay[A] = true;                                         // set decay flag
        drive[A]=sustain_Level[A];                                  // drive toward sustain level
        alpha[A]=alpha2[A];                                         // and set 'time constant' alpha2 for decay phase
      } 
      if((decay[B]==false)&&(envelope[B]>4000)){    // if we've reached envelope >4000 during the attack, we must be at the end of attack phase
                                                          // so switch to decay...
        decay[B] = true;                                         // set decay flag
        drive[B]=sustain_Level[B];                                  // drive toward sustain level
        alpha[B]=alpha2[B];                                         // and set 'time constant' alpha2 for decay phase
      }
  
      envelope[A]=((1.0-alpha[A])*drive[A]+alpha[A]*envelope[A]);     // implement difference equation: y(k) = (1 - alpha) * x(k) + alpha * y(k-1)
      envelope[B]=((1.0-alpha[B])*drive[B]+alpha[B]*envelope[B]);
      setOutput(A, GAIN_2, NO_SHTDWN, (round(envelope[A])));                   // and output the envelope to the DAC
      setOutput(B, GAIN_2, NO_SHTDWN, (round(envelope[B]))); 
     

    gate=!digitalRead(gatePin);                      // read the gate pin (remember we're in the while loop)
    }
    
    if(note_active[A]==true){                // this is the start of the release phase
      drive[A]=0;                              // drive towards zero; the release uses alpha3 directly below, so the release knob stays live
      note_active[A]=false;                    // turn off note_active flag
    }   
    if(note_active[B]==true){                // this is the start of the release phase
      drive[B]=0;                              // drive towards zero; the release uses alpha3 directly below, so the release knob stays live
      note_active[B]=false;                    // turn off note_active flag
    }   
  
    envelope[A]=((1.0-alpha3[A])*drive[A]+alpha3[A]*envelope[A]);   // implement the difference equation again (outside the while loop)
    setOutput(A, GAIN_2, NO_SHTDWN, (round(envelope[A])));                    // and output envelope
    envelope[B]=((1.0-alpha3[B])*drive[B]+alpha3[B]*envelope[B]);   // implement the difference equation again (outside the while loop)
    setOutput(B, GAIN_2, NO_SHTDWN, (round(envelope[B])));                    // and output envelope
    scan+=1;                                         // prepare to look at a new parameter input
    if(scan==POT_COUNT){                             // wrap after the last pot (ADSRduino's fifth slot read a loop-mode switch this board does not have)
      scan=0;
    }
}

void checkforchange (int scan)
{
  int reading;   // signed, like lastanalogread
  #define THRESHOLD 5
  
    reading=analogRead(scan);
    if(reading<(lastanalogread[scan]-THRESHOLD) || reading>(lastanalogread[scan]+THRESHOLD))
    {
      //knob has been moved while we're on this channel, update it.
      update_params(scan,!digitalRead(SW));
      lastanalogread[scan]=reading;
      
    }
  
  
}

void update_params(int scan, boolean chan){             // read the input parameters
  float reading;                                        // the pot reading: an int, kept as float so the divisions below are not integer divisions
  switch (scan){
  case 0:
  reading=analogRead(0);                      // get the attack pole location
  alpha1[chan]=0.999*cos((1023-reading)/795);
  alpha1[chan]=sqrt(alpha1[chan]);  
  break;
  case 1:
  reading=analogRead(1);                      // get the decay pole location
  alpha2[chan]=0.999*cos((1023-reading)/795);
  alpha2[chan]=sqrt(alpha2[chan]);   
  break; 
  case 2:
  sustain_Level[chan]=analogRead(2)<<2;                     // get the (integer) sustain level
  break;
  case 3:
  reading=analogRead(3);                     // get the release pole location (potentially closer to 1.0)
  alpha3[chan]=0.99999*cos((1023-reading)/795);
  alpha3[chan]=sqrt(alpha3[chan]);
  break;  
 
  
  }
  
}
