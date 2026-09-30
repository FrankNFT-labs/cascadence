//TuringMachine
//For Cascadence
//Developed with Modular Seattle for Velocity 2019
    
#include <avr/eeprom.h>

#define MAXSEQLENGTH 16  
const int GAIN_1 = 0x1;
const int GAIN_2 = 0x0;
const int NO_SHTDWN = 1;
const int SHTDWN = 0;

//pin definitions
const int POTS[4]={0,1,2,3};

const int CLK_IN = 8;
const int SW = 7;

const int DAC_MOSI = 6;
const int DAC_SCK = 4;
const int PIN_CS = 5;

unsigned int values[4];
const int A = 0;
const int B = 1;
unsigned int sequence = 0;
unsigned char seq_length;
char seq_randomness;
unsigned int seq_scale;
unsigned int seq_shift;
const unsigned long PULSE_LENGTH = 40000;  //trigger length in microseconds
unsigned long pulsestart;  //when the pulse on B started, from micros()
boolean pulsing;

unsigned int maxvalues[]={0,1,3,7,15,31,63,127,255,511,1023,2047,4095,8191,16383,32767,65535};  //max posible values for a given bit length

const long SEMITONE_X3=125;  //three semitones in DAC codes, so the quantizer's arithmetic stays in whole numbers

//for quantizer, 83.33 mV per semitone
//On prototype, max output is 4.096V, so 83 codes per semitone
//On the final release, the output stage doubles the DAC's 4.096 V to 8.192 V,
//so a semitone is 4096 / (8.192 * 12) = 125/3 codes, about 41.67
//
void setup()
{


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


  uint8_t powerups = eeprom_read_byte((const uint8_t *)0);  //counts power-ups, so each one seeds random() differently and plays a new sequence
  eeprom_update_byte((uint8_t *)0, powerups + 1);  //one byte: the EEPROM writes it in the background, so setup() does not wait
  randomSeed(powerups + 1);  //1 to 256, because randomSeed(0) changes nothing

  updatevalues();
  
}

void loop() {
boolean lastbit;
unsigned int outputvalue;
long semitone;
boolean clockwashigh = false;
while(1)
{
  boolean clockhigh = digitalRead(CLK_IN) == LOW;  //the input transistor inverts the jack
  if(clockhigh && !clockwashigh)  //we've received a clock pulse!
    {
      lastbit=bitRead(sequence,(seq_length-1));

      if(random(90)>seq_randomness)  //randomness is between -10 and 100
        lastbit = !lastbit;
        
      sequence = sequence<<1;
      sequence = sequence & 0xFFFE;
      if(lastbit == 1)
        bitSet(sequence,0); 
      
      outputvalue = sequence & maxvalues[seq_length];
      outputvalue = map(outputvalue,0,maxvalues[seq_length],0,seq_scale);
      outputvalue = outputvalue+seq_shift;
      if(outputvalue>4095)  //scale plus offset can reach 6142; the DAC takes 12 bits, and more would wrap
        outputvalue = 4095;

      if(digitalRead(SW)) //Quantizer is on
      {
        semitone = (outputvalue*3L + SEMITONE_X3/2) / SEMITONE_X3;  //the nearest semitone
        outputvalue = (semitone*SEMITONE_X3 + 1) / 3;  //back to DAC codes, rounded to the nearest
      }

      setOutput(A, GAIN_2, NO_SHTDWN, outputvalue);  //the CV first, so it is in place when the pulse starts
      if(lastbit == 1)
        StartPulse();
      
    }
    clockwashigh = clockhigh;
    EndPulse();

    updatevalues();
}
}


void updatevalues(void)
{
  unsigned char x;
   for(x=0;x<4;x++)
  {
    values[x]=analogRead(POTS[x]);  
  }

  seq_length=map(values[1],0,1023,1,MAXSEQLENGTH);
  seq_randomness=map(values[0],0,1023,-10,100);
  seq_scale=map(values[2],0,1023,0,4095);
  seq_shift=map(values[3],0,1023,0,2047);

}

void StartPulse()  //on B; EndPulse() ends it, so the loop never waits
{
  setOutput(B, GAIN_2, NO_SHTDWN, 0xFFF);
  pulsestart = micros();
  pulsing = true;
}

void EndPulse()
{
  if(pulsing && micros()-pulsestart >= PULSE_LENGTH)  //unsigned subtraction survives micros() wrapping around
  {
    setOutput(B, GAIN_2, NO_SHTDWN, 0);
    pulsing = false;
  }
}
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

