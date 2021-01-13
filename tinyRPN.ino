// tinyRPN : simple RPN caluculator on ATtiny85 by @pado3 
// ver.1.0 2021/01/11
// 
// RPN calculator part:
//  thanks to ARC by deetee aka zooxo https://github.com/zooxo/arc/find/master
// one I/O Keypad reader part:
//  thanks to web app by synapse https://synapse.kyoto/calc/ResDiv/page001.html
// sleep mode with PCINTn (not INT0):
//  thanks to 東京お気楽カメラ https://okiraku-camera.tokyo/blog/?p=7567

#include <TinyWireM.h>
#include <avr/sleep.h>
// #include <math.h>  // no need to include

//#define sdaPin 18         // UNO A4 // ATiny85 pin5(fixed)
//#define sclPin 19         // UNO A5 // ATiny85 pin7(fixed)
#define DISPFIX 0         // display FIX (simple but easily overflow) (-466byte)
#define DISPSCI 0         // display scientist mode (reference)
#define DISPENG 1         // display engineer mode (BEST but memory consume +1780byte)
#define NULLCHAR '\0'     // NULL
#define STRLEN 8          // character width of display
#define CLS "        "    // Clear string, SP*STRLEN
#define NEARZERO 1e-37    // if value lower this, use FIX format (SCI & ENG)
#define LCDadr 0x3e       // AQM0802 https://akizukidenshi.com/catalog/g/gK-06795/
#define Vperi 3           // LCD and keypad powersource on 3=pin2
#define AIN A2            // ADC input for keypad on A2=pin3
#define POWER 1           // POWER(sleep) SW on 1=pin6(PCINT1)
#define KeyNum 18         // Number of keys
int thres[KeyNum] = {     // threshold value of ADC derived by synapse's app
  // 18pc, R1=4.7k, E3
    9,   41,   84,  123,  175,  241,  321,  411,  481,  560, 
  642,  720,  791,  849,  896,  932,  974, 1010
};
char KeyName[KeyNum] = "B/*-+#369852.0147_"; // B:BS, #:CHS, _:ENTER 
byte contrast = 10;       // 0-63, tune for Vperi and so on
int prevkey = -1;         // previous keycode, -1:open
unsigned long lastt;      // sleep timer
unsigned long zzz=120000; // sleep in 120sec non-operation
float x=0.0,y=0.0,z=0.0,u=0.0,lastx=0.0;  // stack value in float
boolean stacklift;        // stack lift flag, true with +-*/
char s[STRLEN]="";        // input line characters

void setup() {
  // set Vcc for periferals
  pinMode(Vperi, OUTPUT);
  digitalWrite(Vperi, HIGH);
  
  // set other pins and mode
  pinMode(AIN, INPUT);
  pinMode(POWER, INPUT_PULLUP);
  
  // LCD setup
  TinyWireM.begin();
  delay(100); // wait for Vperi powerup
  lcd_setup();
  lcd_clear();
  // Startup message
//  lcd_setCursor(0, 0);    // omit 2 lines with memory shortage
//  lcd_printStr("tinyRPN");
  lcd_setCursor(0, 1);
  lcd_printStr("RESET");
  delay(1000);
  printstack();
}

void loop() {
  int i;
  char key = KeyName[keypad()];
  // calculation block is excerpted from ARC
  if(('0'<=key)&&(key<='9')||(key=='.')) {
    if(strlen(s)<STRLEN) { // concatenate input character to string
      strcat(s," ");
      s[strlen(s)-1]=key;
    }
    displaystring();
  } else if((strlen(s)>0)&&(key=='B')) { // backspace was pressed
    s[strlen(s)-1]=NULLCHAR; 
    displaystring();
  } else if((strlen(s)>0)&&(key=='#')) { // CHS(+/-)
    if(s[0]=='-') {
      for(i=0;i<strlen(s);i++) s[i]=s[i+1];
    } else {
      strcat(s," ");
      for(i=strlen(s)-1;i>0;i--) s[i]=s[i-1];
      s[0]='-';
    }
    displaystring();
  } else { // non-digit entered
    if(strlen(s)>0) { // process input string with or without stacklift
      if(stacklift) {
        push();
        lastx=x;
        x=atof(s);
      } else {
        lastx=x;
        x=atof(s);
      }
    }
    switch(key) { // operation demanded
      case '_': // ENTER
        push();
        stacklift=false;
        break;
      case '+':
        lastx=x;
        x=x+y;
        y=z;
        z=u;
        stacklift=true;
        break; // operation
      case '-':
        lastx=x;
        x=y-x;
        y=z;
        z=u;
        stacklift=true;
        break;
      case '*':
        lastx=x;
        x=x*y;
        y=z;
        z=u;
        stacklift=true;
        break;
      case '/':
        lastx=x;
        x=y/x;
        y=z;
        z=u;
        stacklift=true;
        break;
      case '#': // change prefix of register x
        lastx=x;
        x=-x;
        break;
    }
    s[0]=NULLCHAR;
    printstack();
  }
}

void push() { // push stack
  u=z;
  z=y;
  y=x;
}

void pop() { // pull stack
  x=y;
  y=z;
  z=u;
}

// display part
void displaystring() {  // write input string to outchannel
  lcd_setCursor(0, 1);
  lcd_printStr(CLS);
  lcd_setCursor(0, 1);
  lcd_printStr(s);
}

void printstack() {     // print stack
  lcd_clear();
  delay(100);
  displaystack(0, y);
  displaystack(1, x);
}

