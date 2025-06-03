#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ESP32Servo.h>

const char *ssid = "HA_CAR";
const char *password = "123456780";

//Designed and Developed by Hisham Ahmed
//social hishamahmedx

WebSocketsServer webSocket = WebSocketsServer(81); // WebSocket on port 81
WebServer server(80); // HTTP server for HTML and temperature

// Motor pins (TB6612FNG)
const int motorAin1Pin = 6;  // Motor IN1
const int motorAin2Pin = 7;  // Motor IN2
const int motorPwmPin = 5;   // Motor PWM
const int servoPin = 9;      // Servo pin (DSM005)

// PWM settings for motor
const int motorPwmFreq = 1000;  // 1kHz frequency
const int motorPwmResolution = 8; // 8-bit resolution (0-255)
const int motorPwmChannel = 0;   // PWM channel for motor

// Motor configuration
const int MOTOR_MIN_PWM = 20;           // Minimum PWM to start motor
const int MOTOR_MAX_FORWARD_SPEED = 255; // Max forward speed
const int MOTOR_MAX_REVERSE_SPEED = 255; // Max reverse speed
const int MOTOR_FORWARD_PIN1 = HIGH;    // motorAin1Pin state for forward
const int MOTOR_FORWARD_PIN2 = LOW;     // motorAin2Pin state for forward
const int MOTOR_REVERSE_PIN1 = LOW;     // motorAin1Pin state for reverse
const int MOTOR_REVERSE_PIN2 = HIGH;    // motorAin2Pin state for reverse

// Servo configuration (DSM005: 50Hz, 1000-2000µs)
const int SERVO_MIN_ANGLE = 60;     // Left: 60°
const int SERVO_MAX_ANGLE = 180;    // Right: 180°
const int SERVO_CENTER_ANGLE = 120; // Center
const int SERVO_LEFT_ANGLE = SERVO_MAX_ANGLE;
const int SERVO_RIGHT_ANGLE = SERVO_MIN_ANGLE;
const int SERVO_RANGE = abs(SERVO_RIGHT_ANGLE - SERVO_LEFT_ANGLE);
const int SERVO_HALF_RANGE = SERVO_RANGE / 2;

Servo myServo;
int steering = SERVO_CENTER_ANGLE; // Current servo position
unsigned long lastServoUpdate = 0; // For periodic servo refresh
const unsigned long SERVO_UPDATE_INTERVAL = 20; // 20ms = 50Hz

// Map UI input (0-180, 90=center) to servo angle
int mapUIToServoAngle(int uiValue) {
  uiValue = constrain(uiValue, 0, 180);
  int servoAngle;
  if (uiValue <= 90) {
    servoAngle = map(uiValue, 0, 90, SERVO_LEFT_ANGLE, SERVO_CENTER_ANGLE);
  } else {
    servoAngle = map(uiValue, 90, 180, SERVO_CENTER_ANGLE, SERVO_RIGHT_ANGLE);
  }
  return constrain(servoAngle, min(SERVO_LEFT_ANGLE, SERVO_RIGHT_ANGLE), 
                   max(SERVO_LEFT_ANGLE, SERVO_RIGHT_ANGLE));
}

