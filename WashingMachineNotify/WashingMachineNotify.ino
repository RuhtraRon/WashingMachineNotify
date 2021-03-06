#include <WiFi.h>
#include <rom/rtc.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <IFTTTMaker.h>
#include "time.h"

#define SDA_PIN 23
#define SCL_PIN 18

RTC_DATA_ATTR char event_name[64] = "IFTTT_EVENT_NAME";
RTC_DATA_ATTR char ifttt_key[32] = "IFTTT_MAKER_KEY";

#define ACCEL 0x18 //I2C device address
#define CTRL_REG1 0x20 //Data rate selection and X, Y, Z axis enable register
#define CTRL_REG2 0x21 //High pass filter selection
#define CTRL_REG3 0x22 //Control interrupts
#define CTRL_REG4 0x23 //BDU
#define CTRL_REG5 0x24 //FIFO enable / latch interrupts

#define INT1_CFG 0x30 //Interrupt 1 config
#define INT1_SRC 0x31 //Interrupt status - read in order to reset the latch
#define INT1_THS 0x32 //Define threshold in mg to trigger interrupt
#define INT1_DURATION 0x33 //Define duration for the interrupt to be recognised (not sure about this one)

//Read this value to set the reference values against which accel values are compared when calculating a threshold interrupt
//Also called the REFERENCE register in the datasheet
#define HP_FILTER_RESET 0x26 

#define REG_X 0x28
#define REG_Y 0x2A
#define REG_Z 0x2C

#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  300      /* Snooze time between re-notifies */
#define AWAKE_TIME 10000 //milliseconds to wait for another vibration before sleep
#define NOTIFY_THRESHOLD 270000 //milliseconds of vibration before sending notification
#define TIMEOUT 120 //seconds for capture portal to be active
#define NOTIFY_DELAY 120 //seconds to wait before sending first notify

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -18000;
const int   daylightOffset_sec = 3600;

bool button_press = false;

const int ledPin = 22;
int ledState = HIGH;
const int intPin = 25;

WiFiClientSecure secureclient;
IFTTTMaker ifttt(ifttt_key, secureclient);

WiFiManager wifiManager;

RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR int prevState = 0;
RTC_DATA_ATTR bool renotify = false;
RTC_DATA_ATTR bool first_notify = false;
unsigned long bootTime = 0;
unsigned long machineOffTime = 0;
unsigned long blinkTime = 0;
unsigned long startWM = 0;

RTC_DATA_ATTR struct tm first_notify_time;

void blinkLed(){
  ledState = !ledState;
  digitalWrite(ledPin, ledState);
}

void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();
  Serial.print("wakeup_reason: ");
  Serial.println(wakeup_reason);
  switch(wakeup_reason)
  {
    case 1  : Serial.println("Wakeup caused by external signal using RTC_IO"); break; //if(!first_notify){renotify=false;}
    case 2  : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case 3  : Serial.println("Wakeup caused by timer"); break;
    case 4  : Serial.println("Wakeup caused by touchpad"); break;
    case 5  : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.println("Wakeup was not caused by deep sleep"); renotify=false; first_notify = false; break;
  }
}

void verbose_print_reset_reason(RESET_REASON reason)
{
  switch ( reason)
  {
    case 1  : Serial.println ("Vbat power on reset"); button_press=true; break;
    case 3  : Serial.println ("Software reset digital core");break;
    case 4  : Serial.println ("Legacy watch dog reset digital core");break;
    case 5  : Serial.println ("Deep Sleep reset digital core");break;
    case 6  : Serial.println ("Reset by SLC module, reset digital core");break;
    case 7  : Serial.println ("Timer Group0 Watch dog reset digital core");break;
    case 8  : Serial.println ("Timer Group1 Watch dog reset digital core");break;
    case 9  : Serial.println ("RTC Watch dog Reset digital core");break;
    case 10 : Serial.println ("Instrusion tested to reset CPU");break;
    case 11 : Serial.println ("Time Group reset CPU");break;
    case 12 : Serial.println ("Software reset CPU");break;
    case 13 : Serial.println ("RTC Watch dog Reset CPU");break;
    case 14 : Serial.println ("for APP CPU, reseted by PRO CPU");break;
    case 15 : Serial.println ("Reset when the vdd voltage is not stable");break;
    case 16 : Serial.println ("RTC Watch dog reset digital core and rtc module");button_press=true;break;
    default : Serial.println ("NO_MEAN");
  }
}

