#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include "RTClib.h"
#include <SoftwareSerial.h>      //we have to include the SoftwareSerial library, or else we can't use it. 

// A simple data logger for the Arduino analog pins

// how many milliseconds between grabbing data and logging it. 1000 ms is once a second
#define LOG_INTERVAL  10000 // mills between entries (reduce to take more/faster data)
unsigned long previousMillis = 0;        // will store last time LED was updated

// how many milliseconds before writing the logged data permanently to disk
// set it to the LOG_INTERVAL to write each time (safest)
// set it to 10*LOG_INTERVAL to write all data every 10 datareads, you could lose up to 
// the last 10 reads if power is lost but it uses less power and is much faster!
#define SYNC_INTERVAL 1000 // mills between calls to flush() - to write data to the card
uint32_t syncTime = 0; // time of last sync()

#define ECHO_TO_SERIAL   1 // echo data to serial port
#define WAIT_TO_START    0 // Wait for serial input in setup()

// the digital pins that connect to the LEDs
#define redLEDpin 4 //optional LED spot L1  --- not wired
#define greenLEDpin 5 //optional LED spot L2 --- not wired

// The analog pins that connect to the sensors
#define usAnalog A0           // analog 0
#define currentAnalog A1      // analog 1
#define voltageAnalog A2      // analog 2

// The analog pins that connect to the flow meter
#define rx 2                     //define what pin rx is going to be.
#define tx 3                     //define what pin tx is going to be.
SoftwareSerial myserial(rx, tx); //define how the soft serial port is going to work. 

RTC_PCF8523 RTC; // define the Real Time Clock object

char sensordata[30];                //we make a 48 byte character array to hold incoming data from the Flow Meter Totalizer.  
char sensordatajunk[30];
byte received_from_computer=0;     //we need to know how many characters have been received.                                 
byte sensor_bytes_received=0;       //we need to know how many characters have been received.
byte string_received=0;            //used to identify when we have received a string from the Flow Meter Totalizer.
float f;
char *flotot;

// for the data logging shield, we use digital pin 10 for the SD cs line
const int chipSelect = 10;

int AVGcurrentAnalogReading = 0;
int AVGvoltageAnalogReading = 0;
unsigned long currentAnalogReadingSUM = 0;
unsigned long voltageAnalogReadingSUM = 0;
unsigned long readingCount = 0;

// the logging file
File logfile;

void error(char *str)
{
  Serial.print("error: ");
  Serial.println(str);
  
  // red LED indicates error
  digitalWrite(redLEDpin, HIGH);

  while(1);
}

void setup(void)
{
  Serial.begin(9600);
  myserial.begin(9600);        //enable the software serial port
  Serial.println();
  
  // use debugging LEDs
  pinMode(redLEDpin, OUTPUT);
  pinMode(greenLEDpin, OUTPUT);
  
#if WAIT_TO_START
  Serial.println("Type any character to start");
  while (!Serial.available());
#endif //WAIT_TO_START

  // initialize the SD card
  Serial.print("Initializing SD card...");
  // make sure that the default chip select pin is set to
  // output, even if you don't use it:
  pinMode(10, OUTPUT);
  
  // see if the card is present and can be initialized:
  if (!SD.begin(chipSelect)) {
    error("Card failed, or not present");
  }
  
    // connect to RTC
  Wire.begin();  
  if (!RTC.begin()) {
    logfile.println("RTC failed");
#if ECHO_TO_SERIAL
    Serial.println("RTC failed");
#endif  //ECHO_TO_SERIAL
  }
  
  // If you want to set the aref to something other than 5v
  //analogReference(EXTERNAL);
}

void loop(void) {  
  analogRead(currentAnalog); 
  delay(10);
  currentAnalogReadingSUM += analogRead(currentAnalog);
  delay(10);
  analogRead(voltageAnalog);
  delay(10);
  voltageAnalogReadingSUM += analogRead(voltageAnalog);
  delay(10);
  ++readingCount;
  delay(1000); 
  
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= LOG_INTERVAL) {
    previousMillis = currentMillis;
        Serial.println("card initialized.");
  
  // create a filename
  DateTime now;
  now = RTC.now();
  int YR = now.year();
  String YRs = String(YR,DEC);
  int MO = now.month();
  String MOs = String(MO,DEC);
  int DY = now.day();
  String DYs = String(DY,DEC);
  String DATE = YRs + MOs + DYs;
  
  
  String filename = DATE + ".CSV";
    if (SD.exists(filename)) {
      // open existing file
      logfile = SD.open(filename, FILE_WRITE); 
      #if ECHO_TO_SERIAL
      Serial.println("File already exists, opening...");
      #endif //ECHO_TO_SERIAL
    }   
    
    if (! SD.exists(filename)) {
      // only open a new file if it doesn't exist
      
      logfile = SD.open(filename, FILE_WRITE); 
      logfile.println("unixtime,total,us,current,voltage");    
      #if ECHO_TO_SERIAL
      Serial.println("Creating new file!");
      Serial.println("unixtime,total,us,current,voltage,n");
      #endif //ECHO_TO_SERIAL
    }
  
  Serial.print("Logging to: ");
  Serial.println(filename);
    
    // fetch the time
    now = RTC.now();
    // log time
    logfile.print(now.unixtime()); // seconds since 1/1/1970
    logfile.print(", ");
  
    #if ECHO_TO_SERIAL
      Serial.print(now.unixtime()); // seconds since 1/1/1970
      Serial.print(", ");

    #endif //ECHO_TO_SERIAL
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
      delay(100);
    
  logfile.print(f);
  logfile.print(", ");
  logfile.print(USF);
  logfile.print(", ");    
  logfile.print(ACF);
  logfile.print(", ");    
  logfile.print(AVF);
#if ECHO_TO_SERIAL
  Serial.print(f);
  Serial.print(", ");
  Serial.print(USF);
  Serial.print(", ");    
  Serial.print(ACF);
  Serial.print(", ");    
  Serial.print(AVF);
  Serial.print(", ");    
  Serial.print(readingCount);
#endif //ECHO_TO_SERIAL

  logfile.println();
#if ECHO_TO_SERIAL
  Serial.println();
#endif // ECHO_TO_SERIAL

  digitalWrite(greenLEDpin, LOW);

  // Now we write data to disk! Don't sync too often - requires 2048 bytes of I/O to SD card
  // which uses a bunch of power and takes time
  if ((millis() - syncTime) < SYNC_INTERVAL) return;
  syncTime = millis();
  
  // blink LED to show we are syncing data to the card & updating FAT!
  digitalWrite(redLEDpin, HIGH);
  logfile.flush();
  digitalWrite(redLEDpin, LOW);

  //reset averaging tools if interval is met
  readingCount=0; 
  AVGcurrentAnalogReading = 0;
  AVGvoltageAnalogReading = 0;
  currentAnalogReadingSUM = 0;
  voltageAnalogReadingSUM = 0;
  }
  // delay for the amount of time we want between readings
  //delay((LOG_INTERVAL -1) - (millis() % LOG_INTERVAL));
  
  digitalWrite(greenLEDpin, HIGH);


}

void flow(){//reads all flows, both instantaneous and totals

    myserial.print("R");                       //Send the command from the computer to the Atlas Scientific device using the softserial port
    myserial.print("\r");                      //After we send the command we send a carriage return <CR>
    delay(100);
    
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
