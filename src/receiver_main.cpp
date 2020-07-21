#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <Servo.h>
#include <nrfrc_config.h>

#define MOVE_FORWARD 'F'
#define MOVE_BACKWARD 'B'
#define STOP 'S'

#define STEERING_MIN_ANGLE 130 //150
#define STEERING_MAX_ANGLE 50  //90
#define MOTOR_MIN_SPEED_PWM 20
#define MOTOR_MAX_SPEED_PWM 250

#define STICK_MIN -1023
#define STICK_MAX 1023

#define SERVO_PIN 6
#define MOTOR_LPWM 5
#define MOTOR_RPWM 3
#define MOTOR_THROTTLE_THRESHOLD 5

byte SOFT_START_MULTIPLEXER = 80;
byte SOFT_START_CURRENT_SPEED_MULTIPLEXER = 1;
byte SOFT_START_BURST_ADD_PERCENTAGE = 20;

struct StickState
{
  int x;
  int y;
};
struct ControllerState
{
  StickState leftStick;
  StickState rightStick;
};

ControllerState controllerState;

// RADIO
const byte transmitterAddress[6] = "clie1";
const byte receiverAddress[6] = "serv1";
RF24 radio(8, 7);

Servo servo;

const uint32_t timeoutMs = 1000;
uint32_t connectionLostTimer = millis();
uint32_t loopTimer = millis();

void movementController(int joystickX, int joystickY);
void mainMotorController(int throttle, int maxSpeed);
int getMotorSpeed(int throttle, int maxThrottle);
void motorController(int direction,
                     byte speed,
                     byte lpwPin,
                     byte rpwPin);
int smoothThrottleChange(int throttle);
bool equal(uint32_t val1, uint32_t val2, int sigma);

void setup()
{
  Serial.begin(9600);
  servo.attach(SERVO_PIN);

  pinMode(MOTOR_LPWM, OUTPUT);
  pinMode(MOTOR_RPWM, OUTPUT);

  radio.begin();
  radio.setChannel(NRFRC_CONFIG_RADIO_CHANNEL);
  radio.setDataRate(NRFRC_CONFIG_RADIO_DATA_RATE);
  radio.setPALevel(RF24_PA_MAX);
  radio.setAutoAck(NRFRC_CONFIG_RADIO_ACK_ENABLED);

  radio.openReadingPipe(1, receiverAddress);
  radio.startListening();

  Serial.println("Listening...");
  connectionLostTimer = millis();
}

void loop()
{
  loopTimer = millis();
  if (millis() - connectionLostTimer > timeoutMs)
  {
    Serial.println("Connection lost");
    movementController(0, 0);
    connectionLostTimer = millis();
  }

  if (radio.available())
  {
    connectionLostTimer = millis();

    while (radio.available())
    {                                                        // While there is data ready
      radio.read(&controllerState, sizeof(controllerState)); // Get the payload
    }
    movementController(controllerState.leftStick.x, controllerState.rightStick.y);
  }

  // uint32_t looptime = millis() - loopTimer;
  // if (looptime > 5) {
  //   Serial.println("Loop: " + String(looptime));
  // }
}

void movementController(int joystickX, int joystickY)
{
  if (joystickX < STICK_MIN || joystickX > STICK_MAX || joystickY < STICK_MIN || joystickY > STICK_MAX)
  {
    // Serial.println('Stick overload!');
    return;
  }

  mainMotorController(joystickY, STICK_MAX);
  servo.write(map(joystickX, STICK_MIN, STICK_MAX, STEERING_MIN_ANGLE, STEERING_MAX_ANGLE));
}

void mainMotorController(int throttle, int maxSpeed)
{
  static int direction = STOP;
  const int smoothedThrottle = smoothThrottleChange(throttle);

  const int speed = getMotorSpeed(smoothedThrottle, STICK_MAX);
  const byte absSpeed = abs(speed);
  // if (speed != 0) Serial.println(speed);
  if (speed > 0)
    direction = MOVE_FORWARD;
  if (speed < 0)
    direction = MOVE_BACKWARD;
  if (speed == 0)
    direction = STOP;

  // Serial.println("sp: " + String(speed) + "; dir: " + String(direction) + "; smSp: " + String(absSpeed));
  return motorController(direction, absSpeed, MOTOR_LPWM, MOTOR_RPWM);
}

