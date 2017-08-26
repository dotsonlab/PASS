#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include "RTClib.h"
#include <SoftwareSerial.h>      //we have to include the SoftwareSerial library, or else we can't use it. 

// A simple data logger for the Arduino analog pins

// how many milliseconds between grabbing data and logging it. 1000 ms is once a second
#define LOG_INTERVAL  5000 // mills between entries (reduce to take more/faster data)

// how many milliseconds before writing the logged data permanently to disk
// set it to the LOG_INTERVAL to write each time (safest)
// set it to 10*LOG_INTERVAL to write all data every 10 datareads, you could lose up to 
// the last 10 reads if power is lost but it uses less power and is much faster!
#define SYNC_INTERVAL 5000 // mills between calls to flush() - to write data to the card
uint32_t syncTime = 0; // time of last sync()

#define ECHO_TO_SERIAL   1 // echo data to serial port
#define WAIT_TO_START    0 // Wait for serial input in setup()

// the digital pins that connect to the LEDs
#define redLEDpin 4 //optional LED spot L1  --- not wired
#define greenLEDpin 5 //optional LED spot L2 --- not wired

// The analog pins that connect to the sensors
#define usAnalog 0           // analog 0
#define currentAnalog 1      // analog 1
#define voltageAnalog 2      // analog 2

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
  Serial.println("card initialized.");
  
  // create a new file
  char filename[] = "LOGGER00.CSV";
  for (uint8_t i = 0; i < 100; i++) {
    filename[6] = i/10 + '0';
    filename[7] = i%10 + '0';
    if (! SD.exists(filename)) {
      // only open a new file if it doesn't exist
      logfile = SD.open(filename, FILE_WRITE); 
      break;  // leave the loop!
    }
  }
  
  if (! logfile) {
    error("couldnt create file");
  }
  
  Serial.print("Logging to: ");
  Serial.println(filename);

  // connect to RTC
  Wire.begin();  
  if (!RTC.begin()) {
    logfile.println("RTC failed");
#if ECHO_TO_SERIAL
    Serial.println("RTC failed");
#endif  //ECHO_TO_SERIAL
  }
  

  logfile.println("unixtime,total,us,current,voltage");    
#if ECHO_TO_SERIAL
  Serial.println("unixtime,total,us,current,voltage");
#endif //ECHO_TO_SERIAL
 
  // If you want to set the aref to something other than 5v
  analogReference(EXTERNAL);
}

void loop(void)
{
  DateTime now;

  // delay for the amount of time we want between readings
  delay((LOG_INTERVAL -1) - (millis() % LOG_INTERVAL));
  
  digitalWrite(greenLEDpin, HIGH);

  // fetch the time
  now = RTC.now();
  // log time
  logfile.print(now.unixtime()); // seconds since 1/1/1970
  logfile.print(", ");

#if ECHO_TO_SERIAL
  Serial.print(now.unixtime()); // seconds since 1/1/1970
  Serial.print(", ");

#endif //ECHO_TO_SERIAL
  
  //read every second and average over minute
  analogRead(currentAnalog); 
  delay(10);
  int currentAnalogReading = analogRead(currentAnalog);    

  //read every second and average over minute
  analogRead(voltageAnalog); 
  delay(10);
  int voltageAnalogReading = analogRead(voltageAnalog); 

  //read once per minute
  analogRead(usAnalog);
  delay(10); 
  int usAnalogReading = analogRead(usAnalog);  

  flow();
  delay(100);
    
  logfile.print(f);
  logfile.print(", ");
  logfile.print(usAnalogReading);
  logfile.print(", ");    
  logfile.print(currentAnalogReading);
  logfile.print(", ");    
  logfile.print(voltageAnalogReading);
#if ECHO_TO_SERIAL
  Serial.print(f);
  Serial.print(", ");
  Serial.print(usAnalogReading);
  Serial.print(", ");    
  Serial.print(currentAnalogReading);
  Serial.print(", ");    
  Serial.print(voltageAnalogReading);
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

void voltage(){
  
}
