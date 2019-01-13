#include <Arduino.h>
#include <Stream.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

//AWS
#include "sha256.h"
#include "Utils.h"

//WEBSockets
#include <Hash.h>
#include <WebSocketsClient.h>

//MQTT PAHO
#include <SPI.h>
#include <IPStack.h>
#include <Countdown.h>
#include <MQTTClient.h>

#include <Servo.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Ambient.h>
#include <ArduinoJson.h>

//AWS MQTT Websocket
#include "Client.h"
#include "AWSWebSocketClient.h"
#include "CircularByteBuffer.h"

extern "C" {
  #include "user_interface.h"
}

#define DHTPIN 4
#define DHTTYPE DHT22
#define JST     3600*9
#define SERVOPIN 2

//AWS IOT config, change these:
char wifi_ssid[] = "xxxx";
char wifi_password[] = "xxxx";
char aws_endpoint[] = "xxxx-ats.iot.ap-northeast-1.amazonaws.com";
char aws_key[] = "xxxx";
char aws_secret[] = "xxxx";
char aws_region[] = "ap-northeast-1";
const char* aws_topic = "kamekusa/DHT22";
const char* aws_topic_2 = "kamekusa/SG909G";
int port = 443;

//MQTT config
const int maxMQTTpackageSize = 512;
const int maxMQTTMessageHandlers = 1;

ESP8266WiFiMulti WiFiMulti;
WiFiClient wifi;

AWSWebSocketClient awsWSclient(1000);

IPStack ipstack(awsWSclient);
MQTT::Client<IPStack, Countdown, maxMQTTpackageSize, maxMQTTMessageHandlers> client(ipstack);

//ambient
DHT dht(DHTPIN, DHTTYPE);
Servo kameServo;

int pos = 0;

const int AMBIENT_CHANNEL_ID = 8672;
const char* AMBIENT_WRITE_KEY = "xxxx";

Ambient ambient;

//# of connections
long connection = 0;

//generate random mqtt clientID
char* generateClientID () {
  char* cID = new char[23]();
  for (int i=0; i<22; i+=1)
    cID[i]=(char)random(1, 256);
  return cID;
}

void setup() {
    kameServo.attach(SERVOPIN);
    wifi_set_sleep_type(NONE_SLEEP_T);
    Serial.begin (115200);
    delay (2000);
    Serial.setDebugOutput(1);

    //fill with ssid and wifi password
    WiFiMulti.addAP(wifi_ssid, wifi_password);
    Serial.println ("connecting to wifi");
    while(WiFiMulti.run() != WL_CONNECTED) {
        delay(100);
        Serial.print (".");
    }
    Serial.println ("\nconnected");


    int timeSinceLastRead = 10;
    float h;
    float t;
    float f;
    while(timeSinceLastRead > 0) {
      // Reading temperature or humidity takes about 250 milliseconds!
      // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
      h = dht.readHumidity();
      // Read temperature as Celsius (the default)
      t = dht.readTemperature();
      // Read temperature as Fahrenheit (isFahrenheit = true)
      f = dht.readTemperature(true);
      if(isnan(h) || isnan(t)) {
        Serial.println("Failed to read from DHT sensor!");
        timeSinceLastRead -= 1;
        delay(500);
      }else{
        timeSinceLastRead = 0;
      }
    }

    // Compute heat index in Fahrenheit (the default)
    float hif = dht.computeHeatIndex(f, h);
    // Compute heat index in Celsius (isFahreheit = false)
    float hic = dht.computeHeatIndex(t, h, false);

    Serial.print("Humidity: ");
    Serial.print(h);
    Serial.print(" %\t");
    Serial.print("Temperature: ");
    Serial.print(t);
    Serial.print(" *C ");
    Serial.print(f);
    Serial.print(" *F\t");
    Serial.print("Heat index: ");
    Serial.print(hic);
    Serial.print(" *C ");
    Serial.print(hif);
    Serial.println(" *F");

    //fill AWS parameters    
    awsWSclient.setAWSRegion(aws_region);
    awsWSclient.setAWSDomain(aws_endpoint);
    awsWSclient.setAWSKeyID(aws_key);
    awsWSclient.setAWSSecretKey(aws_secret);
    awsWSclient.setUseSSL(true);

    send_am(h, t, f, hic, hif);
    delay (100);
    sendmessage (h,t);
    Serial.println("Reported!: ");
  
    if (connect ()){
      subscribe ();
    }
}

