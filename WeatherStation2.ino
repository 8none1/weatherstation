/* Things which have annoyed me:
1.  When the battery level gets low the serial data coming in to the Mini seems to get screwed up
2.  Faster serial speeds seem to have more power demands.

Things to do:

*/


#define _SS_MAX_RX_BUFF 256
#define _SS_MAX_TX_BUFF 256

#include <SoftwareSerial.h>
#include <OneWire.h>
#include <JeeLib.h>
#include <ArduinoJson.h>
#include <digitalIOPerformance.h>
#include <EEPROM.h>

ISR(WDT_vect) { Sleepy::watchdogEvent(); }


boolean debug = true;
boolean wifiIsGo = false;

const byte wifiPwrPin = 9;
const byte oneWireData = 6;
const byte oneWirePwr = 5; 
const byte rainSensor = A0;
const byte soilSensor = A1;
const byte soilPwr = 4;
const byte rainPwr = A2;
const byte lightSensor = A3;
const byte lightPwr = 3;
const byte windSensor = 11;

int cycles = 0; 
byte fails = 0;
int sleepLoops = 30;

byte addr[8];

OneWire ds(oneWireData);
SoftwareSerial SWSerial(7,8); //Rx, Tx 
boolean waitForOK(unsigned long timeout = 5000UL);

void setup(){
  //EEPROM.write(0,0); // REMEMBER TO DELETE THIS LINE ONCE ITS BEEN UPLOADED
  pinMode(wifiPwrPin, OUTPUT);
  pinMode(oneWirePwr, OUTPUT);
  digitalWrite(oneWirePwr, HIGH); // Seems to need time to be discovered and powered up
  pinMode(rainPwr, OUTPUT);
  pinMode(soilPwr, OUTPUT);  
  pinMode(lightPwr, OUTPUT);
  pinMode(windSensor, INPUT_PULLUP);
  Serial.begin(19200);
  SWSerial.begin(19200);
  fails = EEPROM.read(0);
  logger(F("Initialised."));
  Serial.println(F("MODEM LINE"));
}

void loop(){
  if (fails > 10) sleepforADay();
  boolean wifiStatus = false;
  digitalWrite(oneWirePwr, HIGH);
  cycles++;
  if (!ds.search(addr)) {
    ds.reset_search();
  };
  int soil = getAnalogue(soilPwr, soilSensor);
  logger("Soil: "+String(soil));
  
  int rain = getAnalogue(rainPwr, rainSensor);
  logger("Rain: "+String(rain));
 
  int light = getAnalogue(lightPwr, lightSensor);
  logger("Light: "+String(light));
  
  long vbat = readVcc();
  logger("Voltage: "+String(vbat));

  int curTemp = gettemp();
  logger("Current temp: "+String(curTemp));
  
  float w = measureWind();
  logger("Wind: "+String(w));
  
  boolean sendData = false;
  
  wifiPwr(true);
  for (byte x=0; x <= 5;x++){
    if (wifiIsGo == false) {
      logger(F("Wifi not yet ready.  Setting up ESP for networking..."));
      wifiStatus = connectToServer();
    } else {
      logger(F("Wifi is now ready."));
      wifiStatus = true;
    }

    if (wifiStatus == true){
      logger(F("Attempting to transmit data over network..."));
      sendData = sendDataToServer(curTemp, soil, rain, light, vbat, w);
      if (sendData == true) {
        logger(F("Data has been sent..."));
        break;
      }
    } else {
      logger(F("Something went wrong.  Couldn't connect.  Trying again."));
      wifiPwr(false);
      delay(1000);
      wifiPwr(true);
    }
  };
  logger(F("Completed data transmission cycles. Turning off wifi power."));
  wifiPwr(false);
  if (sendData == false) {
    logger(F("I wasn't able to transmit the data this time, incrementing the FAIL counter."));
    writeFails(fails++);
  }
  
  logger(F("Done work.  Going to sleep..."));
  delay(500);
  for (int x=0; x<=sleepLoops; x++){
    Sleepy::loseSomeTime(60000); //65000
  }
  logger(F("Waking up..."));
}

