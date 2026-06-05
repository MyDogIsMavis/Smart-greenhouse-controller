#include <EEPROM.h>
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <RTClib.h>

// ===== TFT =====
#define TFT_CS   8
#define TFT_DC   9
#define TFT_RST  -1   // через ICSP RESET

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// ===== BUTTONS =====
#define BTN_MODE 4
#define BTN_NEXT 5
#define BTN_PLUS 6
#define BTN_MINUS 7

#define RELAY_PIN 2
#define ONE_WIRE_BUS 3
#define LDR_PIN A1
#define LIGHT_RELAY_PIN 10

struct Button {
  uint8_t pin;

  bool state;
  bool prevState;
  bool lastReading;

  unsigned long lastDebounce;

  bool lock;

  // hold repeat
  unsigned long holdStart;
  unsigned long lastRepeat;
};

Button btnMode  = {BTN_MODE, HIGH, HIGH, HIGH, 0, false, 0, 0};
Button btnNext  = {BTN_NEXT, HIGH, HIGH, HIGH, 0, false, 0, 0};
Button btnPlus  = {BTN_PLUS, HIGH, HIGH, HIGH, 0, false, 0, 0};
Button btnMinus = {BTN_MINUS, HIGH, HIGH, HIGH, 0, false, 0, 0};

void updateButton(Button &b) {
  bool r = digitalRead(b.pin);
  if (r != b.lastReading) b.lastDebounce = millis();
  if (millis() - b.lastDebounce > 30) {
    b.prevState = b.state;
    b.state = r;
  }
  b.lastReading = r;
}

bool pressed(Button &b) {

  if (b.prevState == HIGH &&
      b.state == LOW &&
      !b.lock) {

    b.lock = true;
    return true;
  }

  if (b.state == HIGH)
    b.lock = false;

  return false;
}
bool repeatPressed(Button &b){

  // обычное нажатие
  if(pressed(b)){

    b.holdStart = millis();
    b.lastRepeat = millis();

    return true;
  }

  // удержание
  if(b.state == LOW){

    // ждём 1.5 секунды
    if(millis() - b.holdStart > 1500){

      // повтор каждые 120 мс
      if(millis() - b.lastRepeat > 120){

        b.lastRepeat = millis();

        return true;
      }
    }
  }

  return false;
}

// ===== SENSORS =====
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
RTC_DS3231 rtc;

// SETTINGS
float dayOn=22, dayOff=24;
float nightOn=20, nightOff=22;
int dayStartH=8;
int dayStartM=0;
int dayEndH=22;
int dayEndM=0;
int dayStartH_e;
int dayStartM_e;
int dayEndH_e;
int dayEndM_e;


struct Settings {

  uint16_t magic;

  float dayOn;
  float dayOff;

  float nightOn;
  float nightOff;

  int dayStartH;
  int dayStartM;

  int dayEndH;
  int dayEndM;

  bool sensorEnabled;
};

Settings settings;

// LIGHT
bool sensorEnabled=true;
bool lightState=false;


void loadSettings(){

  EEPROM.get(0, settings);

  // защита от мусора EEPROM
 if(settings.magic != 0xBEEF){

    settings.magic = 0xBEEF;

    settings.dayOn = 8;
    settings.dayOff = 10;

    settings.nightOn = 8;
    settings.nightOff = 10;

    settings.dayStartH = 8;
    settings.dayStartM = 1;

    settings.dayEndH = 22;
    settings.dayEndM = 1;

    settings.sensorEnabled = true;

    EEPROM.put(0, settings);
}

  // перенос в рабочие переменные
  dayOn = settings.dayOn;
  dayOff = settings.dayOff;

  nightOn = settings.nightOn;
  nightOff = settings.nightOff;

  dayStartH = settings.dayStartH;
  dayStartM = settings.dayStartM;

  dayEndH = settings.dayEndH;
  dayEndM = settings.dayEndM;

  sensorEnabled = settings.sensorEnabled;
}
void saveSettings(){

  settings.magic = 0xBEEF;
  settings.dayOn = dayOn;
  settings.dayOff = dayOff;

  settings.nightOn = nightOn;
  settings.nightOff = nightOff;

  settings.dayStartH = dayStartH;
  settings.dayStartM = dayStartM;

  settings.dayEndH = dayEndH;
  settings.dayEndM = dayEndM;

  settings.sensorEnabled = sensorEnabled;

  EEPROM.put(0, settings);
}