//connects to websocket layer and mqtt layer
bool connect () {

    if (client.isConnected ()) {    
        client.disconnect ();
    }  
    //delay is not necessary... it just help us to get a "trustful" heap space value
    delay (1000);
    Serial.print (millis ());
    Serial.print (" - conn: ");
    Serial.print (++connection);
    Serial.print (" - (");
    Serial.print (ESP.getFreeHeap ());
    Serial.println (")");

   int rc = ipstack.connect(aws_endpoint, port);
    if (rc != 1)
    {
      Serial.println("error connection to the websocket server");
      return false;
    } else {
      Serial.println("websocket layer connected");
    }

    Serial.println("MQTT connecting");
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 4;
    char* clientID = generateClientID ();
    data.clientID.cstring = clientID;
    rc = client.connect(data);
    delete[] clientID;
    if (rc != 0)
    {
      Serial.print("error connection to MQTT server");
      Serial.println(rc);
      return false;
    }
    Serial.println("MQTT connected");
    return true;
}

//count messages arrived
int arrivedcount = 0;

//callback to handle mqtt messages
void messageArrived(MQTT::MessageData& md)
{
  MQTT::Message &message = md.message;

  Serial.print("Message ");
  Serial.print(++arrivedcount);
  Serial.print(" arrived: qos ");
  Serial.print(message.qos);
  Serial.print(", retained ");
  Serial.print(message.retained);
  Serial.print(", dup ");
  Serial.print(message.dup);
  Serial.print(", packetid ");
  Serial.println(message.id);
  Serial.print("Payload ");
  char* msg = new char[message.payloadlen+1]();
  memcpy (msg,message.payload,message.payloadlen);
  Serial.print("[");
  Serial.print(msg);
  Serial.println("]");
  pos = atoi(msg);
  servo_move(pos);
  delete msg;
}

//subscribe to a mqtt topic
void subscribe () {
   int rc = client.subscribe(aws_topic_2, MQTT::QOS1, messageArrived);
   if (rc != 0) {
      Serial.print("rc from MQTT subscribe is ");
      Serial.println(rc);
      return;
   }
   Serial.println("MQTT subscribed");
}

//send a message to a mqtt topic
void sendmessage (double humidity, double tempC) {
    time_t t = time(NULL);
    unsigned long seconds = (unsigned long) t;
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    root["id"] = "id" + String(seconds);
    root["expire"] = seconds + 48 * 60 * 60;
    root["d1"] = String(tempC);
    root["d2"] = String(humidity);
    char buf[100];
    root.printTo(buf);
    Serial.println(buf);
  
    //send a message
    MQTT::Message message;
    //strcpy(buf, "{\"state\":{\"reported\":{\"on\": false}, \"desired\":{\"on\": false}}}");
    message.qos = MQTT::QOS0;
    message.retained = false;
    message.dup = false;
    message.payload = (void*)buf;
    message.payloadlen = strlen(buf)+1;
    int rc = client.publish(aws_topic, message); 
}

void send_am(double humidity, double tempC, double tempF, double heatIndexC, double heatIndexF) {
  char humidbuf[12];
  Serial.println("ambient...");
  
  ambient.begin(AMBIENT_CHANNEL_ID, AMBIENT_WRITE_KEY, &wifi);
  ambient.set(1, tempC);
  dtostrf(humidity, 3, 1, humidbuf);
  ambient.set(2, humidity);
  ambient.send();

  Serial.println("... sended.");
}

void servo_move(int p){
  Serial.print("angle: ");
  Serial.println(p);

  // set the servo position
  kameServo.writeMicroseconds(p);
  delay(1000);
}

void loop() {
  int sleepTime = 1000 * 60 * 5; //5 minutes

  while(sleepTime > 0) {  
    sleepTime -= 1000 * 15;
    Serial.println(".");

    //keep the mqtt up and running
    if (awsWSclient.connected ()) {    
      client.yield(50);
    } else {
      //handle reconnection
      if (connect ()){
        subscribe ();      
      }
    }
    
    delay(1000 * 15);
  }

  //ESP.deepSleep(sleepTime * 1000);
  //ESP.restart();
}
