#include <TinyGPS++.h>
#include <SoftwareSerial.h>
#include <ArduinoJson.h>
#include <Crypto.h>
#include <SHA256.h>
#define TINY_GSM_MODEM_SIM800
#include <TinyGsmClient.h>
#include <PubSubClient.h>
// DHT
#include "DHT.h"

/****************************** MQTT defines *********************************/
const char* broker = "test.mosquitto.org";

const char* topicInit = "testMQTT_SIM800";


/************************* GPS defines ***************************************/
#define RXPinGPS 14  //D5 cable amarillo, al pin TX del módulo GPS
#define TXPinGPS 12  //D6 cable verde, al pin RX del módulo GPS
#define MAX_ENTRIES 6 //Caso 1 (10/60) Recogida cada 10sec y envío cada 1min
//#define MAX_ENTRIES 10 //Caso 2 (60/600) Recogida cada 60sec y envío cada 10min
//#define MAX_ENTRIES 6 //Caso 3 (600/3600) Recogida cada 600sec y envío cada 60min

/************************* DHT defines ***************************************/
#define DHTTYPE DHT11
#define DHT11_PIN 2 //D4

/************************* SIM800 defines ***************************************/
#define RXPinSIM 4 //D2 cable amarillo, al pin TX del módulo SIM
#define TXPinSIM 5 //D1 cable verde, al pin RX del módulo SIM

// Set serial for AT commands (to the module)
#include <SoftwareSerial.h>
SoftwareSerial SerialAT(RXPinSIM, TXPinSIM); // RX, TX

// set GSM PIN, if any
#define GSM_PIN ""


// Your GPRS credentials, if any
const char apn[] = "orangeworld";
const char gprsUser[] = "";
const char gprsPass[] = "";


// Inicializamos el sensor dht para poder pedirle informacion
DHT dht(DHT11_PIN, DHTTYPE);

// Struct para las instancias del gps en el json
struct GPSData {
    uint32_t timestamp;
    float latitude;
    float longitude;
    float humid;
    float temp;
    int32_t altitude;
    int32_t speed;
};

TinyGPSPlus gps;
// Serial with GPS
SoftwareSerial ss(RXPinGPS, TXPinGPS);
String IMEI = "";

TinyGsm modem(SerialAT);
TinyGsmClient client(modem);
PubSubClient mqtt(client);

GPSData gpsBuffer[MAX_ENTRIES];  // Buffer para almacenar datos GPS
int dataCount = 0;  // Contador de datos almacenados

// Bug workaround for Arduino 1.6.6, it seems to need a function declaration
// for some reason (only affects ESP8266, likely an arduino-builder bug).
void MQTT_connect();

void setup() {
  // Serial
  Serial.begin(9600);
  ss.begin(9600);

  Serial.println(); Serial.println();
  Serial.println("Wait...");

  SerialAT.begin(9600);
  delay(3000);
  // Init the modem
  Serial.println("Initializing modem...");
  modem.restart();

  String modemInfo = modem.getModemInfo();
  Serial.print("Modem Info: ");
  Serial.println(modemInfo);

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

// Funcion que añade una instancia nueva de datos al buffe GPS
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

// Función para generar el JSON mediante la librería ArduinoJson
String generateJSON() {
    StaticJsonDocument<1024> doc;  // Ajustar tamaño según sea necesario

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
        entry["altitude"] = String(gpsBuffer[i].altitude);
        entry["speed"] = String(gpsBuffer[i].speed);
    }

    // Convertir JSON a string
    String jsonString;
    serializeJson(doc, jsonString);

    // Calcular SHA-256 del JSON
    String hash = calculateSHA256(jsonString);
    doc["sha256"] = hash;

    // Serializar con hash incluido
    jsonString = "";
    serializeJson(doc, jsonString);
    
    return jsonString;
}

