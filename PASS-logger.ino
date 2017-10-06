/*
  SD card datalogger

 This example shows how to log data from three analog sensors
 to an SD card using the SD library.

 The circuit:
 * SD card attached to SPI bus as follows:
 ** MOSI - pin 11
 ** MISO - pin 12
 ** CLK - pin 13
 ** CS - pin 10 

 */

#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include "RTClib.h"
#include <SoftwareSerial.h>      //we have to include the SoftwareSerial library, or else we can't use it. 

const int chipSelect = 10;

unsigned long LOG_INTERVAL = 60000;
unsigned long previousMillis = 0;


// The analog pins that connect to the sensors
#define usAnalog A0           // analog 0
#define currentAnalog A1      // analog 1
#define voltageAnalog A2      // analog 2

// The digital pins that connect to the flow meter
#define rx 2                     //define what pin rx is going to be.
#define tx 3                     //define what pin tx is going to be.
SoftwareSerial myserial(rx, tx); //define how the soft serial port is going to work. 

// define the Real Time Clock object
RTC_PCF8523 RTC; 

char sensordata[30];                //we make a 48 byte character array to hold incoming data from the Flow Meter Totalizer.  
char sensordatajunk[30];
byte received_from_computer=0;     //we need to know how many characters have been received.                                 
byte sensor_bytes_received=0;       //we need to know how many characters have been received.
byte string_received=0;            //used to identify when we have received a string from the Flow Meter Totalizer.
float f;
char *flotot;

int AVGcurrentAnalogReading = 0;
int AVGvoltageAnalogReading = 0;
unsigned long currentAnalogReadingSUM = 0;
unsigned long voltageAnalogReadingSUM = 0;
unsigned long readingCount = 0;

void setup() {
  // Open serial communications and wait for port to open:
  myserial.begin(9600);        //enable the software serial port
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }


  Serial.print("Initializing SD card...");

  // see if the card is present and can be initialized:
  if (!SD.begin(chipSelect)) {
    Serial.println("Card failed, or not present");
    // don't do anything more:
    return;
  }
  Serial.println("card initialized.");
  // connect to RTC
  Wire.begin();  
}

void loop() {
  analogRead(currentAnalog); 
  delay(10);
  currentAnalogReadingSUM += analogRead(currentAnalog);
  delay(10);
  analogRead(voltageAnalog);
  delay(10);
  voltageAnalogReadingSUM += analogRead(voltageAnalog);
  delay(10);
  ++readingCount;
  delay(960); 
  
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= LOG_INTERVAL) {
    previousMillis = currentMillis;
    
    // create a filename
    
    DateTime now;
    now = RTC.now();
    int YR = now.year();
    String YRs = String(YR,DEC);
    int MO = now.month();
    String MOs = String(MO,DEC);
    int DY = now.day();
    //int DY = now.minute();  //testing clear command
    String DYs = String(DY,DEC);
    String DATE = YRs + MOs + DYs;
    String filename = DATE + ".CSV";
  
    //collect data
    //Average repetative current and voltage readings between sampling interval
    AVGcurrentAnalogReading = currentAnalogReadingSUM / readingCount;
    AVGvoltageAnalogReading = voltageAnalogReadingSUM / readingCount;
    float AVF = AVGvoltageAnalogReading / 6.82;    
    float ACF = AVGcurrentAnalogReading / 102.94;
    //read tank level once per sampling interval
    analogRead(usAnalog);
    delay(10); 
    int usAnalogReading = analogRead(usAnalog);
    float USF = usAnalogReading * 0.0267 - 5.31;  
    
    //read total flow once per sampling interval
    flow();
    delay(200);
    
    // make a string for assembling the data to log:
    String dataString = "";
    dataString += String(now.unixtime());dataString += ",";
    dataString += String(f);dataString += ",";
    dataString += String(USF);dataString += ",";
    dataString += String(ACF);dataString += ",";
    dataString += String(AVF);
  
    // open the file. note that only one file can be open at a time,
    // so you have to close this one before opening another.
    File dataFile = SD.open(filename, FILE_WRITE);
  
    // if the file is available, write to it:
    if (dataFile) {
      dataFile.println(dataString);
      dataFile.close();
      // print to the serial port too:
      Serial.println(dataString);
    }
    // if the file isn't open, pop up an error:
    else {
      Serial.print("error opening ");Serial.print(filename);Serial.println("!");
    }
  
    //reset averaging tools if interval is met
    readingCount=0; 
    AVGcurrentAnalogReading = 0;
    AVGvoltageAnalogReading = 0;
    currentAnalogReadingSUM = 0;
    voltageAnalogReadingSUM = 0;
  }
}

void flow(){//reads all flows, both instantaneous and totals

    myserial.print("R");                       //Send the command from the computer to the Atlas Scientific device using the softserial port
    myserial.print("\r");                      //After we send the command we send a carriage return <CR>
    delay(200);
    
    if (myserial.available() > 0) {                 //If data has been transmitted from an Atlas Scientific device
      sensor_bytes_received = myserial.readBytesUntil(13, sensordata, 30); //we read the data sent from the Atlas Scientific device until we see a <CR>. We also count how many character have been received
      sensordata[sensor_bytes_received] = 0;         //we add a 0 to the spot in the array just after the last character we received. This will stop us from transmitting incorrect data that may have been left in the buffer
      flotot = strtok(sensordata, ",");          //Let's parse the string at each colon
      f=atof(flotot);
      delay(10);
      if (myserial.available() > 0) {                 //If data has been transmitted from an Atlas Scientific device
      int junk = myserial.readBytesUntil(13, sensordatajunk, 30);}
      delay(10);
    }
}
