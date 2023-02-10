#include <Arduino.h>
#include <Adafruit_MLX90614.h> // Library untuk sensor suhu MLX90614

Adafruit_MLX90614 mlx = Adafruit_MLX90614();
bool isOn = true;

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  // put your setup code here, to run once:
  Serial.begin(9600);
  while (!Serial);
  Serial.println("Menyiapkan Sensor Suhu.\n");

  if (!mlx.begin()) {
    Serial.println("Gagal terhubung dengan Sensor MLX90614.");
    while (1);
  };

}

void loop() {
  if (isOn) {
  digitalWrite(LED_BUILTIN, LOW);
  } else {
  digitalWrite(LED_BUILTIN, HIGH);
  }
  // put your main code here, to run repeatedly:
  Serial.print("Suhu objek: ");
  Serial.print(mlx.readObjectTempC());
  Serial.print("°C");
  Serial.print("   ");
  Serial.print("Suhu sekitar: ");
  Serial.print(mlx.readAmbientTempC());
  Serial.println("°C");
  Serial.print("Suhu objek: ");
  Serial.print(mlx.readObjectTempF());
  Serial.print("°F");
  Serial.print("   ");
  Serial.print("Suhu sekitar: ");
  Serial.print(mlx.readAmbientTempF());
  Serial.println("°F");

  Serial.println("Mengukur kembali dalam 10 detik...");
  delay(10000);
  isOn = !isOn;
}