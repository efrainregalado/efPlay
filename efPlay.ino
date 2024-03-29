//Based on https://raw.githubusercontent.com/rstellhorn/Steering_Wheel_Analog_Bluetooth_Serial/master/Steering_Wheel_Analog_Bluetooth_Serial.ino

// OLED
#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>

// SW Debounce for buttons
#include <Bounce2.h>

// ATMega32u4 pins
#define OLED_DC    49 //  D8  - B4
#define OLED_CS    48 // D17 - B0 
#define OLED_RESET 50 // was 57  = D9  - B5 
//Note: also need to connect SCL to HW SCK = 52 and SDA to HW MOSI = 51 (in Arduino MEGA)

U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI u8g2(U8G2_R2, /* cs=*/ OLED_CS, /* dc=*/ OLED_DC, /* reset=*/ OLED_RESET);
// Enable U8G2_16BIT in u8g2.h -> it will cause offset issues on drawFrame
///* clock=*/ 52, /* data=*/ 51,

//Constants
const int screenWidth = 256; //pixels
const int screenHeight = 64; //pixels

//WIRING encoders + multi-rocker switch:
const int encoderPin1 = 2;  //Encoder1   White
const int encoderPin2 = 3;  //Encoder2   Red
const int buttonD =     4;  //Left       Yellow X
const int buttonC =     5;  //Down       Yellow
const int buttonB =     6;  //Right      Blue
const int buttonA =     7;  //Up         Green
const int buttonPush =  8;  //Push       Red X

//scroll
const int scrollWait = 2000; //ms before it starts and after it stops scrolling
const int offsetSize = 3;

//Variables
volatile int lastEncoded = 0;
volatile long encoderValue = 0;
long oldEncoderValue = 0;
int lastMSB = 0;
int lastLSB = 0;
int volume = 0;

//scroll
int trackOffset = 0;
int trackWidth = 0;
long trackTimer = 0;
bool trackEndWait = false;
int artistOffset = 0;
int artistWidth = 0;
long artistTimer = 0;
bool artistEndWait = false;

//debounce delay
const int debouncerInterval = 10; //in ms
Bounce debouncerA = Bounce();
Bounce debouncerB = Bounce();
Bounce debouncerC = Bounce();
Bounce debouncerD = Bounce();
Bounce debouncerPush = Bounce();
boolean isButtonDown = false;

const int commandThreshold = 1000; //ms to keep the screenCommand visible before going back to screenUpdate view
long commandTimer = 0;

const String deviceName = "efPlay";

//Properties
int line = 0;

//AVRCP_ command is different in v6 and v7
int ver = 6;
String avrcp_Command = "";
int offset = 0;

String connectedName = "Not Connected";
String connectedId = "Not Connected";

String track     = "No info";
String artist    = "No info";
String trackTime = "No info";

unsigned long startTime = 0;
unsigned long pausedTime = 0;
unsigned long elapsed = 0;
long barTime = 0;

String serialString = "";
//String audioLinkID = "10";
//String sourceLinkID = "11";
//String callLinkID = "13";
int startingVolume = 5; // 0-9, A-F
bool initialized = false;
String command = "";
boolean playing = false;

