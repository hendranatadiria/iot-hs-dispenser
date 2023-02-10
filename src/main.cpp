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

// Device setup basics
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
int obstaclePin = 13; // D7
int ultraTrigPin = 14; // D5
int ultraEchoPin = 12; // D6
int irReading = HIGH; // HIGH = tidak ada tangan, LOW = ada tangan
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 250;
unsigned long ultraMillis = 0;
unsigned long ultraMillisInterval = 60000 * 3; // (60 dtk x 1000 ms) * x -- interval pengiriman level cairan ke broker dalam x menit.
unsigned long displayMillis = 0;
unsigned long displayDur = 3000; // 3dtk x 1000 ms
unsigned long distance;
double ems = 0;
enum States {low, high, falling};
States state = low;
bool actionExecuted = false;


WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

AsyncMqttClient mqtt;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wiFiDisconnectHandler;
Ticker wifiReconnectTimer;


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
    Serial.print("Berhasil terhubung dengan Wifi, IP: ");
    Serial.println(event.ip.toString());
    Serial.println("Menginisialisasi NTP Client...");
    timeClient.begin();
    // timeClient.setTimeOffset(25200); // GMT+7 karena kita kirim EPOCH, epochnya malah jadi ketambahan 7 jam.
    mqttInit();
}

void onWifiDisconnected(const WiFiEventStationModeDisconnected& event) {
    Serial.print("Wifi terputus!");
    mqttReconnectTimer.detach();
    wifiReconnectTimer.once(2, wifiInit);
    // initDisplay("WiFi Reconn...");

}

void onMqttConnected(bool sessionPresent) {
    Serial.println("Terhubung ke MQTT");
    // initDisplay("All systems go!");
    Serial.print("Apakah session di-keep?: "); Serial.println(sessionPresent);
    Serial.println("---------- READY TO GO! ----------");
}

void onMqttDisconnected(AsyncMqttClientDisconnectReason reason) {
    Serial.println("MQTT terputus!");

    if (WiFi.isConnected()) {
        mqttReconnectTimer.once(2, mqttInit);
          // initDisplay("MQTT Reconn...");
    }
}

void onMqttPublish(uint16_t packetId) {
  Serial.println("Publish diterima.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

// Calculate Distance (cm)
unsigned long calculateDistanceCM() {
  unsigned long duration, cm;
  digitalWrite(ultraTrigPin, LOW);
    delayMicroseconds(5);
    digitalWrite(ultraTrigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(ultraTrigPin, LOW);

    pinMode(ultraEchoPin, INPUT);
    duration = pulseIn(ultraEchoPin, HIGH);

    cm = (duration/2)/29.1;
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
  display.display();
  displayMillis = millis();
}

// Setup Routine
void setup() {
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

    digitalWrite(LED_BUILTIN, LOW);
    delay(20);
    double temp = mlx.readObjectTempC();
    delay(50);
    temp = temp + mlx.readObjectTempC();
    delay(20);
    temp = mlx.readObjectTempC();
    delay(20);
    temp = mlx.readObjectTempC();
    showTempDisplay(temp);
    Serial.println("---------------------------------");
    Serial.print("Suhu objek: "); Serial.print(temp); Serial.print("°C   Suhu sekitar: "); Serial.print(mlx.readAmbientTempC()); Serial.println("°C");
    
    distance = calculateDistanceCM();
    Serial.print("Jarak cairan dalam tangki:"); Serial.print(distance); Serial.println(" cm.");
    
    const long epoch = timeClient.getEpochTime();

    // publish to temp node
    mqtt.publish(TEMP_TOPIC, 2, true, String("{temp:'"+String(temp)+"', epoch:'"+String(epoch)+"'}").c_str());
    mqtt.publish(LEVL_TOPIC, 2, true, String("{distance:"+String(distance)+", level: "+String(distance)+" epoch:"+String(epoch)+"}").c_str());
   
    // dispense liquid
    // dispenseLiquid();

    digitalWrite(LED_BUILTIN, HIGH);
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
    const long epoch = timeClient.getEpochTime();
    mqtt.publish(LEVL_TOPIC, 2, true, String("{distance:"+String(distance)+", level: "+String(distance)+" epoch:"+String(epoch)+"}").c_str());
   
    Serial.print("Jarak cairan: ");Serial.print(distance); Serial.println("cm");
    Serial.println("");
    Serial.println("Cairan akan dihitung kembali");
    Serial.print("dalam"); Serial.print(ultraMillisInterval/1000); Serial.println(" detik.");
    Serial.println("---------------------------------");
    ultraMillis = millis();
  }

  if ((millis() - displayMillis) > displayDur) {
    display.clearDisplay();
    display.display();
  }
}