#include <TinyGPS++.h>
#include <SoftwareSerial.h>
#include <ArduinoJson.h>
#include <Crypto.h>
#include <SHA256.h>
// SIM800L & MQTT
#define TINY_GSM_MODEM_SIM800
#include <TinyGsmClient.h>
#include <PubSubClient.h>
// DHT
#include "DHT.h"

/****************************** MQTT defines *********************************/
const char* broker = "test.mosquitto.org";

const char* topicInit = "testMQTT_SIM800";

/******************************* Test Cases **********************************/
#define MAX_ENTRIES 6 //Case 1 (10/60) Collect every 10sec, send every 1min
//#define MAX_ENTRIES 10 //Case 2 (60/600) Collect every 60sec, send every 10min
//#define MAX_ENTRIES 6 //Case 3 (600/3600) Collect every 600sec, send every 60min

/************************* GPS defines ***************************************/
TinyGPSPlus gps;

/************************* DHT defines ***************************************/
#define DHTTYPE DHT11
#define DHT11_PIN 2 //D4

/************************* SIM800 defines ***************************************/
#define RXPinSIM 13 //D7 yellow to TX of the SIM800
#define TXPinSIM 15 //D8 green to RX of the SIM800

// Set serial for AT commands (to the module)
SoftwareSerial SerialAT(RXPinSIM, TXPinSIM); // RX, TX
TinyGsm modem(SerialAT);
TinyGsmClient client(modem);
PubSubClient mqtt(client);

// set GSM PIN, if any
#define GSM_PIN ""

// Your GPRS credentials, if any
const char apn[] = "orangeworld";
const char gprsUser[] = "";
const char gprsPass[] = "";

// Initialize dht sensor with its pin
DHT dht(DHT11_PIN, DHTTYPE);

// Struct for json MQTT message
struct GPSData {
    uint32_t timestamp;
    float latitude;
    float longitude;
    float humid;
    float temp;
    float altitude;
    float speed;
};

String IMEI = "";

GPSData gpsBuffer[MAX_ENTRIES];  // buffer for gps data
int dataCount = 0;  // Count of gps data collected

void MQTT_connect();

void setup() {
  // Serial
  Serial.begin(9600);
  SerialAT.begin(9600);

  delay(3000); //Give time to the modem
  // Init the modem
  Serial.println("Initializing modem...");
  modem.restart();

  String modemInfo = modem.getModemInfo();
  Serial.print("Modem Info: ");
  Serial.println(modemInfo);

  // Get in the network
  Serial.print("Waiting for network...");
  if (!modem.waitForNetwork()) {
    Serial.println(" fail");
    delay(10000);
    return;
  }
  Serial.println(" success");

  if (modem.isNetworkConnected()) {
    Serial.println("Network connected");
  }

  //GPRS connection
  Serial.print(F("Connecting to "));
  Serial.print(apn);
  if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
    Serial.println(" fail");
    delay(10000);
    return;
  }
  Serial.println(" success");

  if (modem.isGprsConnected()) {
    Serial.println("GPRS connected");
  }

  Serial.print("IP address: "); Serial.println(modem.getLocalIP());
  Serial.print("IMEI address: "); Serial.println(modem.getIMEI());
  IMEI = modem.getIMEI();

  // MQTT Broker setup
  mqtt.setServer(broker, 1883);

  // DHT
  dht.begin();
}

// Add new instance to the buffer 
void addGPSData(uint32_t timestamp, float lat, float lon, float hum, float temp, int32_t alt, int32_t speed) {
    if (dataCount < MAX_ENTRIES) {
        gpsBuffer[dataCount].timestamp = timestamp;
        gpsBuffer[dataCount].latitude = lat;
        gpsBuffer[dataCount].longitude = lon;
        gpsBuffer[dataCount].humid = hum;
        gpsBuffer[dataCount].temp = temp;
        gpsBuffer[dataCount].altitude = alt;
        gpsBuffer[dataCount].speed = speed;
        dataCount++;
    }
}

// Generate Json MQTT message
String generateJSON() {
    StaticJsonDocument<2046> doc; 

    doc["sha256"] = "0";
    doc["IMEI"] = IMEI;
    doc["support"][0] = "GPS";
    doc["support"][1] = "TEMP";

    JsonArray dataArray = doc.createNestedArray("data");

    for (int i = 0; i < dataCount; i++) {
        JsonObject entry = dataArray.createNestedObject();
        entry["timestamp"] = String(gpsBuffer[i].timestamp);
        entry["lat"] = String(gpsBuffer[i].latitude, 6);
        entry["long"] = String(gpsBuffer[i].longitude, 6);
        entry["humid"] = String(gpsBuffer[i].humid);
        entry["temp"] = String(gpsBuffer[i].temp);
        entry["altitude"] = String(gpsBuffer[i].altitude, 2);
        entry["speed"] = String(gpsBuffer[i].speed, 2);
    }

    // Json to string
    String jsonString;
    serializeJson(doc, jsonString);

    // Obtain SHA-256 of the json
    String hash = calculateSHA256(jsonString);
    doc["sha256"] = hash;

    // Reserialize with hash in it
    jsonString = "";
    serializeJson(doc, jsonString);
    
    return jsonString;
}

