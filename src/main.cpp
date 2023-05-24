/*
  Cloud-connected Smart Hand Sanitizer Dispenser
  PROJECT SKRIPSI untuk Gelar Sarjana Komputer (S.Kom)
  Bernardinus Hendra Natadiria
  24060118130107

  Universitas Diponegoro
  Semarang, 2022-2023

  Ad Maiorem Dei Gloriam!
*/
#include <Arduino.h>
#include <Adafruit_MLX90614.h> // Library untuk sensor suhu MLX90614
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
// #include <ArduinoJson.h>

// The WiFi connection details
#define WIFI_SSID "iotConnected130107"
#define WIFI_PASS "connectedSHS.107"

// The MQTT connection detail
#define MQTT_HOST "mqtt.hendranata.com"
#define MQTT_PORT 10873
#define MQTT_USER "iotHS"
#define MQTT_PASS "t7dd5Mj45kBe^Z"

// MQTT Topics
#define TEMP_TOPIC "/cc-shs/temp"
#define LEVL_TOPIC "/cc-shs/level"

// MQTT Topics
#define MAINTENANCE_TOPIC "/cc-shs/maintenance"

// Device setup basics
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)

#define NR 10 // number of repetition of readings for averaging distance (Ultrasonic Sensor)

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
int mosfetPin = 2; // D4
int obstaclePin = 13; // D7
int ultraTrigPin = 14; // D5
int ultraEchoPin = 12; // D6
int maintenancePin = 15; // D8
int irReading = HIGH; // HIGH = tidak ada tangan, LOW = ada tangan
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 250;
unsigned long ultraMillis = 0;
unsigned long ultraMillisInterval = 60000 * 3; // (60 dtk x 1000 ms) * x -- interval pengiriman level cairan ke broker dalam x menit.
unsigned long displayMillis = 0;
unsigned long displayDur = 3000; // 3dtk x 1000 ms
unsigned long dispenseMillis = 0;
unsigned long dispenseDur = 250;
float distance;
double ems = 0;
enum States {low, high, falling};
States state = low;
bool actionExecuted = false;
bool wifiConnected = false;
bool commConnected = false;
float percentage = 0;
String msgInit = "";
bool isMaintenance = false;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

AsyncMqttClient mqtt;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wiFiDisconnectHandler;
Ticker wifiReconnectTimer;

String deviceId = "cc-shs-"+String(ESP.getChipId(), HEX);


// Device-related routine
// Display Initialization
// input: msg (string)
void initDisplay(String msg = "") {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(22,20);
  display.setTextColor(WHITE);
  display.println(F("Welcome!"));

  int len = msg.length();
  if (len > 21) {
    msg = msg.substring(0, 21);
    len = 21;
  }
  const int startCenter = (SCREEN_WIDTH - (6*len))/2;
  display.setTextSize(1);
  display.setCursor(startCenter, 38);
  display.print(msg);
  display.display();
  displayMillis = millis();
}