void wifiConfig(){
  #define STA_SSID "YOUR_SSID"
  #define STA_PASS "YOUR_PASSWORD"
  WiFi.begin(STA_SSID, STA_PASS);
  wifiWait();
}

void wifiWait(){
    Serial.println("Waiting for WiFi");
    int wifi_wait_count = 30;
    int count = 0;
    while ((WiFi.status() != WL_CONNECTED)&&(count < wifi_wait_count)) {
        delay(500);
        Serial.print(".");
        count++;
    }
    if (count >= wifi_wait_count){
      esp_sleep_enable_timer_wakeup(1 * uS_TO_S_FACTOR);
      esp_deep_sleep_start();
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}

void printAccel(int vibrations) {
  float x = getAccel(readReg(REG_X));
  float y = getAccel(readReg(REG_Y));
  float z = getAccel(readReg(REG_Z));

  Serial.print("Read: ");
  printFloat(x, 3);
  Serial.print(", ");
  printFloat(y, 3);
  Serial.print(", ");
  printFloat(z, 3);
  Serial.print(", ");
  Serial.println(vibrations);
  
  writeReg(INT1_CFG, 0x2a);
}

void iftttsend(){
  //triggerEvent takes an Event Name and then you can optional pass in up to 3 extra Strings
  if(ifttt.triggerEvent(event_name)){
    Serial.println("IFTTT Successfully sent");
  } else
  {
    Serial.println("IFTTT Failed!");
  }
}

struct tm timeinfo;
void printLocalTime()
{
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

void setupAccel(){
  writeReg(CTRL_REG1, 0x47); //Set data rate to 50hz. Enable X, Y and Z axes
  //1 - BDU: Block data update. This ensures that both the high and the low bytes for each 16bit represent the same sample
  //0 - BLE: Big/little endian. Set to little endian
  //00 - FS1-FS0: Full scale selection. 00 represents +-2g
  //1 - HR: High resolution mode enabled.
  //00 - ST1-ST0: Self test. Disabled
  //0 - SIM: SPI serial interface mode. Default is 0.
  writeReg(CTRL_REG4, 0x88);
  writeReg(CTRL_REG2, 0x09);//2 Write 09h into CTRL_REG2 // High-pass filter enabled on data and interrupt1
  writeReg(CTRL_REG3, 0x40);//3 Write 40h into CTRL_REG3 // Interrupt driven to INT1 pad
  //4 Write 00h into CTRL_REG4 // FS = 2 g
  writeReg(CTRL_REG5, 0x08);//5 Write 08h into CTRL_REG5 // Interrupt latched
  writeReg(INT1_THS, 0x04);// Threshold as a multiple of 16mg. 4 * 16 = 64mg
  writeReg(INT1_DURATION, 0x00);// Duration = 0 not quite sure what this does yet
  // Read the reference register to set the reference acceleration values against which
  // we compare current values for interrupt generation
  readReg(HP_FILTER_RESET);//8 Read HP_FILTER_RESET
  writeReg(INT1_CFG, 0x2a);//9 Write 2Ah into INT1_CFG // Configure interrupt when any of the X, Y or Z axes exceeds (rather than stay below) the threshold
  readReg(INT1_SRC);
}

void wifiManagerConfig(){
  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length

  WiFiManagerParameter custom_event_name("name", "event name", event_name, 64);
  WiFiManagerParameter custom_ifttt_key("key", "ifttt key", ifttt_key, 32);

  wifiManager.addParameter(&custom_event_name);
  wifiManager.addParameter(&custom_ifttt_key);

  wifiManager.setBreakAfterConfig(true);
  wifiManager.setConfigPortalTimeout(TIMEOUT);

  startWM = millis();
  
  if (!button_press){
    wifiManager.autoConnect("WashingMachine");
  } else {
    wifiManager.startConfigPortal("WashingMachine");
  }
  
  if ((WiFi.status() != WL_CONNECTED)&&(millis() > startWM + (TIMEOUT*1000))){ //setConfigPortalTimeout must have triggered
    Serial.println("Going to sleep, portal timeout");
    esp_deep_sleep_start();
  }
  strcpy(event_name, custom_event_name.getValue());
  strcpy(ifttt_key, custom_ifttt_key.getValue());

  Serial.print("event_name: ");
  Serial.println(event_name);
  Serial.print("ifttt_key: ");
  Serial.println(ifttt_key);

  wifiWait();
}

void setup() {
  Serial.begin(115200);
  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, ledState); //off
  print_wakeup_reason();
  verbose_print_reset_reason(rtc_get_reset_reason(0));
  initI2C();
  setupAccel();
  pinMode(intPin, INPUT);
  attachInterrupt(intPin, wakeUp, RISING);
  Serial.println("Accelerometer enabled");
  
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_25, HIGH);

  blinkLed(); //on

  wifiManagerConfig();
//  wifiConfig();

  blinkLed(); //off
  bootTime = millis();
  Serial.print("Re-notify: ");
  Serial.println(renotify);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  printLocalTime();
  Serial.println(&first_notify_time, "%A, %B %d %Y %H:%M:%S");
}

