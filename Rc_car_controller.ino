#include <WiFi.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <WebSocketsServer.h>
#include <ESP32Servo.h>

const char *ssid = "HA_CAR";
const char *password = "123456780";

WebSocketsServer webSocket = WebSocketsServer(81);
WebServer server(80);

// Motor pins (TB6612FNG)
const int motorAin1Pin = 6;
const int motorAin2Pin = 7;
const int motorPwmPin = 5;
const int servoPin = 9;

// PWM settings for motor
const int motorPwmFreq = 1000;
const int motorPwmResolution = 9; // 9-bit for 0-511 range to support 440 max speed
const int motorPwmChannel = 0;

// Motor configuration
const int MOTOR_MIN_PWM = 30;
const int MOTOR_MAX_FORWARD_SPEED = 440; // Supports DRIFT mode
const int MOTOR_MAX_REVERSE_SPEED = 255;
const int MOTOR_FORWARD_PIN1 = LOW;
const int MOTOR_FORWARD_PIN2 = HIGH;
const int MOTOR_REVERSE_PIN1 = HIGH;
const int MOTOR_REVERSE_PIN2 = LOW;

// Servo configuration (DSM005: 50Hz, 1000-2000µs)
const int SERVO_MIN_ANGLE = 16;
const int SERVO_MAX_ANGLE = 146;
const int SERVO_CENTER_ANGLE = 90;
const int SERVO_LEFT_ANGLE = SERVO_MAX_ANGLE;
const int SERVO_RIGHT_ANGLE = SERVO_MIN_ANGLE;
const int SERVO_RANGE = abs(SERVO_RIGHT_ANGLE - SERVO_LEFT_ANGLE);
const int SERVO_HALF_RANGE = SERVO_RANGE / 2;

// Gear-specific configurations
struct GearConfig {
  int maxSpeed;
  float steeringSensitivity; // 1.0 = full range, <1.0 = sharper
};

const GearConfig GEAR_CONFIGS[] = {
  {255, 0.9},   // ECO
  {350, 0.9},   // SPORT+
  {450, 1.0}    // DRIFT
};

int currentGear = 0; // Moved to global scope
int gaugeValueOnGear = 0;
Servo myServo;
int steering = SERVO_CENTER_ANGLE;
unsigned long lastServoUpdate = 0;
const unsigned long SERVO_UPDATE_INTERVAL = 20;
unsigned long lastCommandTime = 0; // Tracks last WebSocket command
const unsigned long INACTIVITY_TIMEOUT = 1000 * 60 * 10; // 10 minutes
unsigned long lastLedBlinkTime = 0; // Tracks last LED blink
const unsigned long LED_BLINK_INTERVAL = 3000; // 3 seconds
bool ledState = false; // LED state


// Map UI input to servo angle with gear-based sensitivity
int mapUIToServoAngle(int uiValue, float sensitivity) {
  uiValue = constrain(uiValue, 0, 180);
  int servoAngle;
  int effectiveRange = SERVO_RANGE * sensitivity;
  int halfRange = effectiveRange / 2;
  int center = SERVO_CENTER_ANGLE;
  if (uiValue <= 90) {
    servoAngle = map(uiValue, 0, 90, center + halfRange, center);
  } else {
    servoAngle = map(uiValue, 90, 180, center, center - halfRange);
  }
  return constrain(servoAngle, min(SERVO_LEFT_ANGLE, SERVO_RIGHT_ANGLE), 
                  max(SERVO_LEFT_ANGLE, SERVO_RIGHT_ANGLE));
}

// Map UI input to motor PWM with gear-based max speed
  int mapUIToMotorPWM(int uiValue, int maxSpeed) {
    int guageMaxSpeed = GEAR_CONFIGS[currentGear].maxSpeed; 
    if (uiValue == 0) {
      return 0;
    } else if (uiValue > 0) {
      return map(uiValue, 1, guageMaxSpeed, MOTOR_MIN_PWM, maxSpeed);
    } else {
      return -map(abs(uiValue), 1, guageMaxSpeed, MOTOR_MIN_PWM, MOTOR_MAX_REVERSE_SPEED);
    }
  }

// Control motor with configured direction pins
void setMotorSpeed(int pwmValue) {
  if (pwmValue == 0) {  
    ledcWrite(motorPwmPin, 0);
    digitalWrite(motorAin1Pin, LOW);
    digitalWrite(motorAin2Pin, LOW);
  } else if (pwmValue > 0) {
    digitalWrite(motorAin1Pin, MOTOR_FORWARD_PIN1);
    digitalWrite(motorAin2Pin, MOTOR_FORWARD_PIN2);
    ledcWrite(motorPwmPin, abs(pwmValue));
  } else {
    digitalWrite(motorAin1Pin, MOTOR_REVERSE_PIN1);
    digitalWrite(motorAin2Pin, MOTOR_REVERSE_PIN2);
    ledcWrite(motorPwmPin, abs(pwmValue));
  }
}

