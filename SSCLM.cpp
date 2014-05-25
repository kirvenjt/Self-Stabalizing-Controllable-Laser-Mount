// Self Stabilizing Controllable Laser Mount
// Amy Pickens, Nate Honold, Tucker Kirven
#include <fstream>
#include <iostream>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "MPUfiles/I2Cdev.h"
#include "MPUfiles/MPU6050_6Axis_MotionApps20.h"
#include <pthread.h>


// struct to hold rotation in degrees about X, Y and Z axis
struct XYZposition {
  int x;
  int y;
  int z;
};

// enum for the mode the device is in
enum mode { MODE_CONTROLLABLE, MODE_STABILIZE, MODE_COMBINED };

// forward declarations
void initMPU(MPU6050 mpu);
void calculateServoPos(struct XYZposition *base, struct XYZposition *controller,
                       mode deviceMode);
void getXYZ(MPU6050 *mpu, struct XYZposition *pos);
typedef int SERVO;
void setServo(SERVO servoNum, int position);

// global to hold the positions the servo should be in
// set by thread1 and read by thread2
// Karsai wants the value passed to thread2 to be a
// change in the current position - not an absolute value
struct XYZposition servoPositions;

struct XYZposition lockPosition;

// need to change address of one or both MPUs
// they will both have default address out of the box
// changing them once should be saved on the device
MPU6050 baseMPU(MPU6050_ADDRESS_AD0_HIGH), controlMPU;

// global mode to be set by thread3 and read by thread1
enum mode deviceMode;
// MPU control/status vars
bool dmpReady = false; // set true if DMP init was successful
uint8_t mpuIntStatus;  // holds actual interrupt status byte from MPU
uint8_t devStatus; // return status after each device operation (0 = success, !0
                   // = error)
uint16_t packetSize;    // expected DMP packet size (default is 42 bytes)
uint16_t fifoCount;     // count of all bytes currently in FIFO
uint8_t fifoBuffer[64]; // FIFO storage buffer

// orientation/motion vars
Quaternion q;   // [w, x, y, z]         quaternion container
VectorInt16 aa; // [x, y, z]            accel sensor measurements
VectorInt16
    aaReal; // [x, y, z]            gravity-free accel sensor measurements
VectorInt16
    aaWorld; // [x, y, z]            world-frame accel sensor measurements
VectorFloat gravity; // [x, y, z]            gravity vector
float euler[3];      // [psi, theta, phi]    Euler angle container
float
    ypr[3]; // [yaw, pitch, roll]   yaw/pitch/roll container and gravity vector

/* Servo values for TG9 servos: */
/* Servo 0 degree angle pulse high time in msec */
#define SRV_0 0.45
/* Servo 180 degree angle pulse high time in msec */
#define SRV_180 2.45

/* Pulse repetition frequency in Hz */
#define FRQ 50.0f
/* Pulse period in msec */
#define PER (1.0E3 / FRQ)

#define SERVO_MIN 15
#define SERVO_MAX (180 - SERVO_MIN)

SERVO servos[3] = {0, 1, 2};

pthread_mutex_t servoPosMutex;
std::ofstream servoDriverFile;
static void *thread1function(void *arg) {

  struct XYZposition basePosition, controllerPosition;
  while(1){
  if (deviceMode == MODE_STABILIZE || deviceMode == MODE_COMBINED)
    getXYZ(&baseMPU, &basePosition);
  usleep(1000);
  if (deviceMode == MODE_CONTROLLABLE || deviceMode == MODE_COMBINED)
    getXYZ(&controlMPU, &controllerPosition);
  
  // calculate neccesary servo position and writes to the servoPositions
  // global variable
  calculateServoPos(&basePosition, &controllerPosition, deviceMode);
  }
  return (void *)0;
}
static void *thread2function(void *arg) {

  
  int servoPosX, servoPosY, servoPosZ;

  while (1) {
    pthread_mutex_lock(&servoPosMutex);
    servoPosX = servoPositions.x;
    servoPosY = servoPositions.y;
    servoPosZ = servoPositions.z;
    pthread_mutex_unlock(&servoPosMutex);

    //std::cout << "x: "<<servoPosX<< " y: "<<servoPosY<< " z: "<<servoPosZ<<std::endl;
    setServo(servos[0], servoPosX);
    setServo(servos[1], servoPosY);
    setServo(servos[2], servoPosZ);
    usleep(100);
  }

  return (void *)0;
}

int main() {

  
  initMPU(controlMPU);
  usleep(100000);
initMPU(baseMPU);
  usleep(100000);
lockPosition.x = 90;
lockPosition.y = 90;
lockPosition.z = 90;

  deviceMode = MODE_COMBINED ;
//opens file that controls servo motors
  servoDriverFile.open ("/dev/servoblaster");
  pthread_mutexattr_t mutexattr;
  pthread_mutexattr_init(&mutexattr);
  pthread_mutex_init(&servoPosMutex, &mutexattr);
  pthread_mutexattr_destroy(&mutexattr);

  pthread_t thread1, thread2;
  pthread_attr_t myattr;

  pthread_attr_init(&myattr);

  // thread1 gets MPU values and calculates desired servo position
  pthread_create(&thread1, &myattr, thread1function, (void *)0);
  // thread2 gets desired servo positions and sets servos
  pthread_create(&thread2, &myattr, thread2function, (void *)0);
  pthread_attr_destroy(&myattr);

  pthread_join(thread1, 0);
  pthread_join(thread2, 0);
  return 0;
}

