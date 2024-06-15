/*
   Written by W. Hoogervorst
   Main electricity power meter
   jan 2018, last update june 2024
*/
#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <credentials.h>  //mySSID, myPASSWORD, mqtt_server
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>

// for HTTPupdate
const char* host = "main_power_meter";
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;
const char* software_version = "version 13";

// define times
#define SECOND 1e3          // 1e3 ms is 1 second
#define MINUTE SECOND * 60  // 60e3 ms is 60 seconds = 1 minute
#define HOUR MINUTE * 60    // number of millis in an hour
#define MEASUREMENT MINUTE

#define PULSE_MAX_LENGTH 45  // actual LED pulse of power meter is 38 ms
#define PULSE_MIN_LENGTH 25  // actual LED pulse of power meter is 38 ms
#define TOTALDELAY 1000      // publish value of counter every ## counts
#define WIFI_CONNECT_TIMEOUT_S 10
#define MQTT_RECONNECT_DELAY 10
#define INPUTPIN 1
#define DEBUGPIN 3
#define LEDPIN 0
#define PULSETIME 100

WiFiClient espClient;
PubSubClient client(espClient);

// global variables
uint64_t time_elapsed, time1, time2, pulsebegin, measurementbegin = 0, pulselength, lastpulse, lastReconnectAttempt = 0;
uint16_t pulsecount = 0;
uint32_t totalcounter = 0, blinktimer;
boolean pulsestate = false;   // false: not detecting a pulse
boolean pulse_error = false;  // error in detecting, no counting
boolean measurement = false;  // not measuring yet
boolean debug = false;        // if true debug messages are published via MQTT
int hourcount = 0, powerW;

String tmp_str;  // String for publishing the int's as a string to MQTT
char buf[5];

void setup() {
  WiFi.setOutputPower(0);  // 0 is lowest, 20.5 is highest (http://www.esp8266.com/viewtopic.php?f=32&t=13496)
  WiFi.mode(WIFI_STA);
  pinMode(LEDPIN, OUTPUT);
  pinMode(INPUTPIN, INPUT);

  // for HTTPupdate
  MDNS.begin(host);
  httpUpdater.setup(&httpServer);
  httpServer.begin();
  MDNS.addService("http", "tcp", 80);
  httpServer.on("/", handleRoot);

  time1 = now();
  client.setServer(mqtt_server, 1883);
  reconnect();
}

void loop() {
  httpServer.handleClient();  // for HTTPupdate
  client.loop();
  if (!client.connected()) {
    if (now() > lastReconnectAttempt + MQTT_RECONNECT_DELAY) {
      lastReconnectAttempt = now();
      // Attempt to reconnect
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  }

  if (digitalRead(DEBUGPIN) == HIGH) {  // switch on GPIO3 determines the state
    debug = true;
  } else {
    debug = false;
  }

  if (pulsestate == false) {
    if (digitalRead(INPUTPIN) == LOW)  // a pulse started
    {
      pulsestate = true;
      pulsebegin = millis();
    }
  }

  if (pulsestate == true && digitalRead(INPUTPIN) == HIGH)  // pulse ended
  {
    pulselength = millis() - pulsebegin;
    if (pulselength < PULSE_MIN_LENGTH || pulselength > PULSE_MAX_LENGTH)  // pulse too short  or too long
    {
      pulsestate = false;
      pulsecount = 0;
      measurement = false;
    }

    else  // this was a good pulse
    {
      if (measurement == false) {
        measurementbegin = millis();  // start the measurement period
        measurement = true;           // we are in a measurement
      }
      pulsestate = false;  // end pulse state
      pulsecount++;        // increase counters
      totalcounter++;
      lastpulse = millis();  // measure te moment of the last pulse in the measurement
      if (debug)             // publish debug messages
      {
        int pulsetemp = pulselength;
        tmp_str = String(pulsetemp);  //converting count to a string
        tmp_str.toCharArray(buf, tmp_str.length() + 1);
        client.publish("ESP-01_2/pulselength", buf);
        client.publish("ESP-01_2/state", "total counts:");
        tmp_str = String(totalcounter);  //converting int to a string
        tmp_str.toCharArray(buf, tmp_str.length() + 1);
        client.publish("ESP-01_2/state", buf);
      }
      if (totalcounter % TOTALDELAY == 0)  // publish value every ## times
      {
        client.publish("ESP-01_2/state", "total counts:");
        tmp_str = String(totalcounter);  //converting count to a string
        tmp_str.toCharArray(buf, tmp_str.length() + 1);
        client.publish("ESP-01_2/state", buf);
      }
      digitalWrite(LEDPIN, HIGH);  // start blinking of led
      blinktimer = millis();
    }
  }
  if (digitalRead(LEDPIN) == HIGH && millis() > (blinktimer + PULSETIME)) // end blink of LED
    digitalWrite(LEDPIN, LOW);

  if (millis() < measurementbegin)  // millis() was resetted to 0 after 2^32 ms = 49,7 days
  {
    pulsecount = 0;
    measurement = false;
  }
  if (measurement == true && millis() > (measurementbegin + MEASUREMENT)) {
    if (debug)  // publish debug messages
    {
      client.publish("ESP-01_2/state", "measurement ended");
    }
    tmp_str = String(pulsecount);  //converting count to a string
    tmp_str.toCharArray(buf, tmp_str.length() + 1);
    client.publish("ESP-01_2/count", buf);

    // new calculation
    time_elapsed = lastpulse - measurementbegin;                             // the last detected pulse defines the actual measurement period
    hourcount = (pulsecount - 1) * ((HOUR) / (time_elapsed - pulselength));  //this gives the power per hour, the last pulse must be substracted, because we should measure until the beginning of the pulse, 
    powerW = hourcount/(2000/1000);                                         // my power meter gives 2000 pulses per kWh (1000 Wh) 
    tmp_str = String(powerW);                                             //converting number to a string
    tmp_str.toCharArray(buf, tmp_str.length() + 1);
    //client.publish("ESP-01_2/state", buf);
    client.publish("sensor/powerW", buf);

    // old calculation
    // extrapolate measurement period to one hour
    //hourcount = pulsecount * ((HOUR) / (MEASUREMENT));

    // calculate power in kW
    // client.publish("ESP-01/input", "Power (W):");
    //tmp_str = String(hourcount); //converting number to a string
    //tmp_str.toCharArray(buf, tmp_str.length() + 1);
    // client.publish("ESP-01_2/state", buf);
    // client.publish("sensor/powerW_old", buf);

    pulsecount = 0;
    measurement = false;
  }
}

boolean reconnect() {
  if (WiFi.status() != WL_CONNECTED) {  // check if WiFi connection is present
    WiFi.begin(mySSID, myPASSWORD);
    time1 = now();
    while (WiFi.status() != WL_CONNECTED) {
      delay(50);
      time2 = now();
      if ((time2 - time1) > WIFI_CONNECT_TIMEOUT_S)  // wifi connection lasts too long
      {
        break;
        ESP.restart();
      }
    }
  }
  client.connect(host);
  client.publish("ESP-01_2/state", "Connected");
  return client.connected();
}

void handleRoot() {
  String message = "WimIOT\nDevice: ";
  message += host;
  message += "\nSoftware version: ";
  message += software_version;
  message += "\nCurent power: ";
  message += powerW;
  message += " W";
  message += "\nUpdatepath at http://[IP]/update";
  httpServer.send(200, "text/plain", message);
}
