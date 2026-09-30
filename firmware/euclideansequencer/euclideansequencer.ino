//Euclidean Sequencer
//For Cascadence
//Developed with Modular Seattle for Velocity 2019

//euclid function modified from Tom Whitwell's amazing euclidean sequencer
//https://github.com/TomWhitwell/Euclidean-sequencer
    
#define MAXSTEPLENGTH 31  //+1 because 0 is a step = 32 is the step length
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

const int THRESHOLD = 5;  //dead band for the knobs in ADC counts, as in the ADSR
int lastread[4];  //each knob's reading when it last moved; a move edits only the output the toggle selects, as in the ADSR
int values[2][4];  //the knob readings each output took
const int A = 0;
const int B = 1;
long int euclids[2];
unsigned char stepnumber[2];  //each output counts through its own rhythm, 0 to its length - 1
unsigned char offset_stepnumber[2];
unsigned char seq_length[2];
unsigned char seq_offset[2];
unsigned char seq_density[2];
unsigned char seq_randomness[2];
const unsigned long PULSE_LENGTH = 40000;  //trigger length in microseconds
unsigned long pulsestart[2];  //when each output's pulse started, from micros()
boolean pulsing[2];
void setup()
{
  unsigned char x;

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


  for(x=0;x<4;x++)  //both outputs start from the knobs
  {
    lastread[x]=analogRead(POTS[x]);
    values[A][x]=lastread[x];
    values[B][x]=lastread[x];
  }
  setpattern(A);
  setpattern(B);
  
}

void loop() {
int pulse = false;
boolean clockwashigh = false;
while(1)
{
  boolean clockhigh = digitalRead(CLK_IN) == LOW;  //the input transistor inverts the jack
  if(clockhigh && !clockwashigh)  //we've received a clock pulse!
    {
      //calculate the actual step number for each sequencer
      offset_stepnumber[A]=stepnumber[A]+seq_offset[A];
      if(offset_stepnumber[A]>=seq_length[A])
        offset_stepnumber[A]=offset_stepnumber[A] % seq_length[A];
        
      offset_stepnumber[B]=stepnumber[B]+seq_offset[B];
      if(offset_stepnumber[B]>=seq_length[B])
        offset_stepnumber[B]=offset_stepnumber[B] % seq_length[B];
      
      pulse=false;
      if(bitRead(euclids[A],seq_length[A]-1-offset_stepnumber[A]) == 1) //if there's a pulse (euclid() puts the first step in the highest bit)
        pulse=true;

      if (random(MAXSTEPLENGTH) < seq_randomness[A] )  //or if there's a randomly generated pulse
        pulse = !pulse;
      if(pulse==1)  
        StartPulse(A); //send one

      pulse=false;
      if(bitRead(euclids[B],seq_length[B]-1-offset_stepnumber[B]) == 1) //if there's a pulse
        pulse = true;
      if (random(MAXSTEPLENGTH) < seq_randomness[B] )
        pulse = !pulse;
      if(pulse == 1)
        StartPulse(B); //send one
      
      stepnumber[A]++;
      if(stepnumber[A]>=seq_length[A])
        stepnumber[A] = 0;
      stepnumber[B]++;
      if(stepnumber[B]>=seq_length[B])
        stepnumber[B] = 0;
      
    }
    clockwashigh = clockhigh;
    EndPulses();

    updatevalues(!digitalRead(SW));
}
}

// Euclid calculation function 
//Borrowed lovingly from TomWhitwell
//https://github.com/TomWhitwell/Euclidean-sequencer