void wifiPwr(boolean state){
  if(state==true){
    logger(F("Switching Wifi power on..."));
    digitalWrite(wifiPwrPin, HIGH);
  } else {
    logger(F("Switching Wifi power off..."));
    digitalWrite(wifiPwrPin, LOW);
  }
}

boolean connectToServer(){
  boolean ok;
  logger(F("Trying to connect to server..."));
  sendModemCommands(F("AT+RST"));
  ok = readModemUntil("ready",10000UL);
  logger(F("Modem is ready..."));
  if (ok==true) {
    //sendModemCommands(F("ATE0"));
    //ok = waitForOK();
    ok = true;
    if (ok==true) {
      sendModemCommands(F("AT+CWMODE=1"));
      ok = waitForOK();
      logger(F("Modem access mode set..."));
      if (ok==true) {
        sendModemCommands(F("AT+CWJAP=\"SSID_IN_HERE\",\"PASSWORD\""));
        ok = waitForOK(8000UL);
        logger(F("Connected to access point..."));
        if (ok==true) {
          sendModemCommands(F("AT+CIPMUX=0"));
          ok = waitForOK(10000UL);
          if (ok==true) {
            sendModemCommands(F("AT+CIPSTART=\"TCP\",\"IP_ADDRESS\",PORT"));
            ok = readModemUntil("CONNECT", 5000UL);
            if (ok==true) {
              logger("Successfully connected to server.");
              wifiIsGo = true;
              return true;
            } else {
              logger("Connect to TCP socket failed.");
            }
          } else {
            logger(F("Set IPMUX failed."));
          }
        } else {
          logger(F("Connect to AP failed."));
        }
      } else {
        logger(F("Set CWMODE failed."));
      }
    } else {
      logger(F("Set echo failed."));
    }
  } else {
    logger(F("Modem power on check failed."));
  }
  // Fell through all the other checks, so we must be broken.
  wifiIsGo = false;
  return false;
};

void logger(String logstr){
  // (DONE)  FIX ME!!!!  Revert back to Software Serial
  if (debug==true){
    SWSerial.println(logstr);
    //Serial.println(logstr);    
  }
}

int getAnalogue(byte pin,byte sensor){ // Change this to fixed values to make use of faster writes
  digitalWrite(pin, HIGH);
  delay(500); // Settle
  int avg = 0;
  for (int x=0; x <=20; x++){
    int a = analogRead(sensor);
    avg += a;
    delay(100);
  };
  digitalWrite(pin, LOW);
  return (avg/20);  
}

int gettemp(){
  byte i;
  byte present = 0;
  byte data[12];
  int HighByte, LowByte, TReading, SignBit, Tc_100;

  ds.reset();
  ds.select(addr);
  ds.write(0x44,1);
  delay(1000);
  present = ds.reset();
  ds.select(addr);
  ds.write(0xBE);
  for (i=0;i<9;i++) {
    data[i] = ds.read();
  }
  digitalWrite(oneWirePwr, LOW);  
  LowByte = data[0];
  HighByte = data[1];
  TReading = (HighByte << 8) + LowByte;
  SignBit = TReading & 0x8000;  // test most sig bit
  if (SignBit) // negative
  {
    TReading = (TReading ^ 0xffff) + 1; // 2's comp
  }
  Tc_100 = (6 * TReading) + TReading / 4;    // multiply by (100 * 0.0625) or 6.25
  if (SignBit) {
    Tc_100 = Tc_100 * -1;
  };
  return Tc_100;
}

long readVcc(){
  // Actually, this is pretty useless.  It reads the voltage from the on board regulator
  // which, unsurprisingly, outputs a stead 3.3v (give or take).  But it's cheap to run
  // so whatevs.
  // Read 1.1V reference against AVcc
  // set the reference to Vcc and the measurement to the internal 1.1V reference
#if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
  ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
  ADMUX = _BV(MUX5) | _BV(MUX0);
#elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
  ADMUX = _BV(MUX3) | _BV(MUX2);
#else
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
#endif  

  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Start conversion
  while (bit_is_set(ADCSRA,ADSC)); // measuring

  uint8_t low  = ADCL; // must read ADCL first - it then locks ADCH  
  uint8_t high = ADCH; // unlocks both

  long result = (high<<8) | low;

  result = 1125300L / result; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
  return result; // Vcc in millivolts
}

