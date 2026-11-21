
#include <Wire.h>
#include <Servo.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

// ---------------- PIN AND DEVICE SETTINGS ----------------

#define MPU_ADDR 0x68
#define SERVO_PIN 9

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(
  SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET
);

Servo platformServo;

// ---------------- SERVO SETTINGS ----------------

const int SERVO_CENTER = 90;
const int SERVO_MIN = 78;
const int SERVO_MAX = 102;
const int SERVO_DIRECTION = 1;

// ---------------- CONTROL SETTINGS ----------------

// Proportional correction
float Kp = 0.65;

// Feed-forward contribution
float KFF = 0.40;

// Integral contribution (NEW)
float Ki = 0.12;

// Integral safety limit (NEW)
const float INTEGRAL_LIMIT = 3.0;

float integralCorrection = 0;

// Gyroscope damping (disabled)
float KD_RATE = 0.0;

const int GYRO_TO_ANGLE_SIGN = 1;

// Gradual target transition
const float TARGET_SLEW_RATE = 15.0;

// Correction safety limit
const float MAX_CORRECTION = 12.0;
const float DEADBAND = 0.7;

// ---------------- ANGLE VARIABLES ----------------

float targetAngle = 0;
float smoothTarget = 0;
float platformAngle = 0;
float platformOffset = 0;

float filteredAngle = 0;
float gyroRate = 0;
float gyroOffset = 0;

bool filterStarted = false;
bool targetReceived = false;

// ---------------- COMMUNICATION ----------------

char receivedBuffer[32];
byte bufferIndex = 0;

unsigned long lastTargetTime = 0;
unsigned long lastControlTime = 0;
unsigned long lastDisplayTime = 0;
unsigned long lastSerialTime = 0;

const unsigned long SERIAL_INTERVAL = 500;
const unsigned long CONTROL_INTERVAL = 20;
const unsigned long DISPLAY_INTERVAL = 200;
const unsigned long TARGET_TIMEOUT = 1500;

// ---------------- MPU6050 SENSOR READING ----------------

bool readMPU(float &angle, float &gx) {

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);

  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  Wire.requestFrom(MPU_ADDR, 14, true);

  if (Wire.available() < 14) {
    return false;
  }

  int16_t axRaw = (Wire.read() << 8) | Wire.read();
  int16_t ayRaw = (Wire.read() << 8) | Wire.read();
  int16_t azRaw = (Wire.read() << 8) | Wire.read();

  // Skip temperature registers
  Wire.read();
  Wire.read();

  int16_t gxRaw = (Wire.read() << 8) | Wire.read();

  // Skip remaining gyro axes
  Wire.read();
  Wire.read();
  Wire.read();
  Wire.read();
  Wire.read();
  Wire.read();

  float ax = (float)axRaw;
  float ay = (float)ayRaw;
  float az = (float)azRaw;

  angle = atan2(
    ay,
    sqrt(ax * ax + az * az)
  ) * 180.0 / PI;

  // Gyroscope range: +/-250 degrees/sec
  gx = gxRaw / 131.0;

  return true;
}

// ---------------- PLATFORM CALIBRATION ----------------

void calibratePlatform() {

  Serial.println("Keep platform LEVEL and STILL.");
  Serial.println("Calibrating angle and gyro...");

  float angleTotal = 0;
  float gyroTotal = 0;
  int validSamples = 0;

  for (int i = 0; i < 200; i++) {

    float angle;
    float gx;

    if (readMPU(angle, gx)) {

      angleTotal += angle;
      gyroTotal += gx;
      validSamples++;
    }

    delay(10);
  }

  if (validSamples > 0) {

    platformOffset = angleTotal / validSamples;
    gyroOffset = gyroTotal / validSamples;

  } else {

    platformOffset = 0;
    gyroOffset = 0;
  }

  Serial.print("Angle offset: ");
  Serial.println(platformOffset, 2);

  Serial.print("Gyro X offset: ");
  Serial.println(gyroOffset, 2);
}

// ---------------- RECEIVE TARGET ANGLE ----------------

void receiveTargetAngle() {

  while (Serial1.available()) {

    char c = Serial1.read();

    if (c == '\n' || c == '\r') {

      if (bufferIndex > 0) {

        receivedBuffer[bufferIndex] = '\0';

        float value = atof(receivedBuffer);

        if (value >= -45.0 && value <= 45.0) {

          targetAngle = value;
          targetReceived = true;
          lastTargetTime = millis();
        }

        bufferIndex = 0;
      }
    }

    else if (bufferIndex < sizeof(receivedBuffer) - 1) {

      receivedBuffer[bufferIndex++] = c;

    }

    else {

      bufferIndex = 0;
    }
  }
}

// ---------------- OLED DISPLAY ----------------

void updateDisplay(bool linkActive) {

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("WIRELESS ANGLE CONTROL");

  display.drawLine(0, 12, 127, 12, SSD1306_WHITE);

  display.setCursor(0, 19);
  display.print("Target: ");
  display.print(targetAngle, 1);
  display.println(" deg");

  display.setCursor(0, 32);
  display.print("Platform: ");
  display.print(platformAngle, 1);
  display.println(" deg");

  display.setCursor(0, 46);

  if (linkActive) {
    display.println("Wireless: CONNECTED");
  } else {
    display.println("Wireless: WAITING");
  }

  display.display();
}

// ---------------- SETUP ----------------