// WiFi connection
void wifiInit() {
    Serial.println("Menginisialisasi WiFi...");
    initDisplay("Running Setup...");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void mqttInit() {
    Serial.println("Menginisialisasi MQTT...");
    // initDisplay("MQTT Setup...");
    mqtt.connect();
}

void onWifiConnected(const WiFiEventStationModeGotIP& event) {
    wifiConnected = true;
    Serial.print("Berhasil terhubung dengan Wifi, IP: ");
    Serial.println(event.ip.toString());
    Serial.println("Menginisialisasi NTP Client...");
    timeClient.begin();
    // timeClient.setTimeOffset(25200); // GMT+7 karena kita kirim EPOCH, epochnya malah jadi ketambahan 7 jam.
    mqttInit();

    msgInit = "WiFi Connected!";
    displayMillis = millis();

}

void onWifiDisconnected(const WiFiEventStationModeDisconnected& event) {
    wifiConnected = false;
    Serial.print("Wifi terputus!");
    mqttReconnectTimer.detach();
    wifiReconnectTimer.once(2, wifiInit);
    // initDisplay("WiFi Reconn...");

    msgInit = "WiFi Disconnected!";
    displayMillis = millis();

}

void onMqttConnected(bool sessionPresent) {
    commConnected = true;
    Serial.println("Terhubung ke MQTT");
    // initDisplay("All systems go!");
    Serial.print("Apakah session di-keep?: "); Serial.println(sessionPresent);
    Serial.println("---------- READY TO GO! ----------");

    msgInit = isMaintenance ? "--MAINTENANCE MODE--" : "Ready to Go!";
    displayMillis = millis();
}

void onMqttDisconnected(AsyncMqttClientDisconnectReason reason) {
    commConnected = false;
    Serial.println("MQTT terputus!");

    if (WiFi.isConnected()) {
        mqttReconnectTimer.once(2, mqttInit);
        Serial.println("Mencoba koneksi ulang ke MQTT...(inside onMqttDisconnected)");

        msgInit = "Reconnecting to Server...";
          // initDisplay("MQTT Reconn...");
    }
}

void onMqttPublish(uint16_t packetId) {
  Serial.println("Publish diterima.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

// Calculate Distance (cm)
float calculateDistanceCM() {
  // Logic: Get the ambient temperature, then calculate the distance based on the ambient temperature.
  // Calculation will be done using n=20 (20 times measurement). Then the max and min data will be taken into account as max and min variable.
  // It will also take into account the second maximum and minimum variable, saved as max2 and min2.
  // If max-min is less than 1,5cm, then it is considered stable, if not, then calculate the max2-min2. 
  // If it's stable, then max and min will be removed from calculation and the average of the rest will be returned.
  // If it's not stable, then -1 will be returned.

  Serial.println("Calculating distance...");

  // Get the ambient temperature
  double temp = mlx.readAmbientTempC();
  Serial.print("Ambient temperature: ");
  Serial.print(temp);
  Serial.println(" C");

  if (temp == NAN || temp <20 || temp > 40) {
    Serial.println("Ambient temperature is not valid. Using 28 degrees instead."); // Average temperature in Semarang (customweather.com)
    temp = 28.0;
  }

  float soundSpeed = 33130+60.6*temp; // cm/s; source: wikipedia
  float distance[NR]; // distance array

  for (int i=0; i<NR;i++) {
    unsigned long duration;
    digitalWrite(ultraTrigPin, LOW);
    delayMicroseconds(5);
    digitalWrite(ultraTrigPin, HIGH);
    delayMicroseconds(12);
    digitalWrite(ultraTrigPin, LOW);

    pinMode(ultraEchoPin, INPUT);
    duration = pulseIn(ultraEchoPin, HIGH);

    distance[i] = (duration/2)*soundSpeed/1000000; // cm, div 10^6 because the duration is in microseconds

    delay(5);
  }

  // Sort the array
  for (int i=0; i<NR; i++) {
    for (int j=i+1; j<NR; j++) {
      if (distance[i] > distance[j]) {
        float temp = distance[i];
        distance[i] = distance[j];
        distance[j] = temp;
      }
    }
  }

  // Get the max and min value
  float max = distance[NR-1];
  float min = distance[0];
  float max2 = distance[NR-2];
  float min2 = distance[1];

  // Calculate the average
  float sum = 0;
  for (int i=0; i<NR; i++) {
    sum += distance[i];
  }

  // Print distances
  Serial.print("Distances: ");
  for (int i=0; i<NR; i++) {
    Serial.print(distance[i]);
    Serial.print(" ");
  }

  float cm = sum/NR;
  Serial.print("Raw average distance: ");
  Serial.print(cm);
  Serial.println(" cm");
  
  // If max-min is less than 1,5cm, then it is considered stable, if not, then calculate the max2-min2. If it's stable, then max and min will be removed from calculation and the average of the rest will be returned.
  // If it's not stable, then -1 will be returned.

  if (max-min < 1.5) {
    Serial.println("Distance is stable.");
    
  } else {
    if (max2-min2 < 1.5) {
      Serial.println("Distance is stable (using max2 min2).");
      sum = sum - max - min;
      cm = sum/(NR-2);
    } else {
      Serial.println("Distance is not stable.");
      cm = -1;
    }
  }

  Serial.print("Final Distance: ");
  Serial.print(cm);
  Serial.println(" cm");
  return cm;
}

// Display temperature on OLED display
// input: temp (double)
void showTempDisplay(double temp) {
  display.clearDisplay();
  display.setCursor(4,16);
  display.setTextSize(4);
  display.println(String(temp));
  display.setCursor(58,22);
  display.setTextSize(1);
  display.print((char)247);
  display.print(F("C"));

  display.setCursor(1,1);
  if(wifiConnected) {
    display.print(F("w   "));
  } else {
    display.print(F("wX  "));
  }
  if(commConnected) {
    display.print(F("c   "));
  } else {
    display.print(F("cX  "));
  }

  int geserKiri = (String(percentage, 2).length()+1) * 6;
  display.setCursor(SCREEN_WIDTH-geserKiri,1);
  display.print(String(percentage, 2));
  display.print(F("%"));

  if(isMaintenance) {
    display.setCursor(0,SCREEN_HEIGHT-8);
    display.print(F("MAINTENANCE"));
  }

  if (percentage < 20) {
    display.setCursor(isMaintenance? 86 : 43,SCREEN_HEIGHT-8);
    display.print(F("REFILL!"));
  }

  display.display();
  displayMillis = millis();
}

void dispenseLiquid() {
  Serial.println("Dispensing liquid...");
  digitalWrite(mosfetPin, HIGH);
  dispenseMillis = millis();
}

float calculatePercentage(float currentHeight) {
  if (currentHeight < 0) return -1;
  if (currentHeight <=3.88) {
    return 100;
  } else if (currentHeight >= 17.3 ) {
    return 0;
  } else {
    return (1 - ((currentHeight - 3.88) / (17.3 - 3.88))) * 100;
  }
}

// Setup Routine
void setup() {
  pinMode(maintenancePin, INPUT_PULLUP);
  pinMode(mosfetPin, OUTPUT);
  digitalWrite(mosfetPin, LOW);
  pinMode(obstaclePin, INPUT);
  pinMode(ultraTrigPin, OUTPUT);
  pinMode(ultraEchoPin, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(9600);
  while (!Serial);


  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }

  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(22,20);
  display.setTextColor(WHITE);
  display.println(F("Welcome!"));
  display.display();

  Serial.println("Sensor Halangan siap.\n");
  Serial.println("Sensor Jarak Ultrasonik siap.\n");
  Serial.println("Menyiapkan Sensor Suhu.\n");

  if (!mlx.begin()) {
    Serial.println("Gagal terhubung dengan Sensor MLX90614.");
    while (1);
  };

  ems = mlx.readEmissivity();
  while(ems != ems) {
      ems = mlx.readEmissivity();
  }
  Serial.print("Emissivity: ");
  Serial.println(ems);

  // if (ems != 0.98) {
  //   Serial.println("Recalibrating emissivity...");
  //   mlx.writeEmissivity(0.98);

  //   ems = mlx.readEmissivity();

  //   Serial.print("New Emissivity: ");
  //   Serial.println(ems);
  //   Serial.println("Done.");
  // }

  display.setTextSize(1);
  display.setCursor(0,0);
  display.print(ems);

  Serial.println("Sensor Suhu siap.\n");

  // Read if the maintenance switch/jumper is on, and set the whole system to maintenance mode if it is.
  digitalRead(maintenancePin) == LOW ? isMaintenance = false : isMaintenance = true;
  if (isMaintenance) {
    Serial.println("Maintenance Mode ON");
    digitalWrite(LED_BUILTIN, HIGH);
  } else {
    Serial.println("Maintenance Mode OFF");
    digitalWrite(LED_BUILTIN, LOW);
  }

  // All sensors ready, let's connect.
  // WiFi Initialization
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnected);
  wiFiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnected);

  mqtt.onConnect(onMqttConnected);
  mqtt.onDisconnect(onMqttDisconnected);
  mqtt.onPublish(onMqttPublish);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCredentials(MQTT_USER, MQTT_PASS);
  wifiInit();
  
  // while(!mqtt.connected() || !WiFi.isConnected()) {
  //   if (millis()%1000 == 0) {
  //     Serial.print(".");
  //   }
  // }
}

void loop(){
irReading = digitalRead(obstaclePin); // State defaultnya HIGH jika tidak ada halangan.
unsigned long currentTime = millis();
timeClient.update();

  // Routine untuk Deteksi tangan -- Hitung Suhu -- Dispense Cairan (TBA)
  // Debouncing using State

  switch (state) {
    case low: 
      if(irReading == LOW) { // ada halangan (tangan)
        state = high;
        lastDebounceTime = currentTime;
        actionExecuted = false;
      }
      break;

    case high: 
      if(irReading == HIGH) { // kembali normal (tidak ada halangan)
        state = falling;
        lastDebounceTime = currentTime;
      }
      break;

    case falling:
      if (currentTime - lastDebounceTime > debounceDelay) {
        if (irReading == HIGH) { // kembali normal (tidak ada halangan)
          state = low; 
        } else {
          state = high; 
        }
      }
      break;
  }

  // Berdasar state, eksekusi aksinya.
  if(state == high && !actionExecuted) {

    // digitalWrite(LED_BUILTIN, LOW);
    // delay(20);
    // double temp = mlx.readObjectTempC();
    // delay(50);
    // temp = temp + mlx.readObjectTempC();
    delay(20);
    double temp = mlx.readObjectTempC();
    delay(20);
    temp = mlx.readObjectTempC();
    
    Serial.println("---------------------------------");
    Serial.print("Suhu objek: "); Serial.print(temp); Serial.print("°C   Suhu sekitar: "); Serial.print(mlx.readAmbientTempC()); Serial.println("°C");
    
    distance = calculateDistanceCM();
    percentage = calculatePercentage(distance);
    Serial.print("Jarak cairan dalam tangki:"); Serial.print(distance); Serial.print(" cm atau "); Serial.print(percentage); Serial.println("%.");
    
    const long epoch = timeClient.getEpochTime();
    showTempDisplay(temp);

    // publish to temp node
    mqtt.publish(isMaintenance ? MAINTENANCE_TOPIC : TEMP_TOPIC, 0, true, String("{\"deviceId\" : \""+String(deviceId)+"\", \"temp\": \""+String(temp)+"\", \"epoch\" : \""+String(epoch)+"\"}").c_str());
    mqtt.publish(isMaintenance ? MAINTENANCE_TOPIC : LEVL_TOPIC, 0, true, String("{\"deviceId\" : \""+String(deviceId)+"\", \"distance\" :\""+String(distance)+"\", \"level\" : \""+String(percentage)+"\", \"epoch\" :\""+String(epoch)+"\"}").c_str());

    // dispense liquid
    dispenseLiquid();

    // digitalWrite(LED_BUILTIN, HIGH);
    Serial.println("---------------------------------");
    Serial.println("Ready.");
    actionExecuted = true;
  } else if (state == low) {
    actionExecuted = false;
  }

  // Routine hitung level cairan tiap 3 menit.
  if ((millis() - ultraMillis) >ultraMillisInterval) {
    Serial.println("---------------------------------");
    Serial.println("Menghitung jarak cairan");
    Serial.println("(persentase cairan dlm tangki)...");
    Serial.println("");
    distance = calculateDistanceCM();
    percentage = calculatePercentage(distance);
    const long epoch = timeClient.getEpochTime();

    mqtt.publish(isMaintenance ? MAINTENANCE_TOPIC : LEVL_TOPIC, 0, true, String("{\"deviceId\" : \""+String(deviceId)+"\", \"distance\" :\""+String(distance)+"\", \"level\" : \""+String(percentage)+"\", \"epoch\" :\""+String(epoch)+"\"}").c_str());
   
    Serial.print("Jarak cairan: ");Serial.print(distance); Serial.print("cm atau "); Serial.print(percentage); Serial.println("%.");
    Serial.println("");
    Serial.println("Cairan akan dihitung kembali");
    Serial.print("dalam "); Serial.print(ultraMillisInterval/1000); Serial.println(" detik.");
    Serial.println("---------------------------------");
    Serial.print("Heap: ");
    Serial.println(ESP.getFreeHeap());
    ultraMillis = millis();
  }

  if ((millis() - displayMillis) > displayDur) {
    display.clearDisplay();
    display.display();
  }

  if ((millis() - dispenseMillis) > dispenseDur) {
    digitalWrite(mosfetPin, LOW);
  } 

  if(!commConnected){
    Serial.println("This is happening inside loop when mqtt disconnected.");
  }

  if(!wifiConnected){
    Serial.println("This is happening inside loop when wifi disconnedted.");
  }

  if (msgInit != "") {
    initDisplay(msgInit);
    msgInit = "";
  }
}