uint64_t euclid(int n, int k){ // inputs: n=total, k=beats, o = offset
  int pauses = n-k;
  int pulses = k;
  int per_pulse = pauses/k;
  int remainder = pauses%pulses;  
  long int workbeat[n];
  long int outbeat;
  int workbeat_count=n;
  int a; 
  int b; 
  int trim_count;
  for (a=0;a<n;a++){ // Populate workbeat with unsorted pulses and pauses 
    if (a<pulses){
      workbeat[a] = 1;
    }
    else {
      workbeat [a] = 0;
    }
  }

  if (per_pulse>0 && remainder <2){ // Handle easy cases where there is no or only one remainer  
    for (a=0;a<pulses;a++){
      for (b=workbeat_count-1; b>workbeat_count-per_pulse-1;b--){
        workbeat[a]  = ConcatBin (workbeat[a], workbeat[b]);
      }
      workbeat_count = workbeat_count-per_pulse;
    }

    outbeat = 0; // Concatenate workbeat into outbeat - according to workbeat_count 
    for (a=0;a < workbeat_count;a++){
      outbeat = ConcatBin(outbeat,workbeat[a]);
    }
    return outbeat;
  }

  else { 


    int groupa = pulses;
    int groupb = pauses; 
    int iteration=0;
    if (groupb<=1){
    }
    while(groupb>1){ //main recursive loop


      if (groupa>groupb){ // more Group A than Group B
        int a_remainder = groupa-groupb; // what will be left of groupa once groupB is interleaved 
        trim_count = 0;
        for (a=0; a<groupa-a_remainder;a++){ //count through the matching sets of A, ignoring remaindered
          workbeat[a]  = ConcatBin (workbeat[a], workbeat[workbeat_count-1-a]);
          trim_count++;
        }
        workbeat_count = workbeat_count-trim_count;

        groupa=groupb;
        groupb=a_remainder;
      }


      else if (groupb>groupa){ // More Group B than Group A
        int b_remainder = groupb-groupa; // what will be left of group once group A is interleaved 
        trim_count=0;
        for (a = workbeat_count-1;a>=groupa+b_remainder;a--){ //count from right back through the Bs
          workbeat[workbeat_count-a-1] = ConcatBin (workbeat[workbeat_count-a-1], workbeat[a]);

          trim_count++;
        }
        workbeat_count = workbeat_count-trim_count;
        groupb=b_remainder;
      }




      else if (groupa == groupb){ // groupa = groupb 
        trim_count=0;
        for (a=0;a<groupa;a++){
          workbeat[a] = ConcatBin (workbeat[a],workbeat[workbeat_count-1-a]);
          trim_count++;
        }
        workbeat_count = workbeat_count-trim_count;
        groupb=0;
      }

      else {
        //        Serial.println("ERROR");
      }
      iteration++;
    }


    outbeat = 0; // Concatenate workbeat into outbeat - according to workbeat_count 
    for (a=0;a < workbeat_count;a++){
      outbeat = ConcatBin(outbeat,workbeat[a]);
    }




    return outbeat;

  }
}
void updatevalues(boolean chan)
{
  unsigned char x;
  int reading;
  boolean moved=false;
   for(x=0;x<4;x++)
  {
    reading=analogRead(POTS[x]);
    if(reading<lastread[x]-THRESHOLD || reading>lastread[x]+THRESHOLD)  //ignore smaller changes, so a knob at the edge of a step cannot flicker
    {
      lastread[x]=reading;
      values[chan][x]=reading;  //a turned knob edits only the output the toggle selects; a flip alone changes nothing
      moved=true;
    }
  }
  if(moved)
    setpattern(chan);
}

void setpattern(boolean chan)  //rebuilds an output's rhythm from the knob readings it took
{
  //map(reading,0,1024,lowest,highest+1) gives each value from lowest to highest an equal share of the knob's travel
  seq_length[chan]=map(values[chan][0],0,1024,1,MAXSTEPLENGTH+2);
  seq_density[chan]=map(values[chan][1],0,1024,1,seq_length[chan]+1);
  seq_offset[chan]=map(values[chan][2],0,1024,0,seq_length[chan]);
  seq_randomness[chan]=map(values[chan][3],0,1024,0,MAXSTEPLENGTH+1);

  if(seq_density[chan]>seq_length[chan])
    seq_density[chan]=seq_length[chan];
  euclids[chan]=euclid(seq_length[chan],seq_density[chan]);
}

void StartPulse (boolean chan)  //EndPulses() ends it, so both outputs fire together and no clock is missed
{
  setOutput(chan, GAIN_2, NO_SHTDWN, 0xFFF);
  pulsestart[chan] = micros();
  pulsing[chan] = true;
}

void EndPulses()
{
  for(byte chan=A; chan<=B; chan++)
  {
    if(pulsing[chan] && micros()-pulsestart[chan] >= PULSE_LENGTH)  //unsigned subtraction survives micros() wrapping around
    {
      setOutput(chan, GAIN_2, NO_SHTDWN, 0);
      pulsing[chan] = false;
    }
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

// Function to concatenate two binary numbers bitwise 
long int ConcatBin(uint64_t bina, uint64_t binb){
  int binb_len=findlength(binb);
  long int sum=(bina<<binb_len);
  sum = sum | binb;
  return sum;
}
// Function to find the binary length of a number by counting bitwise 
int findlength(long int bnry){
  boolean lengthfound = false;
  int length=1; // no number can have a length of zero - single 0 has a length of one, but no 1s for the sytem to count
  for (int q=32;q>=0;q--){
    int r=bitRead(bnry,q);
    if(r==1 && lengthfound == false){
      length=q+1;
      lengthfound = true;
    }
  }
  return length;
}