// Calculates the SHA-256 of a string
String calculateSHA256(String input) {
    SHA256 sha256;
    uint8_t hash[sha256.hashSize()];

    sha256.reset();
    sha256.update(input.c_str(), input.length());
    sha256.finalize(hash, sizeof(hash));

    String hashString = "";
    for (int i = 0; i < sizeof(hash); i++) {
        if (hash[i] < 0x10) hashString += "0"; 
        hashString += String(hash[i], HEX);
    }

    return hashString;
}

// Get UNIX timestamp of a date and time
unsigned long dateTimeToUnixTimestamp(uint32_t date, uint32_t time) {
    // Extract date components
    int day   = date / 10000;
    int month = (date / 100) % 100;
    int year  = 2000 + (date % 100); // Assume year 2000+

    // Extract time components
    int hour = time / 1000000;
    int min  = (time / 10000) % 100;
    int sec  = (time / 100) % 100;

    int daysPerMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    
    // Calculate days from 1970-01-01 to actual year
    unsigned long days = 0;
    for (int y = 1970; y < year; y++) {
        days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    }

    // Days until actual month
    for (int m = 0; m < month - 1; m++) {
        days += daysPerMonth[m];
    }

    // Adjust leap years
    if (month > 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
        days += 1;
    }
    days += day - 1;

    // Obtain total seconds
    unsigned long timestamp = days * 86400L + hour * 3600 + min * 60 + sec;
    return timestamp;
}

String json = "";

void loop() {

 // Decode all bytes of the gps read
  while (Serial.available() > 0) {
    gps.encode(Serial.read());
  }

  // Process if the location is new
  if (gps.location.isUpdated()) {
    uint32_t date = gps.date.value();
    uint32_t time = gps.time.value();
    float lat = gps.location.lat();
    float lon = gps.location.lng();
    float humid = dht.readHumidity();
    float temp = dht.readTemperature();
    float alt = gps.altitude.meters();
    float speed = gps.speed.kmph();

    // Check if any reads failed
    if (isnan(humid) || isnan(temp)) {
      Serial.println(F("Failed to read from DHT sensor!"));
    }
    // Check invalid parameters
    if (date > 0 && time > 0 && dateTimeToUnixTimestamp(date, time) > 1000000000 && lat != 0.0 && lon != 0.0) {
      uint32_t timestamp = dateTimeToUnixTimestamp(date, time); 
      addGPSData(timestamp, lat, lon, humid, temp, alt, speed);
      Serial.print("Instancia ");
      Serial.print(dataCount);
      Serial.println(" recolectada.");

      delay(9500); //Case 1 10sec delay
      //delay(59500); //Case 2 60sec delay
      //delay(599500); //Case 3 600sec delay
    }
  }

  // When all GPS data has been collected
  if(dataCount == MAX_ENTRIES){

    
    // Ensure the connection to the MQTT server is alive (this will make the first
    // connection and automatically reconnect when disconnected).  See the MQTT_connect
    // function definition further below.
    MQTT_connect();

    json = generateJSON();
    dataCount = 0;

    // Publish generated json
    Serial.print(F("\nSending GPS data "));
    Serial.println(json);
    uint16_t len = mqtt.publish(topicInit, json.c_str());
    if (len == 0) {
      Serial.println(F("Failed"));
    } else {
      Serial.print(F("Tamaño de paquete enviado: "));
      Serial.println(len);
    }
  }
} 

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
void MQTT_connect() {

  if (mqtt.connected()) return;

  Serial.println("Verificando red...");

  // Check the phone network
  if (!modem.isNetworkConnected()) {
    Serial.println("Red no conectada. Reintentando...");
    if (!modem.waitForNetwork()) {
      Serial.println("Error al reconectar red");
      delay(10000);
      return; //Retry on next loop
    }
  }

  // Check GRP connection
  if (!modem.isGprsConnected()) {
    Serial.println("GPRS no conectado. Reintentando...");
    if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
      Serial.println("Error al reconectar GPRS");
      delay(10000);
      return; //Retry on next loop
    }
  }

  // Connect to MQTT server
  Serial.print("Conectando al broker MQTT: ");
  Serial.println(broker);

  while (!mqtt.connect("mosquittoTest")) {
    Serial.println("Fallo al conectar al MQTT. Reintentando en 5 segundos...");
    mqtt.disconnect();
    delay(5000);
  }

  Serial.println("MQTT conectado correctamente.");
}




