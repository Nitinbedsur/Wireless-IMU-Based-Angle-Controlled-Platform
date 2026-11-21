
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

Adafruit_MPU6050 mpu;
WiFiUDP udp;

const char* ssid = "ANGLE_PLATFORM";
const char* password = "platform123";

IPAddress receiverIP(192, 168, 4, 1);
const unsigned int receiverPort = 4210;

float zeroOffset = 0;
float filteredAngle = 0;

const float FILTER_ALPHA = 0.20;

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  if (!mpu.begin()) {
    Serial.println("MPU6050 NOT FOUND!");
    while (1) delay(10);
  }

  Serial.println("Keep controller flat for calibration");
  delay(2000);

  float total = 0;

  for (int i = 0; i < 100; i++) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    float angle = atan2(
      a.acceleration.y,
      sqrt(a.acceleration.x * a.acceleration.x +
           a.acceleration.z * a.acceleration.z)
    ) * 180.0 / PI;

    total += angle;
    delay(20);
  }

  zeroOffset = total / 100.0;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to ESP32-CAM");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWi-Fi Connected!");
  Serial.print("Handheld IP: ");
  Serial.println(WiFi.localIP());

  udp.begin(4211);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected!");
    delay(500);
    return;
  }

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  float rawAngle = atan2(
    a.acceleration.y,
    sqrt(a.acceleration.x * a.acceleration.x +
         a.acceleration.z * a.acceleration.z)
  ) * 180.0 / PI;

  float correctedAngle = rawAngle - zeroOffset;

  filteredAngle =
    FILTER_ALPHA * correctedAngle +
    (1.0 - FILTER_ALPHA) * filteredAngle;

  char message[20];
  snprintf(message, sizeof(message), "%.2f", filteredAngle);

  udp.beginPacket(receiverIP, receiverPort);
  udp.print(message);
  udp.endPacket();

  Serial.print("Sending target angle: ");
  Serial.println(filteredAngle, 2);

  delay(200);
}