void initMPU(MPU6050 mpu) {
  // initialize device
  printf("Initializing I2C devices...\n");
  mpu.initialize();

  // verify connection
  printf("Testing device connections...\n");
  printf(mpu.testConnection() ? "MPU6050 connection successful\n"
                              : "MPU6050 connection failed\n");

  // load and configure the DMP
  printf("Initializing DMP...\n");
  devStatus = mpu.dmpInitialize();

  // make sure it worked (returns 0 if so)
  if (devStatus == 0) {
    // turn on the DMP, now that it's ready
    printf("Enabling DMP...\n");
    mpu.setDMPEnabled(true);

    // enable Arduino interrupt detection
    // Serial.println(F("Enabling interrupt detection (Arduino external
    // interrupt 0)..."));
    // attachInterrupt(0, dmpDataReady, RISING);
    mpuIntStatus = mpu.getIntStatus();

    // set our DMP Ready flag so the main loop() function knows it's okay to use
    // it
    printf("DMP ready!\n");
    dmpReady = true;

    // get expected DMP packet size for later comparison
    packetSize = mpu.dmpGetFIFOPacketSize();
  } else {
    // ERROR!
    // 1 = initial memory load failed
    // 2 = DMP configuration updates failed
    // (if it's going to break, usually the code will be 1)
    printf("DMP Initialization failed (code %d)\n", devStatus);
  }
}
void getXYZ(MPU6050 *mpu, struct XYZposition *pos) {
  // if programming failed, don't try to do anything
  if (!dmpReady)
    return;
  // get current FIFO count
  fifoCount = mpu->getFIFOCount();

  if (fifoCount == 1024) {
    // reset so we can continue cleanly
    mpu->resetFIFO();
    printf("FIFO overflow!\n");

    // otherwise, check for DMP data ready interrupt (this should happen
    // frequently)
  } else if (fifoCount >= 42) {
    // read a packet from FIFO
    mpu->getFIFOBytes(fifoBuffer, packetSize);

    // display Euler angles in degrees
    mpu->dmpGetQuaternion(&q, fifoBuffer);
    mpu->dmpGetGravity(&gravity, &q);
    mpu->dmpGetYawPitchRoll(ypr, &q, &gravity);
    //    printf("ypr  %7.2f %7.2f %7.2f    \n",90+ ypr[0] * 180 / M_PI, 90+ypr[1] * 180 / M_PI,90+ ypr[2] * 180 / M_PI);
  }
  pos->x =(ypr[0] * 180 / M_PI)+90;
  pos->y =(ypr[1] * 180 / M_PI)+90;
  pos->z = (ypr[2] * 180 / M_PI)+90;
}

int boundServo(int pos) {
  if (pos > SERVO_MAX) {
    return SERVO_MAX;
  } else if (pos < SERVO_MIN) {
    return SERVO_MIN;
  } else {
    return pos;
  }
}

void calculateServoPos(struct XYZposition *base, struct XYZposition *controller,
                       mode deviceMode) {

  int cx = controller->x;
  int cy = controller->y;
  int cz = controller->z;
  int bx = base->x;
  int by = base->y;
  int bz = base->z;

  int x, y, z;

  switch (deviceMode) {
  case MODE_CONTROLLABLE:
    // simply sets according to controller
    break;

  case MODE_STABILIZE:
    
    x = (2 * lockPosition.x) - bx; // lockPosition.x - (bx - lockPosition.x)
    y = (2 * lockPosition.y) - by;
    z = (2 * lockPosition.z) - bz;

    break;

  case MODE_COMBINED:
    x = (2 * cx) - bx; // cx - (bx - cx)
    y = (2 * cy) - by;
    z = (2 * cz) - bz;

    break;
  default:
    x = 50;
    y = 50;
    z = 50;
    break;
  }

  x = boundServo(x);
  y = boundServo(y);
  z = boundServo(z);
std::cout<<bx<< " "<< by<< " "<< bz;
    std::cout<<" "<<x<<" "<<y<<" "<<z<<" "<<std::endl;
  
  x = (int)(x / 1.8);
  y = (int)(y / 1.8);
  z = (int)(z / 1.8);
  
  pthread_mutex_lock(&servoPosMutex);
  servoPositions.x = x;
  servoPositions.y = y;
  servoPositions.z = z;
  pthread_mutex_unlock(&servoPosMutex);
}

void setServo(SERVO servoNum, int position) {
  if (servoNum == 0)
    position = 50;
  servoDriverFile<<servoNum<<"="<<position<<"%"<<std::endl;
  //   float SM_1_duty; /* Servomotor , connect to ePWM0A */
  // SM_1_duty =
  //     100.0 -
  //     ((SRV_0 / PER) + (position / 180.0) * ((SRV_180 - SRV_0) / PER)) * 100.0;
  // printf("Angle : %d , duty : %f\n", position, SM_1_duty);
  // BBBIO_PWMSS_Setting(BBBIO_PWMSS0, FRQ, SM_1_duty, SM_1_duty); /* Set up PWM */

}


// 3 buttons- 1 for each mode ? 
void buttons() { 

  // mode 1- MODE_CONTROLLABLE
  // if button1 pushed (and released) 
  // deviceMode = MODE_CONTROLLABLE; 

  // mode 2- MODE_STABILIZE
  // if button2 pushed (and released) 
  // deviceMode = MODE_STABILIZE; 

  // mode 3- MODE_COMBINED
  // if button3 pushed (and released) 
  // deviceMode = MODE_COMBINED; 

  // libraries - BCM or Wiring Pie

	
} 