void displaystack(int r, float v) { // display stack on row r
  char st[STRLEN+1];
#if DISPFIX   // display FIX (simple but easily overflow)
  dtostrf(v, 8, 6, st);
#elif DISPSCI // display scientist mode
  if((NEARZERO<abs(v) && abs(v)<1e-3) || 1e6<abs(v)) {
    if(v<0) {
      dtostre(v, st, 1, DTOSTR_ALWAYS_SIGN | DTOSTR_UPPERCASE);
    } else {
      dtostre(v, st, 2, DTOSTR_UPPERCASE);
    }
  } else {  // FIX
    dtostrf(v, 8, 6, st);
  }
#elif DISPENG // display engineer mode
  char mantissa[STRLEN+1];
  float fm;   // float mantissa
  int im, ii; // int mantissa index, int index (im*10^ii)
  ii = 3*(int)(log10(abs(v))/3);
//  fm = v/pow(10.0, (float)ii);  // pow => loop save 486byte!
  int i; float p=1;
  for(i=0; i<abs(ii); i++)  p=p*10;
  if (0<=ii) {
    fm = v/p;
  } else {
    fm = v*p;
  }
  if (-0.999<fm && fm<.999) {  // fm<1 and consider rounding error
    fm *= 1e3;
    ii -= 3;
  }
  // end the workaround for pow()
  im = (int)(log10(abs(fm)));
  dtostrf(fm, 5, 3-im, mantissa);
  if(6<=ii) {         // large ENG, save memory
    sprintf(st, "%.5sE%02d", mantissa, ii);
//  } else if(NEARZERO<abs(v) && abs(v)<1e-2) {  // small ENG - memory over
  } else if(NEARZERO<abs(v) && ii<-3) {  // small ENG, save memory but inflexible
    sprintf(st, "%.4sE%03d", mantissa, ii);
  } else {                  // FIX
    dtostrf(v, 8, 6, st);
  }
#endif
  lcd_setCursor(0, r);
  lcd_printStr(st);
}

// return pushed keypad number
int keypad() {
  int d, i, k;
  lastt = millis();
  do {
    delay(100);  // prevent chattering
    d = analogRead(AIN);
    k = -1;
    for(i=0; i < KeyNum; i++) {
      if(d < thres[i]) {  // hit for maximum threshold
        k = i;
        break;
      }
    }
    if(k == -1) prevkey = -1;
    if (zzz < millis() - lastt || digitalRead(POWER) == LOW) {
      powerdown();
    }
  } while (k == prevkey);
  prevkey = k;
  return k;
}

// sleep mode : power down with SW or timer
ISR (PCINT0_vect) { } // good luck charm - omajinai
void powerdown() {  // Disable BOD with bootloader burn
  lcd_clear();      // prevent noise
  delay(500);       // wait for key up
  digitalWrite(Vperi, LOW);   // peripheral power off
  GIMSK = 0x20;     // enable PCINT, do not allow INT0
  PCMSK = 0x02;     // pin change mask, set interrupt pin to PCINT1
  ADCSRA = 0x00;    // stop ADC (save 330uA)
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_mode();     // sleep_(enable + cpu + disable)
  GIMSK = 0x00;     // disable PCINT
  ADCSRA = 0x80;    // restart ADC
  lastt = millis(); // reset timer
  digitalWrite(Vperi, HIGH);  // peripheral power on
  delay(100);       // wait for power up
  lcd_setup();      // lcd wakeup
  analogRead(AIN);  // clear input charge of ADC
  s[0]=NULLCHAR;    // clear input line buffer
  printstack();     // restore stack display
}

// misc functions for LCD
void lcd_setup() {      // initialize
  lcd_cmd(0b00111000);  // function set
  lcd_cmd(0b00111001);  // function set
  lcd_cmd(0b00000100);  // EntryModeSet
  lcd_cmd(0b00010100);  // interval osc
  lcd_cmd(0b01110000 | (contrast & 0xF)); // contrast Low
  lcd_cmd(0b01011100 | ((contrast >> 4) & 0x3));  // contast High/icon/power
  lcd_cmd(0b01101100);  // follower control
  delay(200);
  lcd_cmd(0b00111000);  // function set
  lcd_cmd(0b00001100);  // Display On
  lcd_cmd(0b00000001);  // Clear Display
}

void lcd_cmd(byte x) {  // send command
  TinyWireM.beginTransmission(LCDadr);
  TinyWireM.send(0b00000000); // CO = 0, RS = 0
  TinyWireM.send(x);
  TinyWireM.endTransmission();
}

void lcd_clear() {      // clear display
  lcd_cmd(0b00000001);
}

void lcd_setCursor(byte x, byte y) {  // 表示位置指定
  lcd_cmd(0x80 | (y * 0x40 + x));
}

void lcd_contdata(byte x) { // use in printStr
  TinyWireM.send(0b11000000); // CO = 1, RS = 1
  TinyWireM.send(x);
}

void lcd_lastdata(byte x) { // use in printStr
  TinyWireM.send(0b01000000); // CO = 0, RS = 1
  TinyWireM.send(x);
}

void lcd_printStr(const char *s) {  // 文字の表示
  TinyWireM.beginTransmission(LCDadr);
  while (*s) {
    if (*(s + 1)) {
      lcd_contdata(*s);
    } else {
      lcd_lastdata(*s);
    }
    s++;
  }
  TinyWireM.endTransmission();
}
// ここまでLCD関係の関数

/* end of program */