void setup() {

  Serial.begin(115200);
  Serial1.begin(115200);

  Wire.begin();
  Wire.setClock(100000);

  Serial.println();
  Serial.println("MEGA PI FEED-FORWARD CONTROLLER");

  // Wake MPU6050
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  Wire.endTransmission();

  // Gyroscope range: +/-250 degrees/sec
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1B);
  Wire.write(0x00);
  Wire.endTransmission();

  // Initialize OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {

    Serial.println("OLED initialization failed!");

  } else {

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 20);
    display.println("Starting Controller...");
    display.display();
  }

  // Initialize servo
  platformServo.attach(SERVO_PIN);
  platformServo.write(SERVO_CENTER);

  delay(1000);

  // Calibrate with platform physically level
  calibratePlatform();

  float initialAngle;
  float initialGyro;

  if (readMPU(initialAngle, initialGyro)) {

    filteredAngle = initialAngle - platformOffset;
    filterStarted = true;
  }

  smoothTarget = 0;
  integralCorrection = 0;

  Serial.println("Controller ready.");
  Serial.println("Waiting for target angle.");
}

// ---------------- MAIN LOOP ----------------

void loop() {

  receiveTargetAngle();

  unsigned long now = millis();

  if (now - lastControlTime >= CONTROL_INTERVAL) {

    float dt = (now - lastControlTime) / 1000.0;
    lastControlTime = now;

    // Protect integral calculation from unusually large dt
    dt = constrain(dt, 0.001, 0.05);

    float rawAngle;
    float rawGyro;

    if (readMPU(rawAngle, rawGyro)) {

      float correctedAngle = rawAngle - platformOffset;

      // Low-pass angle filtering
      if (!filterStarted) {

        filteredAngle = correctedAngle;
        filterStarted = true;

      } else {

        filteredAngle =
          0.75 * filteredAngle +
          0.25 * correctedAngle;
      }

      platformAngle = filteredAngle;

      // Gyroscope bias correction
      gyroRate = rawGyro - gyroOffset;

      bool linkActive =
        targetReceived &&
        (now - lastTargetTime <= TARGET_TIMEOUT);

      if (linkActive) {

        // Gradually move internal target
        float maxTargetChange =
          TARGET_SLEW_RATE * dt;

        float targetDifference =
          targetAngle - smoothTarget;

        targetDifference = constrain(
          targetDifference,
          -maxTargetChange,
          maxTargetChange
        );

        smoothTarget += targetDifference;

        // Control error
        float error = smoothTarget - platformAngle;

        if (fabs(error) < DEADBAND) {
          error = 0;
        }

        // Proportional correction
        float P = Kp * error;

        // Feed-forward contribution
        float FF = KFF * smoothTarget;

        // Gyroscope damping
        float D =
          -KD_RATE *
          GYRO_TO_ANGLE_SIGN *
          gyroRate;

        // -------- NEW INTEGRAL CONTROL --------

        float proposedIntegral =
          integralCorrection + Ki * error * dt;

        proposedIntegral = constrain(
          proposedIntegral,
          -INTEGRAL_LIMIT,
          INTEGRAL_LIMIT
        );

        // Test output with proposed integral
        float proposedCorrection =
          P + FF + D + proposedIntegral;

        // Anti-windup:
        // Do not integrate further into saturation
        if (
          (proposedCorrection > MAX_CORRECTION && error > 0) ||
          (proposedCorrection < -MAX_CORRECTION && error < 0)
        ) {

          // Keep previous integral value

        } else {

          integralCorrection = proposedIntegral;
        }

        // -------- COMBINED CONTROLLER --------

        float correction =
          P + FF + D + integralCorrection;

        // Correction safety limit
        correction = constrain(
          correction,
          -MAX_CORRECTION,
          MAX_CORRECTION
        );

        int servoCommand =
          SERVO_CENTER +
          (int)(SERVO_DIRECTION * correction);

        servoCommand = constrain(
          servoCommand,
          SERVO_MIN,
          SERVO_MAX
        );

        platformServo.write(servoCommand);

        // -------- SERIAL MONITOR --------

        if (now - lastSerialTime >= SERIAL_INTERVAL) {

          lastSerialTime = now;

          Serial.print("Target: ");
          Serial.print(targetAngle, 2);

          Serial.print(" | SmoothTarget: ");
          Serial.print(smoothTarget, 2);

          Serial.print(" | Platform: ");
          Serial.print(platformAngle, 2);

          Serial.print(" | Error: ");
          Serial.print(error, 2);

          Serial.print(" | P: ");
          Serial.print(P, 2);

          Serial.print(" | FF: ");
          Serial.print(FF, 2);

          Serial.print(" | I: ");
          Serial.print(integralCorrection, 2);

          Serial.print(" | Gyro: ");
          Serial.print(gyroRate, 2);

          Serial.print(" | D: ");
          Serial.print(D, 2);

          Serial.print(" | Correction: ");
          Serial.print(correction, 2);

          Serial.print(" | Servo: ");
          Serial.println(servoCommand);
        }
      }

      else {

        // Hold last servo position when link is lost
        smoothTarget = platformAngle;

        // Reset integral when target is inactive
        integralCorrection = 0;
      }
    }
  }

  // -------- OLED UPDATE --------

  if (now - lastDisplayTime >= DISPLAY_INTERVAL) {

    lastDisplayTime = now;

    bool linkActive =
      targetReceived &&
      (now - lastTargetTime <= TARGET_TIMEOUT);

    updateDisplay(linkActive);
  }
}