// WebSocket event handler
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_TEXT) {
    String message = String((char*)payload);
    if (message.startsWith("M:") || message.startsWith("S:") || message.startsWith("G:")) {
      lastCommandTime = millis(); // Update on command
      Serial.println("Command received: " + message);
    }
    if (message.startsWith("S:")) {
      unsigned long startTime = micros();
      int uiValue = message.substring(2).toInt();
      int newSteering = mapUIToServoAngle(uiValue, GEAR_CONFIGS[currentGear].steeringSensitivity);
      if (steering != newSteering) {
        steering = newSteering;
        myServo.write(steering);
        unsigned long endTime = micros();
        Serial.print("Steering set to: ");
        Serial.print(uiValue);
        Serial.print(" (Angle: ");
        Serial.print(steering);
        Serial.print(", Time us: ");
        Serial.print(endTime - startTime);
        Serial.println(")");
      }
    } else if (message.startsWith("M:")) {
              Serial.print(" (Messageee: ");
        Serial.print(message);
      int uiValue = message.substring(2).toInt();
      int pwmValue = mapUIToMotorPWM(uiValue, GEAR_CONFIGS[currentGear].maxSpeed);
      setMotorSpeed(pwmValue);
      Serial.print("Speed set to: ");
      Serial.print(uiValue);
      Serial.print(" (PWM: ");
      Serial.print(pwmValue);
      Serial.println(")");
    } else if (message.startsWith("G:")) {
        int gearIndex = message.substring(2).toInt();
          if (gearIndex >= 0 && gearIndex < 3) {
          currentGear = gearIndex;
          Serial.print("Gear set to: ");
          Serial.println(gearIndex);

          int newGaugeSpeed = GEAR_CONFIGS[currentGear].maxSpeed;
          String msg = "gearSpeed:" + String(newGaugeSpeed);
          webSocket.broadcastTXT(msg); // 🔥 Send max speed to JS
        }
      }
  }
}

