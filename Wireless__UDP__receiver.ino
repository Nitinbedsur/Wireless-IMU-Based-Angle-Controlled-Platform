
#include <WiFi.h>
#include <WiFiUdp.h>

const char* ssid = "ANGLE_PLATFORM";
const char* password = "platform123";

WiFiUDP udp;
const unsigned int localPort = 4210;

#define UART_TX 14

void setup() {
  Serial.begin(115200);

  // UART2 sends data to Arduino Mega
  Serial2.begin(115200, SERIAL_8N1, -1, UART_TX);

  Serial.println();
  Serial.println("=============================");
  Serial.println("ESP32-CAM UDP ANGLE RECEIVER");
  Serial.println("=============================");

  WiFi.mode(WIFI_AP);

  if (WiFi.softAP(ssid, password)) {
    Serial.println("Wi-Fi Access Point started!");
  } else {
    Serial.println("Wi-Fi AP FAILED!");
    while (1) delay(1000);
  }

  Serial.print("SSID: ");
  Serial.println(ssid);

  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());

  if (udp.begin(localPort)) {
    Serial.println("UDP server started on port: 4210");
  } else {
    Serial.println("UDP server FAILED!");
  }

  Serial.println("Waiting for target angle...");
}

void loop() {
  int packetSize = udp.parsePacket();

  if (packetSize > 0) {
    char incoming[32];

    int len = udp.read(incoming, sizeof(incoming) - 1);

    if (len > 0) {
      incoming[len] = '\0';

      char* endPtr;
      float angle = strtof(incoming, &endPtr);

      if (endPtr != incoming &&
          *endPtr == '\0' &&
          isfinite(angle) &&
          angle >= -90.0 &&
          angle <= 90.0) {

        Serial.print("Received angle: ");
        Serial.println(angle, 2);

        Serial2.println(angle, 2);

        Serial.print("Forwarded to Mega: ");
        Serial.println(angle, 2);
      }
    }
  }
}
