#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <IFTTTMaker.h>
#include <WiFiClientSecure.h>

#include <credentials.h>  //WiFi, IFTTT, SMTP2Go

extern "C" {
#include "user_interface.h"
}

ADC_MODE(ADC_VCC); //vcc read-mode

/*credentials & definitions */
//wifi

//IFTTT
#define EVENT_NAME1 "wpu_malfunction1" // Name of your event name, set when you are creating the applet
#define EVENT_NAME2 "wpu_malfunction2" // Name of your event name, set when you are creating the applet

// SMTP2GO
char server[] = "mail.smtpcorp.com";
int port = 2525;

WiFiClient client;
WiFiClientSecure Sclient;
IFTTTMaker ifttt(KEY, Sclient);

#define MODULE "module name"   //name of module here

#define MINUTE 60e6  // 60e6 is 60 seconds = 1 minute
#define HOUR MINUTE*60 // number of millis in an hour
#define DAY HOUR*24 // number of millis in a day
#define SLEEPTIME HOUR // short time for testing an debugging
#define DELAY 6     // mail is sent every # hours

#define BLUELED 13 // blue led on GPIO13
#define REDLED 12 // blue led on GPIO12
#define GREENLED 14 // blue led on GPIO14
#define FLASHPIN 5 // Switch for starting OTA flashmode on GPIO5
#define BATPIN 4 // Switch for checking battery on GPIO4

#define VOLT_THRES 3.28

#define WIFI_CONNECT_TIMEOUT_S 10

// define mail headers
#define HEADER_ERROR "Heatpump has an error"
#define HEADER_REMINDER "Reminder: Heatpump still has an error"
#define HEADER_LOW_BAT "ESP-07S_WPU_malfunction_detector low battery"
#define HEADER_BAT "battery measurement ESP-07S_WPU_malfunction_detector"

// define mail types
#define MAIL_ERROR 1
#define MAIL_REMINDER 2
#define MAIL_LOW_BAT 3
#define MAIL_BAT 4

//program modes
#define OTAFLASH_MODE 1
#define BAT_MODE 2
#define ERROR_MODE 3

// define mail adresses
#define MAIL_FROM "youremail@here.nl"
#define MAIL_TO_1 "youremail@here.nl"
#define MAIL_TO_2 "youremail@here.nl"
#define MAIL_TO_3 "youremail@here.nl"

// RTC-MEM Adresses
#define RTC_CHECK 65
#define RTC_FLAG 66
#define RTC_COUNT 67

#define CHECK_VALUE_1 56 // change values for a fresh start
#define CHECK_VALUE_2 88 // change values for a fresh start

// global variables
byte buf[2], countbuf[2];
float Voltage, volt_round;
uint32_t time1, time2;
int volt2;

int program_mode = 0; // this defines the mode of the program (WPU in error, battery test, OTAflash)

#define SERIAL_DEBUG 0