// EDIT
float dOn_e, dOff_e, nOn_e, nOff_e;


// STATE
float temp=0;
bool relay=false;

int screen=0;
bool editMode=false;
int editField=0;

// TIMERS
unsigned long sensorTimer=0;
unsigned long screenTimer=0;

// HOLD
unsigned long bothTime=0;
bool bothHold=false;

String prev[8];

String two(int v){return (v<10?"0":"")+String(v);}

void draw(int i,String t,int s,uint16_t c){
  if(prev[i]!=t){
    prev[i]=t;
    int y=10+i*18;
    tft.fillRect(0,y,128,18,ST77XX_WHITE);
    tft.setTextSize(s);
    tft.setTextColor(c);

    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(t, 0, y, &x1, &y1, &w, &h);
    int x = (128 - w) / 2;

    tft.setCursor(x, y);
    tft.print(t);
  }
}

void clearAll(){
  for(int i=0;i<8;i++) prev[i]="";
  tft.fillScreen(ST77XX_WHITE);
}

// ===== SETUP =====
void setup(){

  // ===== SPI FIX =====
  // D10 обязательно должен быть OUTPUT
  // иначе SPI может сломаться
  pinMode(10, OUTPUT);

  // выключаем реле света
  digitalWrite(10, HIGH);

  delay(50);

  // ===== TFT =====
  tft.initR(INITR_BLACKTAB);
  tft.fillScreen(ST77XX_WHITE);

  // тест
  tft.setTextColor(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10,20);
  tft.print("BOOT");

  // ===== SENSORS =====
  sensors.begin();
  loadSettings();

  // ===== RTC =====
  rtc.begin();
  //rtc.adjust(DateTime(F(__DATE__),F(__TIME__)));

  // ===== HEAT RELAY =====
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  // ===== LIGHT RELAY =====
  pinMode(LIGHT_RELAY_PIN, OUTPUT);
  digitalWrite(LIGHT_RELAY_PIN, HIGH);

  // ===== BUTTONS =====
  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_PLUS, INPUT_PULLUP);
  pinMode(BTN_MINUS, INPUT_PULLUP);

  delay(500);

  clearAll();

}
// ===== LOOP =====
void loop(){

  DateTime now=rtc.now();

  updateButton(btnMode);
  updateButton(btnNext);
  updateButton(btnPlus);
  updateButton(btnMinus);

  if(millis()-sensorTimer>2000){
    sensors.requestTemperatures();
    temp=sensors.getTempCByIndex(0);
    sensorTimer=millis();
  }

int nowMinutes =
    now.hour() * 60 +
    now.minute();

int startMinutes =
    dayStartH * 60 +
    dayStartM;

int endMinutes =
    dayEndH * 60 +
    dayEndM;

bool isDay;

if(startMinutes < endMinutes){

  // обычный режим
  // например 08:00 -> 22:00
  isDay =
    nowMinutes >= startMinutes &&
    nowMinutes < endMinutes;

} else {

  // диапазон через полночь
  // например 22:00 -> 08:00
  isDay =
    nowMinutes >= startMinutes ||
    nowMinutes < endMinutes;
}

  float tOn=isDay?dayOn:nightOn;
  float tOff=isDay?dayOff:nightOff;

  if(temp==-127){
    digitalWrite(RELAY_PIN,HIGH);
    relay=false;
  } else {
    if(temp<=tOn && !relay){digitalWrite(RELAY_PIN,LOW);relay=true;}
    if(temp>=tOff && relay){digitalWrite(RELAY_PIN,HIGH);relay=false;}
  }

  if(sensorEnabled){
    
    int lightThreshold=500;//задаем порог включения света

    lightState=(analogRead(LDR_PIN)<lightThreshold);
  }
//реле света
digitalWrite(
  LIGHT_RELAY_PIN,
  lightState ? LOW : HIGH
); 

  if(pressed(btnNext)){
    if(!editMode){
      editMode=true;
      editField=0;
      dOn_e=dayOn; dOff_e=dayOff;
      nOn_e=nightOn; nOff_e=nightOff;
      dayStartH_e = dayStartH;
dayStartM_e = dayStartM;
dayEndH_e = dayEndH;
dayEndM_e = dayEndM;
    } else {
editField++;

if(screen==1){
  if(editField>3) editField=0;
} else {
  if(editField>1) editField=0;
}
    }
    clearAll();
  }

  if(btnPlus.state==LOW && btnMinus.state==LOW){
    if(!bothHold){
      bothHold=true;
      bothTime=millis();
    }
    if(millis()-bothTime>3000 && editMode){
      dayOn=dOn_e; dayOff=dOff_e;
      nightOn=nOn_e; nightOff=nOff_e;
      dayStartH = dayStartH_e;
dayStartM = dayStartM_e;

dayEndH = dayEndH_e;
dayEndM = dayEndM_e;
saveSettings();
      editMode=false;
      clearAll();
      bothHold=false;
    }
  } else {
    bothHold=false;
  }

if(pressed(btnMode)){

  // CANCEL edit mode
  if(editMode){

    editMode = false;
    clearAll();
  }
  else{

    screen++;

    if(screen>4)
      screen=0;

    clearAll();
  }
}

  if(editMode){

if(screen==1){

if(repeatPressed(btnPlus)){

  if(editField==0 && dayStartH_e<23)
    dayStartH_e++;

  if(editField==1 && dayStartM_e<59)
    dayStartM_e++;

  if(editField==2 && dayEndH_e<23)
    dayEndH_e++;

  if(editField==3 && dayEndM_e<59)
    dayEndM_e++;
}

  if(repeatPressed(btnMinus)){

  if(editField==0 && dayStartH_e>0)
    dayStartH_e--;

  if(editField==1 && dayStartM_e>0)
    dayStartM_e--;

  if(editField==2 && dayEndH_e>0)
    dayEndH_e--;

  if(editField==3 && dayEndM_e>0)
    dayEndM_e--;
}
}

    if(screen==2){
      if(repeatPressed(btnPlus)){
        if(editField==0 && dOn_e+1<dOff_e) dOn_e++;
        if(editField==1) dOff_e++;
      }
      if(repeatPressed(btnMinus)){
        if(editField==0) dOn_e--;
        if(editField==1 && dOff_e-1>dOn_e) dOff_e--;
      }
    }

    if(screen==3){
      if(repeatPressed(btnPlus)){
        if(editField==0 && nOn_e+1<nOff_e) nOn_e++;
        if(editField==1) nOff_e++;
      }
      if(repeatPressed(btnMinus)){
        if(editField==0) nOn_e--;
        if(editField==1 && nOff_e-1>nOn_e) nOff_e--;
      }
    }

    if(screen==4){
      if(editField==0){
        if(repeatPressed(btnPlus) && !sensorEnabled){
    sensorEnabled=true;
    saveSettings();
}
if(repeatPressed(btnMinus) && sensorEnabled){
    sensorEnabled=false;
    lightState=false;
    saveSettings();
}
      }

      if(!sensorEnabled && editField==1){
        if(repeatPressed(btnPlus) && !lightState) lightState=true;
        if(repeatPressed(btnMinus) && lightState) lightState=false;
      }
    }
  }

  if(millis()-screenTimer>200){
    screenTimer=millis();

    if(screen==0){
      draw(0,two(now.hour())+":"+two(now.minute()),2,ST77XX_BLACK);
      draw(1,two(now.day())+"."+two(now.month()),2,ST77XX_BLACK);
      draw(3,String(temp,1)+"C",2,ST77XX_BLUE);
      draw(5,isDay?"DAY":"NIGHT",1,ST77XX_BLACK);
      draw(6,relay?"HEAT ON":"HEAT OFF",1,ST77XX_BLACK);
      draw(7,
        String("Sens: ")+(sensorEnabled?"ON":"OFF")+
        " Light: "+(lightState?"ON":"OFF"),
        1,ST77XX_BLACK);
    }

if(screen==1){

  draw(0,"Day&Night",2,ST77XX_RED);

String s1;
String s2;

if(editMode){

  s1 =
    "Start: " +
    two(dayStartH_e) +
    ":" +
    two(dayStartM_e);

  s2 =
    "End: " +
    two(dayEndH_e) +
    ":" +
    two(dayEndM_e);

} else {

  s1 =
    "Start: " +
    two(dayStartH) +
    ":" +
    two(dayStartM);

  s2 =
    "End: " +
    two(dayEndH) +
    ":" +
    two(dayEndM);
}

  uint16_t c1 = ST77XX_BLACK;
  uint16_t c2 = ST77XX_BLACK;

  // Start hours
  if(editMode && editField==0)
    c1 = ST77XX_BLUE;

  // Start minutes
  if(editMode && editField==1)
    c1 = ST77XX_BLUE;

  // End hours
  if(editMode && editField==2)
    c2 = ST77XX_BLUE;

  // End minutes
  if(editMode && editField==3)
    c2 = ST77XX_BLUE;

  draw(3,s1,1,c1);
  draw(4,s2,1,c2);
}

if(screen==2){

  draw(0,"Day Temp",2,ST77XX_RED);

  String a;
  String b;

  if(editMode){

    a="ON: "+String(dOn_e,1);
    b="OFF: "+String(dOff_e,1);

  } else {

    a="ON: "+String(dayOn,1);
    b="OFF: "+String(dayOff,1);
  }

  if(editMode && editField==0)
    a="> "+a;

  if(editMode && editField==1)
    b="> "+b;

  draw(4,a,1,
      (editMode && editField==0)?
      ST77XX_BLUE:ST77XX_BLACK);

  draw(5,b,1,
      (editMode && editField==1)?
      ST77XX_BLUE:ST77XX_BLACK);
}

    if(screen==3){

  draw(0,"Night Temp",2,ST77XX_RED);

  String a;
  String b;

  if(editMode){

    a="ON: "+String(nOn_e,1);
    b="OFF: "+String(nOff_e,1);

  } else {

    a="ON: "+String(nightOn,1);
    b="OFF: "+String(nightOff,1);
  }

  if(editMode && editField==0)
    a="> "+a;

  if(editMode && editField==1)
    b="> "+b;

  draw(4,a,1,
      (editMode && editField==0)?
      ST77XX_BLUE:ST77XX_BLACK);

  draw(5,b,1,
      (editMode && editField==1)?
      ST77XX_BLUE:ST77XX_BLACK);
}


if(screen==4){

  draw(0,"Light",2,ST77XX_RED);

  String s="Sensor "+String(sensorEnabled?"ON":"OFF");

  if(editMode && editField==0)
    s="> "+s;

  draw(3,s,1,
      (editMode && editField==0)?
      ST77XX_BLUE:ST77XX_BLACK);

  if(!sensorEnabled){

    String l="Light: "+
      String(lightState?"ON":"OFF");

    if(editMode && editField==1)
      l="> "+l;

    draw(5,l,1,
        (editMode && editField==1)?
        ST77XX_BLUE:ST77XX_BLACK);

  } else {

    // очищаем старую строку
    prev[5]="";
    tft.fillRect(0,100,128,18,ST77XX_WHITE);
  }
}
  }
}