// HTTP handler for serving HTML
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
   <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>RC Controller with Gear Switch</title>
  <style>
    :root {
      --primary-bg: #2c3e50;
      --track-bg: #333;
      --knob-color: #e74c3c;
      --highlight-color: #f39c12;
      --text-color: #ecf0f1;
      --switch-width: 100px;
      --switch-height: 220px;
      --knob-size: 45px;
    }

    * {
      margin: 0;
      padding: 0;
      box-sizing: border-box;
    }

    body {
      font-family: "Arial", sans-serif;
      overflow: hidden;
      position: relative;
      height: 100vh;
      background-color: #000000;
    }

    .container {
      position: relative;
      width: 100%;
      height: 100vh;
    }

    .gear-switch {
      position: absolute;
      top: 20px;
      right: 20px;
      width: var(--switch-width);
      height: var(--switch-height);
      background: linear-gradient(145deg, var(--primary-bg), #34495e);
      border-radius: calc(var(--switch-width) / 2);
      box-shadow: inset 0 8px 16px rgba(0, 0, 0, 0.3), 0 4px 20px rgba(0, 0, 0, 0.4);
      border: 3px solid #555;
      transition: transform 0.2s ease;
      z-index: 10;
    }

    .gear-track {
      position: absolute;
      top: 20px;
      left: 50%;
      transform: translateX(-50%);
      width: 6px;
      height: calc(var(--switch-height) - 40px);
      background: linear-gradient(to bottom, #666, var(--track-bg));
      border-radius: 3px;
      box-shadow: inset 0 2px 4px rgba(0, 0, 0, 0.5);
    }

    .gear-positions {
      position: absolute;
      left: calc(var(--switch-width) / -2);
      top: 0;
      width: calc(var(--switch-width) * 2);
      height: 100%;
    }

    .gear-label {
      position: absolute;
      font-weight: bold;
      font-size: 16px;
      color: var(--text-color);
      text-shadow: 1px 1px 3px rgba(0, 0, 0, 0.5);
      transition: all 0.3s ease;
      cursor: pointer;
      user-select: none;
      padding: 6px 10px;
      border-radius: 8px;
      background: rgba(255, 255, 255, 0.1);
    }

    .gear-label:hover {
      background: rgba(255, 255, 255, 0.2);
      transform: scale(1.05);
    }

    .gear-label.active {
      color: var(--highlight-color);
      background: rgba(243, 156, 18, 0.3);
      box-shadow: 0 0 15px rgba(243, 156, 18, 0.6);
      transform: scale(1.1);
    }

    .gear-label[data-gear="0"] { top: 0px; left: 20px; }
    .gear-label[data-gear="1"] { top: 55px; left: 20px; }
    .gear-label[data-gear="2"] { top: 100px; left: 20px; }

    .gear-knob {
      position: absolute;
      width: var(--knob-size);
      height: var(--knob-size);
      background: linear-gradient(145deg, var(--knob-color), #c0392b);
      border-radius: 50%;
      left: 50%;
      transform: translateX(-50%);
      top: 20px;
      cursor: grab;
      transition: top 0.4s cubic-bezier(0.25, 0.46, 0.45, 0.94);
      box-shadow: 0 6px 12px rgba(0, 0, 0, 0.4), inset 0 2px 4px rgba(255, 255, 255, 0.2);
      border: 3px solid #a93226;
    }

    .gear-knob:active {
      cursor: grabbing;
      transform: translateX(-50%) scale(0.98);
    }

    .gear-knob::before {
      content: '';
      position: absolute;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      width: 18px;
      height: 18px;
      background: linear-gradient(145deg, #fff, #ddd);
      border-radius: 50%;
      box-shadow: inset 0 2px 4px rgba(0, 0, 0, 0.2);
    }

    .gauge-transmission {
      color: azure;
      position: absolute;
      top: 30%;
      left: 48%;
      font-size: 20px;
      font-style: italic;
      font-weight: 800;
    }

    .gauge-container {
      position: absolute;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      width: 360px;
      height: 360px;
      z-index: 2;
    }

    .joystick-container {
      position: absolute;
      bottom: 0;
      width: 100%;
      display: flex;
      justify-content: space-between;
      z-index: 5;
    }

    .joystick-left,
    .joystick-right {
      width: 50%;
      height: 300px;
      position: relative;
    }

    .joystick {
      position: absolute;
      bottom: 100px;
      left: 50%;
      transform: translateX(-50%);
      width: 140px;
      height: 140px;
      background-color: rgba(108, 108, 108, 0.1);
      border-radius: 50%;
    }

    .joystick-knob {
      position: absolute;
      width: 80px;
      height: 80px;
      background-color: #c90016;
      border-radius: 50%;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      cursor: pointer;
      user-select: none;
    }

    .gauge {
      width: 100%;
      height: 100%;
      position: relative;
    }

    .gauge-value {
      position: absolute;
      top: 50%;
      left: 49%;
      transform: translate(-50%, -50%);
      font-size: 80px;
      font-weight: 700;
      font-style: italic;
      -webkit-text-stroke: 4px #2ecc71;
      color: transparent;
    }

    .gauge-unit {
      position: absolute;
      bottom: 110px;
      left: 50%;
      color: grey;
      transform: translateX(-50%);
      font-size: 15px;
      font-weight: 600;
    }

    .gauge-temperature {
      color: grey;
      display: flex;
      position: absolute;
      bottom: 18%;
      left: 46.9%;
    }

    .gauge-temperature-unit {
      font-size: 10px;
    }

    .gauge-left {
      color: azure;
      display: flex;
      position: absolute;
      font-size: 30px;
      font-weight: 900;
      top: 29%;
      left: 34%;
    }

    .gauge-right {
      color: azure;
      display: flex;
      position: absolute;
      font-size: 28px;
      font-weight: 900;
      top: 29%;
      left: 59.3%;
    }

    canvas {
      display: block;
      margin: 0 auto;
    }

    .deepSleepMessage {
      display: none;
      position: fixed;
      top: 0;
      left: 0;
      width: 100%;
    }
@media (orientation: landscape) {
  :root {
    --switch-width: 70px;
    --switch-height: 140px;
    --knob-size: 30px;
  }

  body {
    height: 100vh;
    overflow: hidden;
  }

  .container {
    height: 100vh;
    display: flex;
    align-items: center;
    justify-content: center;
  }

  /* Gear Switch - Move to top left and make smaller */
  .gear-switch {
    top: 10px;
    right: 10px;
    width: var(--switch-width);
    height: var(--switch-height);
    border: 2px solid #555;
  }

  .gear-track {
    top: 10px;
    width: 4px;
    height: calc(var(--switch-height) - 20px);
  }

  .gear-positions {
    left: calc(var(--switch-width) / -1.8);
    width: calc(var(--switch-width) * 1.8);
  }

  .gear-label {
    font-size: 12px;
    padding: 3px 6px;
    border-radius: 6px;
  }

    .gear-label[data-gear="0"] { top: 5px; left: 20px; }
    .gear-label[data-gear="1"] { top: 55px; left: 20px; }
    .gear-label[data-gear="2"] { top: 105px; left: 20px; }

  .gear-knob {
    width: var(--knob-size);
    height: var(--knob-size);
    border: 2px solid #a93226;
  }

  .gear-knob::before {
    width: 12px;
    height: 12px;
  }

  /* Transmission label - Move to top center */
  .gauge-transmission {
    top: 24%;
    left: 50%;
    transform: translateX(-50%);
    font-size: 16px;
    font-weight: 700;
  }

  /* Main gauge - Make smaller and center */
  .gauge-container {
    width: 240px;
    height: 240px;
    top: 50%;
    left: 50%;
    transform: translate(-50%, -50%);
  }

  .gauge-value {
    font-size: 50px;
    -webkit-text-stroke: 3px #2ecc71;
  }

  .gauge-unit {
    bottom: 80px;
    font-size: 12px;
  }

  .gauge-temperature {
    bottom: 20%;
    left: 47%;
    font-size: 12px;
  }

  .gauge-temperature-unit {
    font-size: 8px;
  }

  .gauge-left {
    font-size: 20px;
    top: 30%;
    left: 30%;
  }

  .gauge-right {
    font-size: 20px;
    top: 30%;
    left: 62%;
  }

  /* Joystick containers - Adjust for landscape */
  .joystick-container {
    bottom: 0;
    height: 200px;
    padding: 0 20px;
  }

  .joystick-left,
  .joystick-right {
    height: 200px;
    width: 45%;
  }

  .joystick {
    bottom: 50px;
    width: 100px;
    height: 100px;
  }

  .joystick-knob {
    width: 60px;
    height: 60px;
  }

  /* Canvas adjustments */
  canvas {
    max-width: 240px;
    max-height: 240px;
  }

  /* Deep sleep message */
  .deepSleepMessage {
    font-size: 14px;
    padding: 10px;
  }
}

/* Specific mobile landscape optimizations */
@media (max-width: 500px) {
  :root {
    --switch-width: 60px;
    --switch-height: 120px;
    --knob-size: 25px;
  }

  .gauge-transmission {
    top: 28%;
    font-size: 14px;
  }

  .gauge-container {
    width: 200px;
    height: 200px;
  }

  .gauge-value {
    font-size: 40px;
    -webkit-text-stroke: 2px #2ecc71;
  }

  .gauge-unit {
    bottom: 70px;
    font-size: 10px;
  }

  .gauge-left {
    font-size: 18px;
    top: 32%;
    left: 28%;
  }

  .gauge-right {
    font-size: 18px;
    top: 32%;
    left: 64%;
  }

  .joystick-container {
    height: 150px;
    padding: 0 15px;
  }

  .joystick-left,
  .joystick-right {
    height: 150px;
  }

  .joystick {
    bottom: 40px;
    width: 80px;
    height: 80px;
  }

  .joystick-knob {
    width: 50px;
    height: 50px;
  }

  .gear-switch {
    top: 5px;
    right: 5px;
  }

  .gear-label {
    font-size: 10px;
    padding: 2px 4px;
  }

  .gear-label[data-gear="0"] { top: 5px; left: 15px; }
  .gear-label[data-gear="1"] { top: 52px; left: 15px; }
  .gear-label[data-gear="2"] { top: 100px; left: 15px; }

  canvas {
    max-width: 200px;
    max-height: 200px;
  }
}

  </style>
</head>
<body>
  <div class="container">
  <div id="deepSleepMessage" class="deepSleepMessage"></div>
    <div class="gear-switch" id="gearSwitch">
      <div class="gear-track"></div>
      <div class="gear-positions">
        <div class="gear-label active" data-gear="0">ECO</div>
        <div class="gear-label" data-gear="1">SPORT+</div>
        <div class="gear-label" data-gear="2">DRIFT</div>
      </div>
      <div class="gear-knob" id="gearKnob"></div>
    </div>

    <div class="gauge-container">
      <span class="gauge-left" id="gauge-left">⇦</span>
      <span class="gauge-right" id="gauge-right">⇨</span>
      <div class="gauge-transmission" id="gauge-transmission">N</div>
      <canvas id="gauge" width="360" height="360"></canvas>
      <div class="gauge-value" id="speed-value">0</div>
      <div class="gauge-unit">Km/h</div>
      <div class="gauge-temperature" id="gauge-temperature">
        <div class="gauge-temperature-value" id="gauge-temperature-value">0</div>
        <div class="gauge-temperature-unit" id="gauge-temperature-unit">°C</div>
      </div>
    </div>

    <div class="joystick-container">
      <div class="joystick-left">
        <div class="joystick" id="joystick-dc">
          <div class="joystick-knob" id="knob-dc"></div>
        </div>
      </div>
      <div class="joystick-right">
        <div class="joystick" id="joystick-servo">
          <div class="joystick-knob" id="knob-servo"></div>
        </div>
      </div>
    </div>
  </div>
  <script>
    const CMD_DC = 0;
    const CMD_SERVO = 1;
    const DATA_GAP = 5;
    const DEBOUNCE_MS = 50;

    let isSendingDC = false;
    let isSendingSERVO = false;
    let preDC = 0;
    let preServo = 90;
    let gaugeSpeed = 0;
    let temperature = 0;
    let lastSentTime = 0;
    let currentGear = 0;
    let currentGaugeMax = null; // Last received gearSpeed
    let currentSpeed = 0;       // Optional: your current motor speed (if you send it)
    let temperatureInterval;

    const knobDC = document.getElementById("knob-dc");
    const knobServo = document.getElementById("knob-servo");
    const joystickDC = document.getElementById("joystick-dc");
    const joystickServo = document.getElementById("joystick-servo");
    const speedValue = document.getElementById("speed-value");
    const gaugeCanvas = document.getElementById("gauge");
    const gaugeCtx = gaugeCanvas.getContext("2d");
    const gaugeTransmission = document.getElementById("gauge-transmission");
    const tempValue = document.getElementById("gauge-temperature-value");
    const tempUnit = document.getElementById("gauge-temperature-unit");
    const gaugeLeft = document.getElementById("gauge-left");
    const gaugeRight = document.getElementById("gauge-right");
    const deepSleepMessage = document.getElementById('deepSleepMessage');

      const gaugeOptions = {
        minSpeed: 0,
        radius: 160,
        centerX: gaugeCanvas.width / 2,
        centerY: gaugeCanvas.height / 2,
        startAngle: Math.PI * 0.75,
        endAngle: Math.PI * 2.25,
        alertSpeeds: [255, 350, 440],
        alertColors: ["#2ecc71", "#f39c12", "#ff0800"],

        // Make maxSpeed dynamic
        get maxSpeed() {
         return currentGaugeMax !== null ? currentGaugeMax : 255;
        }
      };


    drawGauge(0);   

   temperatureInterval = setInterval(function () {
      fetch('/getTemperature', { signal: AbortSignal.timeout(2000) })
        .then(response => response.text())
        .then(temp => {
          temperature = parseInt(temp);
          if (temperature > 0) {
            tempValue.textContent = temperature;
            tempValue.style.display = "block";
            tempUnit.style.display = "block";
          } else {
            tempValue.textContent = "ENG";
            tempValue.style.color = "red";
            tempValue.style.fontSize = "12px";
            tempValue.style.display = "block";
            tempUnit.style.display = "none";
          }
        })
        .catch(error => {
          console.log('getTemperature failed:', error.message);
          tempValue.textContent = "--";
          tempValue.style.color = "grey";
          tempValue.style.display = "block";
          tempUnit.style.display = "none";
        });
    }, 5000);

    const socket = new WebSocket('ws://' + window.location.hostname + ':81/');
    socket.onopen = function() {
      console.log('WebSocket connected');
    };

    socket.onmessage = function(event) {
      const message = event.data;
      console.log('Received:', message);

        if (message.startsWith("gearSpeed:")) {
          const newMaxSpeed = parseInt(message.split(":")[1]);
          if (newMaxSpeed !== currentGaugeMax) {
            currentGaugeMax = newMaxSpeed;
            drawGauge(currentSpeed); // Redraw with new max
          }
        };

      if (message.startsWith("DEEPSLEEP:")) {
        deepSleepMessage.textContent = message.substring(10);
      deepSleepMessage.style.display = 'grid';
      deepSleepMessage.style.textAlign = 'center';
      deepSleepMessage.style.alignItems = 'center';
      deepSleepMessage.style.height = '100vh';
      deepSleepMessage.style.width = '100%';
      deepSleepMessage.style.zIndex = '20';
      deepSleepMessage.style.background = 'darkred';
      deepSleepMessage.style.fontSize = '1.2em';
      deepSleepMessage.style.fontWeight = '800';
      deepSleepMessage.style.fontFamily = 'monospace';
      deepSleepMessage.style.color = 'azure';
      };
    };

      socket.onerror = function(error) {
        console.log('WebSocket error:', error);
      };

      socket.onclose = function() {
      console.log('WebSocket closed');
      // Stop temperature polling
      if (temperatureInterval) {
        clearInterval(temperatureInterval);
        temperatureInterval = null;
        console.log('Stopped getTemperature polling due to WebSocket close');
      }
    };

    function writeBLE(cmd, data) {
      if (socket.readyState === WebSocket.OPEN) {
        if (cmd === CMD_DC) {
          socket.send('M:' + data);
          console.log('Sent: M:' + data);
        } else if (cmd === CMD_SERVO) {
          socket.send('S:' + data);
          console.log('Sent: S:' + data);
        } else if (cmd === 'GEAR') {
          socket.send('G:' + data);
          console.log('Sent: G:' + data);
        }
      } else {
        console.log('WebSocket not open, cannot send:', cmd, data);
      }
    }

    class CarGearComponent {
      #container;
      #knob;
      #currentGear = 0;
      #gearPositions = { 0: 3, 1: 53, 2: 100 };
      #rafId = null;

      constructor(containerId) {
        this.#container = document.getElementById(containerId);
        this.#knob = document.getElementById('gearKnob');
        this.#init();
      }

      #init() {
        this.#setupEventListeners();
      }

      #setupEventListeners() {
        const gearLabels = this.#container.querySelectorAll('.gear-label');
        gearLabels.forEach(label => {
          label.addEventListener('click', () => this.setGear(parseInt(label.dataset.gear)));
        });

        let isDragging = false;
        let touchId = null;

        const handleStart = (e) => {
          isDragging = true;
          e.preventDefault();
          if (e.type === 'touchstart') {
            touchId = e.changedTouches[0].identifier;
          }
        };

        const handleEnd = (e) => {
          isDragging = false;
          touchId = null;
          if (this.#rafId) {
            cancelAnimationFrame(this.#rafId);
            this.#rafId = null;
          }
        };

        const handleMove = (e) => {
          if (!isDragging) return;
          const clientY = e.type === 'touchmove' ? e.touches[0].clientY : e.clientY;
          const rect = this.#container.getBoundingClientRect();
          const y = clientY - rect.top - (this.#knob.offsetHeight / 2);

          const closestGear = Object.entries(this.#gearPositions).reduce((closest, [gear, pos]) => {
            const distance = Math.abs(y - pos);
            return distance < closest.distance ? { gear: parseInt(gear), distance } : closest;
          }, { gear: this.#currentGear, distance: Infinity }).gear;

          this.setGear(closestGear);
        };

        this.#knob.addEventListener('mousedown', handleStart);
        this.#knob.addEventListener('touchstart', handleStart);
        document.addEventListener('mousemove', (e) => {
          if (isDragging && !this.#rafId) {
            this.#rafId = requestAnimationFrame(() => {
              handleMove(e);
              this.#rafId = null;
            });
          }
        });
        document.addEventListener('touchmove', handleMove, { passive: false });
        document.addEventListener('touchend', (e) => {
          for (let i = 0; i < e.changedTouches.length; i++) {
            if (e.changedTouches[i].identifier === touchId) {
              handleEnd(e);
              break;
            }
          }
        }, { passive: false });
      }

      setGear(gear) {
        if (gear < 0 || gear > 2 || this.#currentGear === gear) return;
        this.#currentGear = gear;
        this.#knob.style.top = `${this.#gearPositions[gear]}px`;
        this.#container.querySelectorAll('.gear-label').forEach(label => {
          label.classList.toggle('active', parseInt(label.dataset.gear) === gear);
        });
        writeBLE('GEAR', gear);
        currentGear = gear;
      }

      getCurrentGear() {
        return this.#currentGear;
      }
    }

function setupJoystick(joystick, knob, isVertical, cmdType) {
  let isDragging = false;
  let startX, startY;
  let joystickRect;
  let initialCenterX, initialCenterY;
  let touchId = null;
  const extensionFactor = 3.0;

  const resetKnob = () => {
    knob.style.transform = "translate(-50%, -50%)";
  };

  // Mouse down event
  knob.addEventListener("mousedown", function (e) {
    e.preventDefault();
    if (!isDragging) {
      isDragging = true;
      joystickRect = joystick.getBoundingClientRect();
      initialCenterX = joystickRect.width / 2;
      initialCenterY = joystickRect.height / 2;
      if (cmdType === CMD_DC) {
        isSendingDC = true;
      } else {
        isSendingSERVO = true;
      }
      startX = e.clientX;
      startY = e.clientY;
    }
  });

  // Touch start event
  joystick.addEventListener("touchstart", function (e) {
    e.preventDefault();
    if (touchId === null && e.touches.length > 0) {
      isDragging = true;
      touchId = e.changedTouches[0].identifier;
      joystickRect = joystick.getBoundingClientRect();
      initialCenterX = joystickRect.width / 2;
      initialCenterY = joystickRect.height / 2;
      if (cmdType === CMD_DC) {
        isSendingDC = true;
      } else {
        isSendingSERVO = true;
      }
      let touch = e.changedTouches[0];
      startX = touch.clientX;
      startY = touch.clientY;
    }
  });

  // Mouse move event
  document.addEventListener("mousemove", function (e) {
    if (!isDragging) return;
    e.preventDefault();
    handleMovement(e.clientX, e.clientY);
  });

  // Touch move event
  document.addEventListener("touchmove", function (e) {
    if (!isDragging || touchId === null) return;
    e.preventDefault();
    let touch = null;
    for (let i = 0; i < e.changedTouches.length; i++) {
      if (e.changedTouches[i].identifier === touchId) {
        touch = e.changedTouches[i];
        break;
      }
    }
    if (!touch) return;
    handleMovement(touch.clientX, touch.clientY);
  }, { passive: false });

  // Mouse up event
  document.addEventListener("mouseup", function (e) {
    if (isDragging) {
      endDrag();
    }
  });

  // Touch end event
  document.addEventListener("touchend", function(e) {
    for (let i = 0; i < e.changedTouches.length; i++) {
      if (e.changedTouches[i].identifier === touchId) {
        endDrag();
        break;
      }
    }
  }, { passive: false });

  function handleMovement(clientX, clientY) {
    const deltaX = clientX - startX;
    const deltaY = clientY - startY;
    const joystickRadius = joystickRect.width / 2;
    const knobRadius = knob.offsetWidth / 2;
    const maxDistance = (joystickRadius - knobRadius) * extensionFactor;

    if (isVertical) {
      let offsetY = Math.min(Math.max(deltaY, -maxDistance), maxDistance);
      knob.style.transform = `translate(-50%, -50%) translateY(${offsetY}px)`;
      let valueToSend = Math.round((offsetY / maxDistance) * -(currentGaugeMax || 255));
       prepareSendingData(cmdType, valueToSend);
    } else {
      let offsetX = Math.min(Math.max(deltaX, -maxDistance), maxDistance);
      knob.style.transform = `translate(-50%, -50%) translateX(${offsetX}px)`;
      let servoAngle = Math.round(90 + (offsetX / maxDistance) * 90);
      servoAngle = Math.max(0, Math.min(180, servoAngle));
      prepareSendingData(cmdType, servoAngle);
    }
  }

  function endDrag() {
    isDragging = false;
    touchId = null;
    resetKnob();
    if (cmdType === CMD_DC) {
      isSendingDC = false;
      preDC = 0;
      writeBLE(cmdType, 0);
      updateSpeedometer(0);
      console.log("Motor reset to 0");
    } else {
      isSendingSERVO = false;
      preServo = 90;
      writeBLE(cmdType, 90);
      gaugeRight.style.webkitTextStroke = "0px azure";
      gaugeLeft.style.webkitTextStroke = "0px azure";
      console.log("Servo reset to 90");
    }
  }
}

    setupJoystick(joystickDC, knobDC, true, CMD_DC);
    setupJoystick(joystickServo, knobServo, false, CMD_SERVO);

    function prepareSendingData(cmd, data) {
      const now = Date.now();
      if (now - lastSentTime < DEBOUNCE_MS) return;
      lastSentTime = now;

      if (cmd === CMD_DC) {
        updateSpeedometer(data);
        if (Math.abs(data - preDC) < DATA_GAP) return;
        preDC = data;
        console.log("DC Motor:", data);
        writeBLE(cmd, data);
      } else if (cmd === CMD_SERVO) {
        if (data > 90) {
          gaugeRight.style.webkitTextStroke = "1px #ff8c00";
          gaugeLeft.style.webkitTextStroke = "0px azure";
        } else if (data < 90) {
          gaugeLeft.style.webkitTextStroke = "1px #ff8c00";
          gaugeRight.style.webkitTextStroke = "0px azure";
        } else {
          gaugeRight.style.webkitTextStroke = "0px azure";
          gaugeLeft.style.webkitTextStroke = "0px azure";
        }
        if (Math.abs(data - preServo) < DATA_GAP) return;
        preServo = data;
        console.log("Servo Angle:", data);
        writeBLE(cmd, data);
      }
    }

    function updateSpeedometer(rawValue) {
      if (rawValue > 0) {
        gaugeTransmission.textContent = "D";
        gaugeTransmission.style.color = "Green";
        speedValue.style.webkitTextStrokeColor = "#00ff00";
      } else if (rawValue < 0) {
        gaugeTransmission.textContent = "R";
        gaugeTransmission.style.color = "#ff5349";
        speedValue.style.webkitTextStrokeColor = "Red";
      } else {
        gaugeTransmission.textContent = temperature > 0 ? "N" : "P";
        gaugeTransmission.style.color = "Gray";
        speedValue.style.webkitTextStrokeColor = "Grey";
      }
      drawGauge(Math.abs(rawValue));
      speedValue.textContent = rawValue;
    }

    function drawGauge(speed) {
      gaugeCtx.clearRect(0, 0, gaugeCanvas.width, gaugeCanvas.height);
      gaugeCtx.beginPath();
      gaugeCtx.arc(
        gaugeOptions.centerX,
        gaugeOptions.centerY,
        gaugeOptions.radius,
        gaugeOptions.startAngle,
        gaugeOptions.endAngle
      );
      gaugeCtx.lineWidth = 20;
      gaugeCtx.strokeStyle = "#FFFFFF";
      gaugeCtx.stroke();
     
        console.log("gaugeOptions", gaugeOptions.maxSpeed);

      const speedRatio = speed / gaugeOptions.maxSpeed;
      const speedAngle =
        gaugeOptions.startAngle +
        speedRatio * (gaugeOptions.endAngle - gaugeOptions.startAngle);

      let color = "#2ecc71";
      for (let i = gaugeOptions.alertSpeeds.length - 1; i >= 0; i--) {
        if (speed >= gaugeOptions.alertSpeeds[i]) {
          color = gaugeOptions.alertColors[i];
          break;
        }
      }

      if (speed > 0) {
        gaugeCtx.beginPath();
        gaugeCtx.arc(
          gaugeOptions.centerX,
          gaugeOptions.centerY,
          gaugeOptions.radius,
          gaugeOptions.startAngle,
          speedAngle
        );
        gaugeCtx.lineWidth = 20;
        gaugeCtx.strokeStyle = color;
        gaugeCtx.stroke();
      }
  
      for (let i = 0; i <= gaugeOptions.maxSpeed; i += 25) {
        const tickAngle =
          gaugeOptions.startAngle +
          (i / gaugeOptions.maxSpeed) *
          (gaugeOptions.endAngle - gaugeOptions.startAngle);
        const outerX =
          gaugeOptions.centerX +
          (gaugeOptions.radius + 15) * Math.cos(tickAngle);
        const outerY =
          gaugeOptions.centerY +
          (gaugeOptions.radius + 15) * Math.sin(tickAngle);
        const innerX =
          gaugeOptions.centerX +
          (gaugeOptions.radius - 25) * Math.cos(tickAngle);
        const innerY =
          gaugeOptions.centerY +
          (gaugeOptions.radius - 25) * Math.sin(tickAngle);
        gaugeCtx.beginPath();
        gaugeCtx.moveTo(innerX, innerY);
        gaugeCtx.lineTo(outerX, outerY);
        gaugeCtx.lineWidth = 2;
        gaugeCtx.strokeStyle = "#c90016";
        gaugeCtx.stroke();

        if (i % 25 === 0) {
          const labelX =
            gaugeOptions.centerX +
            (gaugeOptions.radius - 40) * Math.cos(tickAngle);
          const labelY =
            gaugeOptions.centerY +
            (gaugeOptions.radius - 40) * Math.sin(tickAngle);
          gaugeCtx.font = "14px Arial";
          gaugeCtx.fillStyle = "#888";
          gaugeCtx.textAlign = "center";
          gaugeCtx.textBaseline = "middle";
          gaugeCtx.fillText(i.toString(), labelX, labelY);
        }
      }
    }

    const gearComponent = new CarGearComponent('gearSwitch');
  </script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleGetTemperature() {
  int temperature = (int)temperatureRead();
  server.send(200, "text/plain", String(temperature));
}

void setup() {
  Serial.begin(115200);
  pinMode(motorAin1Pin, OUTPUT);
  pinMode(motorAin2Pin, OUTPUT);
  pinMode(motorPwmPin, OUTPUT);
  ledcAttach(motorPwmPin, motorPwmFreq, motorPwmResolution);
  ledcAttachChannel(motorPwmPin, motorPwmFreq, motorPwmResolution, motorPwmChannel);
  myServo.attach(servoPin, 1000, 2000);
  myServo.write(SERVO_CENTER_ANGLE);
  steering = SERVO_CENTER_ANGLE;
  WiFi.softAP(ssid, password);
  esp_wifi_set_max_tx_power(40);
   lastCommandTime = millis(); // Initialize timer
  Serial.print("Access Point IP: ");
  Serial.println(WiFi.softAPIP());
  server.on("/", handleRoot);
  server.on("/getTemperature", handleGetTemperature);
  server.begin();
  Serial.println("Web server started");
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket server started");
}

void loop() {
  webSocket.loop();
  server.handleClient();
  unsigned long now = millis();
  if (now - lastServoUpdate >= SERVO_UPDATE_INTERVAL) {
    myServo.write(steering);
    lastServoUpdate = now;
  }
  // Check for inactivity
  if (now - lastCommandTime > INACTIVITY_TIMEOUT && WiFi.softAPgetStationNum() > 0) {
    Serial.println("No commands for 10 minutes, entering deep sleep");
    webSocket.broadcastTXT("DEEPSLEEP:Controller is in deepSleep due to inactivity, kindly restart or power on/off it again.");
    delay(800); // Allow message to send
    setMotorSpeed(0);
    myServo.detach();
    digitalWrite(2, LOW); // LED off
    WiFi.softAPdisconnect(true);
    esp_deep_sleep_start();
  }
  delay(10);
}