void setup()
{
  // set pinmodes
  pinMode(BATPIN, INPUT_PULLUP);
  pinMode(FLASHPIN, INPUT_PULLUP);
  pinMode(BLUELED, OUTPUT);
  pinMode(REDLED, OUTPUT);
  pinMode(GREENLED, OUTPUT);
  digitalWrite(BLUELED, LOW);
  digitalWrite(REDLED, LOW);
  digitalWrite(GREENLED, LOW);

  // if serial is not initialized all following calls to serial end dead.
  if (SERIAL_DEBUG)
  {
    Serial.begin(115200);
    delay(10);
    Serial.println("");
    Serial.println("");
    Serial.println("");
    Serial.println(F("Started from reset"));
  }

  // set program mode
  if (digitalRead(FLASHPIN) == LOW)
  {
    program_mode = OTAFLASH_MODE;
    digitalWrite(BLUELED, HIGH);    // turn blue led on
  }
  else if (digitalRead(BATPIN) == LOW) {
    program_mode = BAT_MODE;
    digitalWrite(GREENLED, HIGH);    // turn green led on
  }
  if (digitalRead(BATPIN) == HIGH && (digitalRead(FLASHPIN) == HIGH))
  {
    program_mode = ERROR_MODE;
    digitalWrite(REDLED, HIGH);    // turn red led on
  }

  // check init values
  system_rtc_mem_read(RTC_CHECK, buf, 2); // read 2 bytes from RTC-MEMORY
  Serial.println("");
  Serial.println("");
  Serial.print("check values:\t");
  Serial.print(CHECK_VALUE_1);
  Serial.print("\t");
  Serial.println(CHECK_VALUE_2);

  Serial.print("read values:\t");

  Serial.print(buf[0]);
  Serial.print("\t");
  Serial.println(buf[1]);

  if ((buf[0] != CHECK_VALUE_1) || (buf[1] != CHECK_VALUE_2))  // program runs for first time
  {
    buf[0] = CHECK_VALUE_1;
    buf[1] = CHECK_VALUE_2;
    Serial.println("program runs for first time, setting init values");
    system_rtc_mem_write(RTC_CHECK, buf, 2);
    // initialise values
    countbuf[0] = 0;
    system_rtc_mem_write(RTC_COUNT, countbuf, 1);     // set counter to 0
  }

  // Attempt to connect to Wifi network:
  Serial.print("Connecting Wifi: ");
  Serial.println(mySSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(mySSID, myPASSWORD);
  time1 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(50);
    time2 = millis();
    if (((time2 - time1) / 1000) > WIFI_CONNECT_TIMEOUT_S)  // wifi connection lasts too long
    {
      Serial.println("Connection Failed! Rebooting...");
      delay(2000);
      ESP.restart();
    }
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  IPAddress ip = WiFi.localIP();
  Serial.println(ip);

  // switch in setup
  switch (program_mode)
  {
    case 0:
      {
        Serial.println("No program mode defined! Rebooting...");
        delay(2000);
        ESP.restart();
        break;
      }
    case OTAFLASH_MODE:
      {
        // start of OTA routine in setup
        ArduinoOTA.begin();
        Serial.print("OTA Ready on IP address: ");
        Serial.println(WiFi.localIP());
        // end OTA routine
        break;
      }
    case BAT_MODE:
      {
        sendEmail(MAIL_BAT);
        digitalWrite(GREENLED, LOW);
        break;
      }
    case ERROR_MODE:
      {
        //check whether the e-mails must be send in this program run
        system_rtc_mem_read(RTC_COUNT, countbuf, 1);     // read counter from RTC mem
        if (countbuf[0] % DELAY == 0)
        {
          Serial.println("WPU malfunction, send  mail every # hours");
          if (countbuf[0]  == 0)                         //  send mail first time
          {
            sendEmail(MAIL_ERROR);
          }
          else
          {
            sendEmail(MAIL_REMINDER);                 //  send mail every # hours
          }
          //IFTTT actions

          //Notification to Wim
          if (ifttt.triggerEvent(EVENT_NAME1, mySSID, ip.toString())) {
            Serial.println("IFTTT Successfully sent");
          }
          else
          {
            Serial.println("IFTTT Failed!");
          }

          //SMS to Jose
          if (ifttt.triggerEvent(EVENT_NAME2, mySSID, ip.toString())) {
            Serial.println("IFTTT Successfully sent");
          }
          else
          {
            Serial.println("IFTTT Failed!");
          }

        }
        // check voltage level
        if ( (ESP.getVcc() / (float)1023 )  < VOLT_THRES)
        {
          sendEmail(MAIL_LOW_BAT);
        }
        Serial.print("current counter: ");
        Serial.println(countbuf[0]);
        countbuf[0] = countbuf[0] + 1;                        // increase counter
        system_rtc_mem_write(RTC_COUNT, countbuf, 1);     // write increased counter to RTC mem
        Serial.print("new counter: ");
        Serial.println(countbuf[0]);
        Serial.println("going to sleep");
        delay(500);
        //actions done, go to sleep

        ESP.deepSleep(SLEEPTIME, WAKE_RFCAL);
        delay(500);
        yield();
        //end of ERROR routine
        break;
      }
  }
}

void loop()
{
  // switch in loop
  switch (program_mode)
  {
    case OTAFLASH_MODE:
      {
        ArduinoOTA.handle();
        break;
      }
    case BAT_MODE:         // do nothing, only in setup
      {
        digitalWrite(GREENLED, HIGH);
        delay(100);
        digitalWrite(GREENLED, LOW);
        delay(500);
        yield();
        break;
      }
    case ERROR_MODE:     // do nothing, only in setup
      {
        yield();
        break;
      }
  }
}


// send mail function
void sendEmail (int mail_type)
{
  Serial.println("starting sendEmail routine");
  client.connect(server, port);
  if (!eRcv()) return ;
  client.println("EHLO www.example.com");
  if (!eRcv()) return;
  client.println("auth login");
  if (!eRcv()) return ;
  client.println(SMTP2goUSER); //<---------User in base64 from credentials file
  if (!eRcv()) return ;
  client.println(SMTP2goPW);//<---------Password in base64 from credentials file
  if (!eRcv()) return ;
  // change to your email address (sender)
  client.print(F("MAIL From: "));
  client.println(F(MAIL_FROM));
  if (!eRcv()) return ;
  // change to recipient address
  client.print(F("RCPT To: "));
  client.println(F(MAIL_TO_1));
  if (!eRcv()) return ;
  if (mail_type == MAIL_ERROR)
  {
    client.print(F("RCPT To: "));
    client.println(F(MAIL_TO_2));
    if (!eRcv()) return ;
    client.print(F("RCPT To: "));
    client.println(F(MAIL_TO_3));
    if (!eRcv()) return ;
  }
  client.println(F("DATA"));
  if (!eRcv()) return ;
  // change to recipient address
  client.print(F("To:  "));
  client.println(F(MAIL_TO_1));

  client.print(F("From: "));
  client.println(F(MAIL_FROM));
  client.print(F("Subject: "));

  if (mail_type == MAIL_ERROR)
    client.println(F(HEADER_ERROR));
  if (mail_type == MAIL_LOW_BAT)
    client.println(F(HEADER_LOW_BAT));
  if (mail_type == MAIL_BAT)
    client.println(F(HEADER_BAT));
  if (mail_type == MAIL_REMINDER)
    client.println(F(HEADER_REMINDER));

  client.print(F("Mail from module: "));
  client.print(MODULE);
  client.print(F(", ChipID = "));
  int32 ChipID = ESP.getChipId();
  client.println(ChipID);
  client.println(F(""));
  client.print(F("Battery voltage is: "));
  client.print((ESP.getVcc() / (float)1023 * (float)0.98), 1);
  client.println(F(" V"));
  client.println(F(""));
  client.print(F("WiFi power (RSSI): "));
  client.println(WiFi.RSSI());
  client.print(F("Sent via SSID: "));
  client.println(WiFi.SSID());
  

  if (mail_type == MAIL_REMINDER)
  {
    client.print(F("Error last about "));
    system_rtc_mem_read(RTC_COUNT, countbuf, 1); // read counter from RTC-MEMORY
    int temp_count = countbuf[0];
    client.print(temp_count);
    client.println(F(" hour"));
  }
  client.println(F(""));  //do not remove this last line
  client.println(F("."));  //do not remove this last important "." since it tells end of the mailbody

  if (!eRcv()) return ;
  client.println(F("QUIT"));
  if (!eRcv()) return ;
  client.stop();
  Serial.println("finished sendEmail routine");
  Serial.println("");
}

// function for checking mail sending
byte eRcv()
{
  byte respCode;
  byte thisByte;
  int loopCount = 0;

  while (!client.available()) {
    delay(1);
    loopCount++;
    // if nothing received for 10 seconds, timeout
    if (loopCount > 10000) {
      client.stop();
      Serial.println(F("\r\nTimeout"));
      return false;
    }
  }

  respCode = client.peek();
  while (client.available())
  {
    thisByte = client.read();
    Serial.write(thisByte);
  }
  if (respCode >= '4')
  {
    //  efail();
    return false;
  }
  return true;
}
