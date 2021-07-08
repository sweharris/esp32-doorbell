// A normal doorbell has a simple circuit that's basically an AC power
// (maybe 16V) supply connected to a button connected to a solenoid that
// throws a hammer to hit a chime.  When you press the button the circuit
// is closed and the chime rings
//
// Now this can't be read directly by Arduino/ESP8266/ESP32 type devices
// because of the 16V AC supply.
//
// But what we _can_ do is detect the current flow with a simple current
// sensor.   With a device like a CT103C you can run one of the cables
// through the sensor.  It outputs a signal proportional to the current,
// and this can be read by an analog port.
//
// Many homes have two doorbells (front and back).  These are effectively
// two different circuits, so you need two sensors and two ADC ports.  If
// you also need WiFi that pretty much means an ESP32 (or an Arduino with
// a WiFi hat).  I went down the ESP32 route.
//
// This sketch also has "hidden" hooks for OTA updates via HTTP,
// based off my example sketch
//   https://github.com/sweharris/esp8266-example
// but minor modifications for ESP32 (since the libraries are different)
//
// When a button is pressed a message is sent to the MQTT server
//
// The MQTT channels are based off the word "doorbell" and the last 6 digits
// of the MAC
//   eg doorbell/123456/front_door
//      doorbell/123456/back_door
//
// This message can be used as a trigger for HomeAssistant, for example.

// ESP32 ADCs.  A6 is GPIO34, A7 is GPIO35.  These can both be
// used at the same time as WiFi

#define back_doorbell A6
#define front_doorbell A7

// Prevent multiple calls incase the switch doesn't make/break cleanly.
// Also stop aggressive bell ringers
#define debounce 1000

// I was getting pretty good readings 0 and 100+ values
// so let's set the threshold to 50
#define bell_threshold 50

#include <WiFi.h>
#include <PubSubClient.h>
#include "network_conn.h"

#define _mqttBase    "doorbell"

// For MQTT
WiFiClient espClient;
PubSubClient client(espClient);

char mqttBackdoor[50];  // Enough for Base + "/" + 6 hex digits + "/" + channel
char mqttFrontdoor[50];
char mqttDebug[50];

// Generate log messages on the serial port
void log_msg(String msg)
{
  time_t now = time(nullptr);
  String tm = ctime(&now);
  tm.trim();
  tm = tm + ": " + msg;
  Serial.println(tm);
}

// To hold part of the MAC address
char macstr[7];

void setup()
{
  // Let's create the channel names based on the MAC address
  unsigned char mac[6];
  WiFi.macAddress(mac);
  sprintf(macstr, "%02X%02X%02X", mac[3], mac[4], mac[5]);
  sprintf(mqttBackdoor,  "%s/%s/back_doorbell",  _mqttBase, macstr);
  sprintf(mqttFrontdoor, "%s/%s/front_doorbell", _mqttBase, macstr);
  sprintf(mqttDebug,     "%s/%s/debug",          _mqttBase, macstr);

  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);     // Connect to the network
  Serial.print("Connecting to ");
  Serial.print(ssid);
  Serial.println(" ...");

  int i = 0;
  while (WiFi.status() != WL_CONNECTED) // Wait for the Wi-Fi to connect
  {
    delay(1000);
    Serial.print(++i); Serial.print(' ');
  }

  Serial.println('\n');
  Serial.println("Connection established!");  
  Serial.print("IP address:\t");
  Serial.println(WiFi.localIP());
  Serial.print("Hostname:\t");
  Serial.println(WiFi.getHostname());

  // Get the current time.  Initial log lines may be wrong 'cos
  // it's not instant.  Timezone handling... eh, this is just for
  // debuging, so I don't care.  GMT is always good :-)
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  delay(1000);

  // Now we're on the network, setup the MQTT client
  client.setServer(mqttServer, mqttPort);

  pinMode(back_doorbell, INPUT);
  pinMode(front_doorbell, INPUT);

#ifdef NETWORK_UPDATE
   __setup_updater();
#endif

}

void ring_bell(String bell, char *channel, int senseDoorbell)
{
  client.publish(channel, "pressed");

  time_t now = time(nullptr);
  String tm = bell + " Ding Dong at ";
  tm += ctime(&now);
  tm.trim();
  tm +=  " -- " + String(senseDoorbell);
  client.publish(mqttDebug, tm.c_str());
  log_msg(tm);
}


int senseDoorbell = 0;
unsigned long prevRing = 0;

void loop()
{
  // Try to reconnect to MQTT each time around the loop, in case we disconnect
  while (!client.connected())
  {
    log_msg("Connecting to MQTT Server " + String(mqttServer));

    // Generate a random ID each time
    String clientId = "ESP32Client-" + String(macstr) + "-";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str()))
    {
      log_msg("MQTT connected.  I am " + WiFi.localIP().toString());
    }
    else
    {
      log_msg("failed with state " + client.state());
      delay(2000);
    }
  }

  // only check the doorbell if it hasn't been hit in the last second
  if(millis() - prevRing >= debounce)
  {
    senseDoorbell = analogRead(back_doorbell);

    if(senseDoorbell > bell_threshold)
    {
      ring_bell("Back", mqttBackdoor, senseDoorbell);
      prevRing = millis();
    }

    senseDoorbell = analogRead(front_doorbell);
    if(senseDoorbell > bell_threshold)
    {
      ring_bell("Front", mqttFrontdoor, senseDoorbell);
      prevRing = millis();
    }
  }

#ifdef NETWORK_UPDATE
  __netupdateServer.handleClient();
#endif

  // Keep MQTT alive
  client.loop();
}