void setup() {
  //Serials init
  Serial.begin(9600);
  Serial1.begin(9600);

  //oled
  u8g2.begin();
  u8g2.setFontRefHeightExtendedText();
  u8g2.setFontPosTop();
  u8g2.setFontDirection(0);
  u8g2.enableUTF8Print();    // enable UTF8 support for the Arduino print() function

  Serial.println("initialized display");
  commandTimer = millis() - commandThreshold - 1;
  screenCommand("CONNECTING...");

  //encoder + pushbutton
  //Initialize Buttons
  pinMode(buttonA, INPUT_PULLUP);
  pinMode(buttonB, INPUT_PULLUP);
  pinMode(buttonC, INPUT_PULLUP);
  pinMode(buttonD, INPUT_PULLUP);
  pinMode(buttonPush, INPUT_PULLUP);
  debouncerA.attach(buttonA);
  debouncerB.attach(buttonB);
  debouncerC.attach(buttonC);
  debouncerD.attach(buttonD);
  debouncerPush.attach(buttonPush);
  debouncerA.interval(debouncerInterval);
  debouncerB.interval(debouncerInterval);
  debouncerC.interval(debouncerInterval);
  debouncerD.interval(debouncerInterval);
  debouncerPush.interval(debouncerInterval);

  //Initialize Encoders
  pinMode(encoderPin1, INPUT);
  pinMode(encoderPin2, INPUT);
  digitalWrite(encoderPin1, HIGH); //turn pullup resistor on
  digitalWrite(encoderPin2, HIGH); //turn pullup resistor on
  //call updateEncoder() when any high/low changed seen
  //on interrupt 0 (pin 2), or interrupt 1 (pin 3)
  attachInterrupt(0, updateEncoder, CHANGE);
  attachInterrupt(1, updateEncoder, CHANGE);

  //setup BT device
  Serial1.print("SET NAME " + deviceName + "\r");
  Serial1.print("SET AUTOCONN 1\r");
  Serial1.print("SET MM OFF OFF 0 OFF OFF OFF OFF OFF\r");
  Serial1.print("SET MUSIC_META_DATA ON\r");
  Serial1.print("WRITE\r");
  Serial1.print("RESET\r");

  if (ver == 6) {
    avrcp_Command = "AVRCP_MEDIA ";
    offset = 0;
  } else { // if is ver 7+
    avrcp_Command = "AVRCP_MEDIA 11 ";
    offset = 3;
  }

}

#define PROFILE 0
/* Starts a timer to measure how long a function takes */
unsigned long timer;
void startTimer() {
#if PROFILE
  timer = millis();
#endif
}
/* Outputs how long it was before the last checkpoint */
void checkpoint(String checkpoint) {
#if PROFILE
  unsigned long current = millis() - timer;
  if (current > 0) {
    Serial.println("[Timer] " + checkpoint + ": " + String(current) + "ms");
  }
  timer = millis();
#endif
}

void loop() {
  parseSerial();
  screenUpdate();
  checkEncoder();
  checkButton();
}