int getMotorSpeed(int throttle, int maxThrottle)
{
  if (throttle > MOTOR_THROTTLE_THRESHOLD)
  {
    return map(throttle, 0, maxThrottle, MOTOR_MIN_SPEED_PWM, MOTOR_MAX_SPEED_PWM);
  }
  if (throttle < -MOTOR_THROTTLE_THRESHOLD)
    return (-1) * getMotorSpeed((-1) * throttle, maxThrottle);
  return 0;
}

void motorController(int direction,
                     byte speed,
                     byte lpwPin,
                     byte rpwPin)
{
  return;
  switch (direction)
  {
  case MOVE_FORWARD:
    analogWrite(lpwPin, speed);
    analogWrite(rpwPin, 0);
    break;
  case MOVE_BACKWARD:
    analogWrite(lpwPin, 0);
    analogWrite(rpwPin, speed);
    break;
  case STOP:
    analogWrite(lpwPin, 0);
    analogWrite(rpwPin, 0);
    break;
  default:
    break;
  }
}

// #define STICK_MIN -1023
// #define STICK_MAX 1023
const uint32_t SM_MAX_THROTTLE = 102300;
const int SM_MAX_THROTTLE_SPEED = 2000;        // 100 * throttle/ms
const int SM_THROTTLE_ACCELERATION = 30;       // 100 * throttle/ms2
const int SIGMA = SM_MAX_THROTTLE_SPEED * 1.2; // * 100

int smoothThrottleChange(int rawThrottle)
{
  static uint32_t smoothStartUpTimer = millis();
  static uint32_t currentThrottle = map(rawThrottle, STICK_MIN, STICK_MAX, 0, SM_MAX_THROTTLE);
  static int32_t throttleSpeed = 0;

  const uint32_t throttle = map(rawThrottle, STICK_MIN, STICK_MAX, 0, SM_MAX_THROTTLE);
  const int time = millis() - smoothStartUpTimer;

  const bool areThrottlesEqual = equal(throttle, currentThrottle, SIGMA);

  if (throttle > currentThrottle && !areThrottlesEqual)
  {
    throttleSpeed += time * SM_THROTTLE_ACCELERATION;
  }
  if (throttle < currentThrottle && !areThrottlesEqual)
  {
    throttleSpeed -= time * SM_THROTTLE_ACCELERATION;
  }
  if (equal(throttleSpeed, 0, SM_THROTTLE_ACCELERATION * 2))
    throttleSpeed = 0;

  if (areThrottlesEqual && throttleSpeed > 0)
  {
    throttleSpeed = max(throttleSpeed - SM_THROTTLE_ACCELERATION, 0);
  }
  if (areThrottlesEqual && throttleSpeed < 0)
  {
    throttleSpeed = min(throttleSpeed + SM_THROTTLE_ACCELERATION, 0);
  }
  if (abs(throttleSpeed) > SM_MAX_THROTTLE_SPEED)
  {
    throttleSpeed = SM_MAX_THROTTLE_SPEED * throttleSpeed / abs(throttleSpeed);
  }

  if (!areThrottlesEqual)
  {
    if (throttleSpeed < 0 && abs(throttleSpeed) > currentThrottle)
    {
      currentThrottle = 0;
    }
    else
    {
      currentThrottle += throttleSpeed;
      currentThrottle = min(currentThrottle, SM_MAX_THROTTLE);
    }
  }
  else
  {
    // Serial.println(String(currentThrottle) + " " + String(throttleSpeed) + " " + String((int)(currentThrottle + throttleSpeed)) + " " + String(currentThrottle + throttleSpeed));

    currentThrottle = throttle;
  }

  currentThrottle = max(currentThrottle, 0);
  currentThrottle = min(currentThrottle, SM_MAX_THROTTLE);

  const int resultThrottle = map(currentThrottle, 0, SM_MAX_THROTTLE, STICK_MIN, STICK_MAX);
  const int normalizedThrottle = map(throttle, 0, SM_MAX_THROTTLE, STICK_MIN, STICK_MAX);
  // if (resultThrottle != normalizedThrottle)
    // Serial.println(String(currentThrottle) + " " + String(throttle) + " " + String(throttleSpeed) + " " + String(time));
  // Serial.println(resultThrottle);

  smoothStartUpTimer = millis();
  return resultThrottle;
}

bool equal(uint32_t val1, uint32_t val2, int sigma)
{
  return val1 == val2 ||
         ((val1 > val2) && (val1 < sigma || (val1 - sigma) <= val2)) ||
         ((val1 < val2) && (val1 + sigma) >= val2);
}
