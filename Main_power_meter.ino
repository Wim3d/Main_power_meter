/*
   Written by W. Hoogervorst
   Main electricity power meter
   jan 2018
*/
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <credentials.h>      //mySSID, myPASSWORD

// define times
#define SECOND 1e3  // 1e3 ms is 1 second
#define MINUTE SECOND*60  // 60e3 ms is 60 seconds = 1 minute
#define HOUR MINUTE*60 // number of millis in an hour
#define DAY HOUR*24 // number of millis in a day
#define MEASUREMENT MINUTE

#define PULSE_MAX_LENGTH 45   // actual LED pulse of power meter is 38 ms
#define PULSE_MIN_LENGTH 25   // actual LED pulse of power meter is 38 ms
#define TOTALDELAY 1000        // publish value of counter every ## counts
#define WIFI_CONNECT_TIMEOUT_S 10
#define MQTT_RECONNECT_DELAY 10000
#define INPUTPIN 1
#define DEBUGPIN 3
#define LEDPIN 0

const char* mqtt_server = "192.168.10.104";

WiFiClient client;
PubSubClient MQTTclient(client);

// global variables
uint32_t  time_elapsed;
uint64_t time1, time2, pulsebegin, measurementbegin, lastpulse, lastReconnectAttempt = 0;
uint16_t pulselength, pulsecount = 0;
uint32_t totalcounter = 0;
boolean pulsestate = false; // false: not detecting a pulse
boolean measurement = false;   // not measuring yet
boolean debug = false;        // switched by mechanical switch, if true debug messages are published via MQTT

String tmp_str; // String for publishing the int's as a string to MQTT
char buf[5];

void setup() {
  WiFi.setOutputPower(0); // 0 is lowest (close to AP), 20.5 is highest (http://www.esp8266.com/viewtopic.php?f=32&t=13496)
  WiFi.mode(WIFI_STA);
  pinMode(LEDPIN, OUTPUT);
  pinMode(INPUTPIN, INPUT);

  time1 = millis();
  MQTTclient.setServer(mqtt_server, 1883);

  WiFi.begin(mySSID, myPASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(50);
    time2 = millis();
    if (((time2 - time1) / 1000) > WIFI_CONNECT_TIMEOUT_S)  // wifi connection lasts too long
    {
      break;
    }
  }
  MQTTclient.connect("ESP-01_2-Client");
  MQTTclient.publish("ESP-01_2/state", "Up and running");
}

void loop()
{
  if (!client.connected())
  {
    if (millis() > lastReconnectAttempt + MQTT_RECONNECT_DELAY)
    {
      lastReconnectAttempt = millis();
      // Attempt to reconnect
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  }

  if (digitalRead(DEBUGPIN) == HIGH) {      // switch on GPIO3 determines the state of 'debug'
    debug = true;
  }
  else {
    debug = false;
  }

  if (pulsestate == false)
  {
    if (digitalRead(INPUTPIN) == LOW)   // a pulse started
    {
      pulsestate = true;
      pulsebegin = millis();
    }
  }

  if (pulsestate == true && digitalRead(INPUTPIN) == HIGH)    // pulse ended
  {
    pulselength = millis() - pulsebegin;
    if (pulselength <  PULSE_MIN_LENGTH || pulselength > PULSE_MAX_LENGTH) // pulse too short  or too long
    {
      pulsestate = false;
    }

    else    // this was a good pulse
    {
      if (measurement == false)
      {
        measurementbegin = millis();           // start the measurement period
        measurement = true;                    // we are in a measurement
      }
      pulsestate = false;         // end pulse state
      pulsecount++;               // increase counters
      totalcounter++;
      lastpulse = millis();       // measure te moment of the last pulse in the measurement
      if (debug)      // publish debug messages?
      {
        tmp_str = String(pulselength); //converting count to a string
        tmp_str.toCharArray(buf, tmp_str.length() + 1);
        MQTTclient.publish("ESP-01_2/pulselength", buf);
        MQTTclient.publish("ESP-01_2/state", "total counts:");
        tmp_str = String(totalcounter); //converting int to a string
        tmp_str.toCharArray(buf, tmp_str.length() + 1);
        MQTTclient.publish("ESP-01_2/state", buf);
      }
      if (totalcounter % TOTALDELAY == 0)      // publish value every ## times as status update
      {
        MQTTclient.publish("ESP-01_2/state", "total counts:");
        tmp_str = String(totalcounter); //converting count to a string
        tmp_str.toCharArray(buf, tmp_str.length() + 1);
        MQTTclient.publish("ESP-01_2/state", buf);
      }
      digitalWrite(LEDPIN, HIGH);     // blink led
      delay(100);
      digitalWrite(LEDPIN, LOW);
    }

  }
  if (measurement == true && millis() > (measurementbegin + MEASUREMENT))
  {
    if (debug)      // publish debug messages
    {
      MQTTclient.publish("ESP-01_2/state", "measurement ended");
    }
    tmp_str = String(pulsecount); //converting count to a string
    tmp_str.toCharArray(buf, tmp_str.length() + 1);
    MQTTclient.publish("ESP-01_2/count", buf);

    // new calculation
    time_elapsed = lastpulse - measurementbegin;  // the last detected pulse defines the actual measurement period
    int hourcount = (pulsecount - 1) * ((HOUR) / (time_elapsed - pulselength));      //the last pulse must be substracted, because we should measure until the beginning of the pulse
    tmp_str = String(hourcount); //converting number to a string
    tmp_str.toCharArray(buf, tmp_str.length() + 1);
    //MQTTclient.publish("ESP-01_2/state", buf);
    MQTTclient.publish("sensor/powerW", buf);

    // clean up values
    pulsecount = 0;
    measurement = false;
  }
}

boolean reconnect()
{
  if (WiFi.status() != WL_CONNECTED) {    // check if WiFi connection is present
    WiFi.begin(mySSID, myPASSWORD);
    time1 = millis();
    while (WiFi.status() != WL_CONNECTED) {
      delay(50);
      time2 = millis();
      if (((time2 - time1) / 1000) > WIFI_CONNECT_TIMEOUT_S)  // wifi connection lasts too long
      {
        break;
      }
    }
  }
  MQTTclient.connect("ESP-01_2-Client");
  return client.connected();
}