void parseSerial() {
  boolean p = false;
  while (Serial1.available() > 0) {
    p = true;
    char c = Serial1.read();
    if (c == '\r') {
      command = serialString;
      serialString = "";
      // Print whatever comes from the BC127 serial into the Arduino Serial
      Serial.println("BT: " + command);

      if (command.startsWith("OPEN_OK 11")) {
        //check if the BC127 is connected to a phone
        //First run commands
        Serial.println("Auto-playing music if available");
        Serial1.print("MUSIC 11 PLAY\r");
        initialized = true;

      } else if (command.startsWith("CLOSE_OK 11")) {
        initialized = false;
        screenCommand ("DISCONNECTED");

      } else if (command.startsWith("LINK 11 CONNECTED AVRCP")) {
        connectedId = command.substring(24, 36);
        Serial.print("NAME " + connectedId);
        Serial1.print("NAME " + connectedId + "\r");


      } else if (command.startsWith("NAME " + connectedId)) {
        connectedName = command.substring(19, (command.length() - 1));
        Serial.println("deviceName: " + connectedName);
        screenCommand ("BT: " + connectedName);

      } else  if (command.startsWith(avrcp_Command + "TITLE")) {
        playing = true;
        //check if is a new track
        String previousTrack = track;
        track = command.substring(19 + offset);
        if (previousTrack != track) {
          //set track original start time
          startTime = millis();
          //pausedTime = 0;
          //on new track, determine pixel width
          u8g2.setFont(u8g2_font_6x10_tf);
          trackWidth = u8g2.getUTF8Width(track.c_str());
          //reset scrolling variables
          trackOffset = 0;
          trackTimer = 0;
          trackEndWait = false;
          //Serial.println("track width: " + (String) trackWidth);
        }

      } else if (command.startsWith(avrcp_Command + "ARTIST")) {
        artist = command.substring(20 + offset);

        //on new artist, determine pixel width
        u8g2.setFont(u8g2_font_6x10_tf);
        artistWidth = u8g2.getUTF8Width(artist.c_str());
        //reset scrolling variables
        artistOffset = 0;
        artistTimer = 0;
        artistEndWait = false;

        //Serial.println("artist width: " + (String) artistWidth);

      } else if (command.startsWith(avrcp_Command + "PLAYING")) {
        trackTime = command.substring(30 + offset);
        //validate that the track length is valid (an actual number)
        if (trackTime.toInt() % 1 == 0) {
          //Serial.println("Track length: " + (String) trackTime);
        } else {
          trackTime = "No info";
          Serial.println("Track length couldn't be read correctly: " + (String) trackTime);

        }

      } else if (command.startsWith("10 A2DP")) {
        //Check volume, returns a number from 0-9 and A-F
        volume = strtol(command.substring(8).c_str(), 0, 16);

      } else if (command.startsWith("AVRCP_PLAY 11")) {
        //user pressed played from device
        play();
      } else if (command.startsWith("AVRCP_PAUSE 11")) {
        //user paused from device
        pause();
      } else if (command.startsWith("ABS_VOL 11")) {
        //volume returns a number from 0-127, turn it into hex 0-F
        float absVol = strtol(command.substring(11).c_str(), 0, 10);
        volume = ((absVol) / 128.0) * 16;
      } else if (command.startsWith("A2DP_STREAM_START 10")) {
        Serial.println("Set starting volume");
        //Set volume of A2DP to 5 out of 10 - defined and saved previously with BT_VOL_CONFIG
        Serial1.print("VOLUME 10 ");
        Serial1.print(startingVolume);
        Serial1.print("\r");
        checkVolume();
      }
    } else {
      serialString += c;
    }
  }
}

void screenUpdate() {
  if (millis() - commandTimer <= commandThreshold) {
    return;
  }
  if (!initialized || Serial1.available() > 0) {
    return;
  }

  startTimer();
  if (track == "No info") {
    screenCommand("FINDING MEDIA");
    return;
  }
  u8g2.clearBuffer();

  u8g2.setFontMode(1);  /* activate transparent font mode */
  u8g2.setDrawColor(1); /* color 1 for the box */
  u8g2.drawBox(0, 8, 25, 8);

  //track label and string
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

  //artist label and string
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

  //status & volume
  if (playing) {
    u8g2.setDrawColor(1); /* color 1 for the box */
    u8g2.drawBox(200, 0, 40, 13);
    u8g2.setDrawColor(0);
    u8g2.setCursor(209, 2);
    u8g2.print("PLAY");
  }
  else {
    u8g2.setDrawColor(1); /* color 1 for the box */
    u8g2.drawFrame(200, 0, 40, 13);
    u8g2.setCursor(205, 2);
    u8g2.print("PAUSE");
  }

  //display volume in bottom right corner
  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.setCursor(205, 53);

  String volumeLevel;
  if (volume >= 15) {
    volumeLevel = "MAX";
  } else if (volume <= 0) {
    volumeLevel = "MIN";
  } else {
    volumeLevel = volume;
  }
  u8g2.print("VOL "); u8g2.print(volumeLevel);

  //keep the bar static if paused
  if (playing) {
    barTime = millis() - startTime;
  }

  u8g2.setDrawColor(1); /* color 1 for the box */
  //draw the bar if values are valid
  if (trackTime != "No info" && barTime > 0) {
    float percentage = ((float)(barTime) / trackTime.toFloat());
    int barLength = 1 + (percentage * (screenWidth - 2));
    u8g2.drawBox(0, 62, barLength, 3);
  } else {
    Serial.println("Either Bar: " + (String) barTime + " or Track length: " + (String)trackTime + " are not valid");
  }

  //refresh the screen with updated info
  if (Serial1.available() == 0) {
    u8g2.sendBuffer();
  }
}

