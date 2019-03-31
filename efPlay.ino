//Based on https://raw.githubusercontent.com/rstellhorn/Steering_Wheel_Analog_Bluetooth_Serial/master/Steering_Wheel_Analog_Bluetooth_Serial.ino

// OLED
#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>

// ATMega32u4 pins
#define OLED_DC    49 //  D8  - B4
#define OLED_CS    48 // D17 - B0 
#define OLED_RESET 50 // was 57  = D9  - B5 
//Note: also need to connect SCL to HW SCK = 52 and SDA to HW MOSI = 51 (in Arduino MEGA)

U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI u8g2(U8G2_R2, /* cs=*/ OLED_CS, /* dc=*/ OLED_DC, /* reset=*/ OLED_RESET);
// Enable U8G2_16BIT in u8g2.h -> it will cause offset issues on drawFrame
///* clock=*/ 52, /* data=*/ 51,
const int screenWidth = 256; //pixels
const int screenHeight = 64; //pixels

const int commandThreshold = 1000; //ms to keep the screenCommand visible before going back to screenUpdate view
long commandTimer = 0;

String track    = "123456789012345678901234567890123456789012345678901--->";
String artist   = "abcdefghijklmnopqrstuvwxyz0123456789abcdefghijk--->";

//new variables
const int scrollWait = 2000; //ms before it starts and after it stops scrolling
const int offsetSize = 3;

int trackOffset = 0;
int trackWidth = 0;
long trackTimer = 0;
bool trackEndWait = false;

int artistOffset = 0;
int artistWidth = 0;
long artistTimer = 0;
bool artistEndWait = false;



void setup() {
  //Serials init
  Serial.begin(9600);

  //oled
  u8g2.begin();
  u8g2.setFontRefHeightExtendedText();
  u8g2.setFontPosTop();
  u8g2.setFontDirection(0);
  u8g2.enableUTF8Print();    // enable UTF8 support for the Arduino print() function

  Serial.println("initialized display");
  commandTimer = millis() - commandThreshold - 1;





  //on new track, determine pixel width
  u8g2.setFont(u8g2_font_6x10_tf);
  trackWidth = u8g2.getUTF8Width(track.c_str());
  Serial.println("track width: " + (String) trackWidth);

  //on new artist, determine pixel width
  u8g2.setFont(u8g2_font_6x10_tf);
  artistWidth = u8g2.getUTF8Width(artist.c_str());
  Serial.println("artist width: " + (String) artistWidth);

}


void loop() {
  screenUpdate();
}


void screenUpdate() {
  if (millis() - commandTimer <= commandThreshold) {
    return;
  }





  //on new track, determine pixel width
  trackWidth = u8g2.getStrWidth(track.c_str());

  //on new artist, determine pixel width
  artistWidth = u8g2.getStrWidth(artist.c_str());




  u8g2.clearBuffer();

  u8g2.setFontMode(1);  /* activate transparent font mode */
  u8g2.setDrawColor(1); /* color 1 for the box */
  u8g2.drawBox(0, 8, 25, 8);

  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.setCursor(0, 8);
  u8g2.print("TRACK");
  u8g2.setDrawColor(1);

  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setCursor(0 - trackOffset, 18);
  //scrolling logic
  if ((trackOffset == 0 || trackOffset > (trackWidth - 255)) && trackTimer == 0) {
    trackTimer = millis() + scrollWait;
  }
  if (millis() >= trackTimer && trackWidth >= 255) {
    if (trackOffset <= (trackWidth - 255)) {
      trackOffset = trackOffset + offsetSize;
    } else {
      if (!trackEndWait) {
        trackEndWait = true;
      } else {
        trackOffset = 0;
        trackEndWait = false;
      }
      trackTimer = 0;
    }
  }
  u8g2.print(track);

  u8g2.setFontMode(1);  /* activate transparent font mode */
  u8g2.setDrawColor(1); /* color 1 for the box */
  u8g2.drawBox(0, 32, 31, 8);

  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.setCursor(1, 32);
  u8g2.print("ARTIST");
  u8g2.setDrawColor(1);

  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setCursor(0 - artistOffset, 42);
  //scrolling logic
  if ((artistOffset == 0 || artistOffset > (artistWidth - 255)) && artistTimer == 0) {
    artistTimer = millis() + scrollWait;
  }
  if (millis() >= artistTimer && artistWidth >= 255) {
    if (artistOffset <= (artistWidth - 255)) {
      artistOffset = artistOffset + offsetSize;
    } else {
      if (!artistEndWait) {
        artistEndWait = true;
      } else {
        artistOffset = 0;
        artistEndWait = false;
      }
      artistTimer = 0;
    }
  }
  u8g2.print(artist);


  u8g2.sendBuffer();

}