float getAccel(int16_t accel) {
  return float(accel) / 16000;
}

volatile bool machineOn = false;
volatile unsigned int vibrations = 0;
unsigned int prev_vibrations = 0;

int activeLoopInterval = 200; //Loop 5 times a second

bool started = false;
float readdata = 0.0;

void loop() {  
  if (!started) { //first power on
      started = true;
      //Reset the interrupt when we are actually ready to receive it
      readReg(INT1_SRC);
    }
  if (machineOn) {
    readReg(INT1_SRC);
    delay(activeLoopInterval);
    blinkLed();
    printAccel(vibrations);

    if (vibrations > prev_vibrations){
      prev_vibrations = vibrations;
    } else{
      machineOn = false;
      machineOffTime = millis();
    }
    if (renotify && vibrations > 10){
      renotify = false;
      int i = 100;
      while (i >0){
        blinkLed();
        delay(50);
        i--;
      }
    }
  } else if (millis() - machineOffTime > AWAKE_TIME) {
    if ((millis() - bootTime > (NOTIFY_THRESHOLD)) || renotify) {
      if (!renotify){
        first_notify = true;
        Serial.println("Turning first_notify on");
        getLocalTime(&first_notify_time);
        Serial.print("Setting first_notify_time: ");
        Serial.println(&first_notify_time, "%A, %B %d %Y %H:%M:%S");
        renotify = true;
        Serial.println("Re-notify On");
      }
      printLocalTime();
      double notifydifftime = difftime(mktime(&timeinfo),mktime(&first_notify_time));
      Serial.print("Time Diff: ");
      Serial.println(notifydifftime);
      int sleep_time = 0;
      if (first_notify){
        sleep_time = (NOTIFY_DELAY-notifydifftime);
      } else {
        sleep_time = (TIME_TO_SLEEP-notifydifftime);
      }
      if (sleep_time < 0) {
        iftttsend();
        first_notify = false;
        Serial.println("Turning first_notify off");
        getLocalTime(&first_notify_time);
        Serial.print("Setting first_notify_time: ");
        Serial.println(&first_notify_time, "%A, %B %d %Y %H:%M:%S");
        sleep_time = TIME_TO_SLEEP;
      }
      esp_sleep_enable_timer_wakeup(sleep_time * uS_TO_S_FACTOR);
      Serial.print("Sleeping for: ");
      Serial.println(sleep_time);
    }
    writeReg(CTRL_REG1, 0x2f); //Set data rate to 1z. Enable X, Y and Z axes in low power mode
    writeReg(CTRL_REG4, 0x0); //Turn off BDU, turn off high precision mode
    readReg(HP_FILTER_RESET); //Reset the reference state (seems to be necessary when changing the ODR)
    readReg(INT1_SRC); //Reset the interrupt so it will trigger when we are asleep
    Serial.println("Going to sleep now");
    esp_deep_sleep_start();

  } else {
    //Slow flash when idling
    if (millis() - blinkTime > 1000){
      blinkTime = millis();
      blinkLed();
      printAccel(vibrations);
    }
  }
}

void wakeUp() {
  machineOn = true;
  vibrations++;
}