void checkButton() {
  //
  //       A
  //       ^
  //       |
  // B <--   --> D
  //       |
  //       v
  //       C

  debouncerA.update();
  debouncerB.update();
  debouncerC.update();
  debouncerD.update();
  debouncerPush.update();

  if (debouncerPush.read() == HIGH) {
    isButtonDown = false;
    //No button pushed or delay threshold not met
    return;
  } else if (!isButtonDown) {
    isButtonDown = true;
    if (debouncerA.read() == LOW) { //Up Button, STATUS
      Serial.println("*Pressed Up");
      if (initialized) {
        Serial1.print("STATUS AVRCP\r");
      } else {
        screenCommand("NOT CONNECTED");
      }
    } else if (debouncerB.read() == LOW) {
      Serial.println("*BACKWARD command"); //Left Button, BACKWARD
      Serial1.print("MUSIC 11 BACKWARD\r");
      //screenCommand("BACKWARD");
    } else if (debouncerC.read() == LOW) {
      Serial.println("Pressed Down");
    } else if (debouncerD.read() == LOW) {
      Serial.println("*FORWARD command"); //Right Button, FORWARD
      Serial1.print("MUSIC 11 FORWARD\r");
      //      screenCommand("FORWARD");
    } else {
      Serial.println("Push");
      //button is being pushed
      Serial.println("*BUTTON pushed");
      checkVolume();
      Serial1.print("MUSIC 11 ");
      if (playing) {
        Serial1.print("PAUSE\r"); //If music is playing, then stop it
        Serial.println("*PAUSE command");
        pause();
      } else {
        Serial1.print("PLAY\r"); //nothing is playing, let's listen to something
        Serial.println("*PLAY command");
        play();
      }
    }
  }
}

void play() {
  if (!playing) {
    if (pausedTime > 0) {
      startTime = startTime + (millis() - pausedTime);
      pausedTime = 0;
    }
    playing = true;
  }
}

void pause() {
  if (playing) {
    pausedTime = millis();
    playing = false;
    Serial.println("!! User paused at " + (String)pausedTime);
  }
}

void checkVolume() {
  Serial1.print("VOLUME 10\r");
}

void checkEncoder() {
  //detect if encoder moved
  if (encoderValue == oldEncoderValue) {
    return;
  }
  else {
    if (encoderValue < oldEncoderValue) {
      Serial.println("*Down command");
      Serial1.print("VOLUME 10 DOWN\r");
      String volumeLevel = String((volume - 1), DEC);
      if (volume <= 0) {
        volumeLevel = "MIN";
      }
      //screenCommand("VOLUME " + volumeLevel);

    } else {
      Serial.println("*UP command");
      Serial1.print("VOLUME 10 UP\r");

      String volumeLevel = String((volume + 1), DEC);
      if (volume >= 15) {
        volumeLevel = "MAX";
      }
      //screenCommand("VOLUME " + volumeLevel);

    }
    Serial1.print("VOLUME 10\r");
    oldEncoderValue = encoderValue;
  }
}

void screenCommand(String command) {
  if (millis() - commandTimer <= commandThreshold) {
    return;
  }
  commandTimer = millis();

  u8g2.clearBuffer();

  u8g2.setDrawColor(1);
  //todo: update calc to center space with new font
  u8g2.setCursor(((screenWidth - (command.length() * 11 )) / 2) + 10, 26);
  u8g2.setFont(u8g2_font_helvB14_tr);
  u8g2.print(command);

  //refresh the screen with updated info
  u8g2.sendBuffer();

}

void updateEncoder() {
  int MSB = digitalRead(encoderPin1); //MSB = most significant bit
  int LSB = digitalRead(encoderPin2); //LSB = least significant bit

  int encoded = (MSB << 1) | LSB; //converting the 2 pin value to single number
  int sum = (lastEncoded << 2) | encoded; //adding it to the previous encoded value
  if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011)
    encoderValue ++;
  if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000)
    encoderValue --;
  lastEncoded = encoded; //store this value for next time
}