float measureWind(){
  logger(F("Measuring wind speed in MPH:"));
  float pulses = 0.0;
  unsigned long duration = 1000UL * 10UL;
  unsigned long startTime = millis();
  boolean oldState = digitalRead(windSensor);
  boolean newState = oldState;
  
  while (millis() - startTime < duration || millis() < startTime) {
    newState = digitalRead(windSensor);
    if (newState != oldState) {
      oldState = newState;
      pulses++;
      delay(10); //debounce a bit
    }
  }
  float metersPerSec = ((pulses / 4.0) * 1.26) / (float) 10.0; // 1.26M per rotation, 4 pulses per rotation
  float mph = metersPerSec * 2.236;
  return mph;
}

boolean sendDataToServer(int curTemp, int soil, int rain, int light, long vbat, float w) {
  boolean ok;
  StaticJsonBuffer<120> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  root["sensor"] = "WeatherStation";
  root["temp"] = curTemp;
  root["soil"] = soil;
  root["rain"] = rain;
  root["light"] = light;
  root["wind"] = w;
  root["cycles"] = cycles;
  root["fails"] = fails;
  root["vbat"] = vbat;  
  char buffer[120];
  root.printTo(buffer, sizeof(buffer));
  String jsonStr(buffer);
  String cmd = "AT+CIPSEND="+String(jsonStr.length());
  sendModemCommands(cmd);
  ok = readModemUntil(">", 10000UL);
  if (ok==true) {
    logger(F("Sending JSON data..."));
    sendModemCommands(jsonStr);
    ok = readModemUntil("SEND OK", 10000UL);    
    if (ok==true){
        logger(F("SUCCESS - Data transmitted"));
        sendModemCommands(F("AT+CIPCLOSE"));
        logger(F("Closing server connection..."));
        ok = readModemUntil("CLOSED", 20000UL); // Closed connection
        
        if (fails > 0){
          // Only write to EEPROM if necessary.  Saves wear on the chip
          fails = 0;
          writeFails(fails);
        }
        return true;
      } else {
        logger(F("Failed to send data. No SEND OK received."));
        sendModemCommands(F("AT+CIPCLOSE"));
        ok = readModemUntil("CLOSED", 10000UL);
        wifiIsGo = false;
        return false;
      }
    } else {
      logger(F("Failed to send data. No prompt."));
      sendModemCommands(F("AT+CIPCLOSE"));
      ok = readModemUntil("CLOSED", 10000UL);
      wifiIsGo = false;
      return false;
    }
};

void sendModemCommands(String data){
  // A convenience function so that we have only one place to change the
  // serial port being used for sending data to the modem.
  Serial.println(data);
}

boolean readModemUntil(String waitfor, unsigned long timeout){
  char character;
  String buffer;
  String mainString;
  char ssbuf[100];
  int count = 0;
  unsigned long endtime = millis() + timeout;
  unsigned long starttime = millis();
  
  while (millis() < endtime && starttime <= millis()) {
    if (Serial.available() > 0) {
      // There is data, is it what we are looking for?
      while (Serial.available() > 0 && count < 100) {
        character = Serial.read();
        if (character > 31 && character < 128) ssbuf[count++]=character; // Might be unnecessary load?
      };
      ssbuf[count]='\0';
      String buffer(ssbuf);
      mainString.concat(buffer);
      if (mainString.indexOf(waitfor) >=0 ){        
        return true; // Found the string, done.
      };
      count = 0;
      };
    };
    logger("ERROR - string not found: "+waitfor);
    return false;
  };  
  
boolean waitForOK(unsigned long timeout){
  return readModemUntil("OK", timeout);
}

byte getFails(){
  fails = EEPROM.read(0);
}

void writeFails(byte fails){
  EEPROM.write(0,fails);
}

void sleepforADay(){
  /* AKA The Death Spiral
  If the power gets too low the modem goes screwy and doesnt send the data.  The causes
  the code to try again and again, using more power, making the screwiness worse.
  This is supposed to catch that situation and put everything to sleep for a day to try
  and let the batteries charge enough to get a transmission through.
  */
  for (int x=0; x<=1440; x++){
    Sleepy::loseSomeTime(65000); //max is ~ 65000
  }
  fails = 0;
  writeFails(0);
}