// Map UI input (-255 to 255) to motor PWM
int mapUIToMotorPWM(int uiValue) {
  if (uiValue == 0) {
    return 0;
  } else if (uiValue > 0) {
    return map(uiValue, 1, 255, MOTOR_MIN_PWM, MOTOR_MAX_FORWARD_SPEED);
  } else {
    return -map(abs(uiValue), 1, 255, MOTOR_MIN_PWM, MOTOR_MAX_REVERSE_SPEED);
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
    if (message.startsWith("S:")) { // Steering command
      unsigned long startTime = micros();
      int uiValue = message.substring(2).toInt();
      int newSteering = mapUIToServoAngle(uiValue);
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
    } else if (message.startsWith("M:")) { // Motor command
      int uiValue = message.substring(2).toInt();
      int pwmValue = mapUIToMotorPWM(uiValue);
      setMotorSpeed(pwmValue);
      Serial.print("Speed set to: ");
      Serial.print(uiValue);
      Serial.print(" (PWM: ");
      Serial.print(pwmValue);
      Serial.println(")");
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
  <title>RC Controller</title>
    <style>
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

      .gauge-transmission{
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

        .gauge-temperature{
            color: grey;
            display: flex;
            position: absolute;
            bottom: 18%;
            left: 46.9%;
        }
        .gauge-temperature-unit{
            font-size: 10px;
        }


        .gauge-left{
         color: azure;
         display: flex;
         position: absolute;
         font-size: 30px;
         font-weight: 900;
         top: 29%;
         left: 34%;
         
        }
        .gauge-right{
            color: azure;
             display: flex;
         position: absolute;
         font-size: 28px;
         font-weight: 900;
          top: 29%;
         left: 59.3%;

        }
      /* Canvas gauge styling */
      canvas {
        display: block;
        margin: 0 auto;
      }
    </style>
</head>
<body>
    <div class="container">

      <div class="gauge-container">
        <span class="gauge-left" id="gauge-left">&#8678;</span>
        <span class="gauge-right" id="gauge-right">&#8680;</span>
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
    let preServo = 90; // Initialize to center (maps to 120°)
    let gaugeSpeed = 0;
    let temperature = 0;
    let lastSentTime = 0;

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

    const gaugeOptions = {
      minSpeed: 0,
      maxSpeed: 255,
      radius: 160,
      centerX: gaugeCanvas.width / 2,
      centerY: gaugeCanvas.height / 2,
      startAngle: Math.PI * 0.75,
      endAngle: Math.PI * 2.25,
      alertSpeeds: [40, 100, 170],
      alertColors: ["#2ecc71", "#f39c12", "#ff0800"],
    };

    drawGauge(0);

    setInterval(function () {
      fetch('/getTemperature')
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
        });
    }, 5000);

    const socket = new WebSocket('ws://' + window.location.hostname + ':81/');
    socket.onopen = function() {
      console.log('WebSocket connected');
    };
    socket.onmessage = function(event) {
      console.log('Received:', event.data);
    };

    function writeBLE(cmd, data) {
      if (cmd === CMD_DC) {
        socket.send('M:' + data);
        console.log('Sent: M:' + data);
      } else if (cmd === CMD_SERVO) {
        socket.send('S:' + data);
        console.log('Sent: S:' + data);
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

      document.addEventListener("touchend", function (e) {
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
          let valueToSend = Math.round((offsetY / maxDistance) * -255);
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
          writeBLE(cmdType, 0); // Force motor reset
          updateSpeedometer(0);
          console.log("Motor reset to 0");
        } else {
          isSendingSERVO = false;
          preServo = 90;
          writeBLE(cmdType, 90); // Force servo reset
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
  </script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

// HTTP handler for temperature
void handleGetTemperature() {
  int temperature = (int)temperatureRead();
  server.send(200, "text/plain", String(temperature));
}

void setup() {
  Serial.begin(115200);

  // Setup motor pins
  pinMode(motorAin1Pin, OUTPUT);
  pinMode(motorAin2Pin, OUTPUT);
  pinMode(motorPwmPin, OUTPUT);
  ledcAttach(motorPwmPin, motorPwmFreq, motorPwmResolution);
  ledcAttachChannel(motorPwmPin, motorPwmFreq, motorPwmResolution, motorPwmChannel);

  // Setup servo (DSM005: 1000-2000µs, 50Hz)
  myServo.attach(servoPin, 1000, 2000); // Auto-assigns PWM channel
  myServo.write(SERVO_CENTER_ANGLE);
  steering = SERVO_CENTER_ANGLE;

  // WiFi Access Point
  WiFi.softAP(ssid, password);
  Serial.print("Access Point IP: ");
  Serial.println(WiFi.softAPIP());

  // Setup web routes
  server.on("/", handleRoot);
  server.on("/getTemperature", handleGetTemperature);
  server.begin();
  Serial.println("Web server started");

  // Start WebSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket server started");
}

void loop() {
  webSocket.loop();
  server.handleClient();
  unsigned long now = millis();
  if (now - lastServoUpdate >= SERVO_UPDATE_INTERVAL) {
    myServo.write(steering); // Refresh servo at 50Hz
    lastServoUpdate = now;
  }
}