// Función para calcular SHA-256 de una cadena
String calculateSHA256(String input) {
    SHA256 sha256;
    uint8_t hash[sha256.hashSize()];

    sha256.reset();
    sha256.update(input.c_str(), input.length());
    sha256.finalize(hash, sizeof(hash));

    String hashString = "";
    for (int i = 0; i < sizeof(hash); i++) {
        if (hash[i] < 0x10) hashString += "0";  // Formato hexadecimal con ceros
        hashString += String(hash[i], HEX);
    }

    return hashString;
}

// Geenrado de timestamp de linux a partir de date y time
unsigned long dateTimeToUnixTimestamp(uint32_t date, uint32_t time) {
    // Extraer componentes de fecha
    int day   = date / 10000;
    int month = (date / 100) % 100;
    int year  = 2000 + (date % 100); // Asumiendo año 2000+

    // Extraer componentes de tiempo
    int hour = time / 1000000;
    int min  = (time / 10000) % 100;
    int sec  = (time / 100) % 100;

    // Meses en días hasta el mes actual (sin contar bisiestos)
    int daysPerMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    
    // Calcular días desde 1970-01-01 hasta el año actual
    unsigned long days = 0;
    for (int y = 1970; y < year; y++) {
        days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    }

    // Agregar días del año actual hasta el mes actual
    for (int m = 0; m < month - 1; m++) {
        days += daysPerMonth[m];
    }

    // Ajustar si es año bisiesto y el mes es después de febrero
    if (month > 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
        days += 1;
    }

    // Agregar días del mes actual
    days += day - 1;

    // Calcular total de segundos
    unsigned long timestamp = days * 86400L + hour * 3600 + min * 60 + sec;
    return timestamp;
}

String json = "";

void loop() {

 // Decodificar todos los bytes disponibles del GPS antes de procesar la ubicación
  while (ss.available()) {
    gps.encode(ss.read());
  }

  // Solo procesar si la ubicación ha cambiado
  if (gps.location.isUpdated() && dataCount < MAX_ENTRIES) {
    uint32_t date = gps.date.value();
    uint32_t time = gps.time.value();
    
    // Verificar si la fecha y la hora son válidas
    if (date > 0 && time > 0) {
      uint32_t timestamp = dateTimeToUnixTimestamp(date, time);
      float lat = gps.location.lat();
      float lon = gps.location.lng();
      float humid = dht.readHumidity();
      float temp = dht.readTemperature();
      int32_t alt = gps.altitude.value();
      int32_t speed = gps.speed.value();

      // Check if any reads failed and exit early (to try again).
      if (isnan(humid) || isnan(temp)) {
        Serial.println(F("Failed to read from DHT sensor!"));
      }
      // Evitar añadir datos inválidos
      if (timestamp > 1000000000 && lat != 0.0 && lon != 0.0) {
        addGPSData(timestamp, lat, lon, humid, temp, alt, speed);
        Serial.print("Instancia ");
        Serial.print(dataCount);
        Serial.println(" recolectada.");
      }
    }
    delay(9500); //Caso 1 10sec delay
    //delay(59500); //Caso 2 60sec delay
    //delay(599500); //Caso 3 600sec delay
  }
  // Ensure the connection to the MQTT server is alive (this will make the first
  // connection and automatically reconnect when disconnected).  See the MQTT_connect
  // function definition further below.
  MQTT_connect();

  // Cuando tengamos datos de un minuto generamos el json y lo enviamos
  if(dataCount == MAX_ENTRIES){
    json = generateJSON();
    dataCount = 0;

    // Publicamos el json generado
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
  int8_t ret;

  if (mqtt.connected()) return;
  
  Serial.print("Connecting to ");
  Serial.println(broker);
  
  while (!mqtt.connect("mosquittoTest")) { // connect will return true for connected
       Serial.println("Retrying MQTT connection in 5 seconds...");
       mqtt.disconnect();
       delay(5000);  // wait 5 seconds
  }
  Serial.println("MQTT Connected!");
}