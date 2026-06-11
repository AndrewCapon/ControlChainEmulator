#include <stdio.h>
#include <string.h>
#include <fcntl.h> 
#include <errno.h> 
#include <termios.h> 
#include <unistd.h> 
#include <sys/ioctl.h>
#include <stdint.h>
#include <time.h>

#include "control_chain.h"

// serial port
int nSerialPort;

// Timer shenanigans

struct timespec startTime, nowTime;
uint32_t uTimerUS = 0;
uint32_t uElapsedUS = 0;
bool bTimerActive = false;

extern "C"
{
  void (*timerCallback)(void);

  void timer_init(void (*callback)(void))
  {
    uTimerUS = 0;
    bTimerActive = false;
    timerCallback = callback;
  }

  void timer_set(uint32_t time_us)
  {
    uTimerUS = time_us;
    clock_gettime(CLOCK_MONOTONIC, &startTime);
  }

  void delay_us(uint32_t time_us)
  {
      struct timespec td = {
          .tv_sec = 0,
          .tv_nsec = time_us*1000 
      };
      nanosleep(&td, NULL);   
  }
}

// Actuators
#define buttonPortsCount 4
#define variablePortsCount 4

struct Actuator
{
    cc_actuator_t       *pActuator = nullptr;
    cc_assignment_t     *pAssignment = nullptr;
    float               fSetValue=0.0f;
};

float buttonValues[buttonPortsCount];
float variableValues[variablePortsCount];
Actuator actuators[buttonPortsCount+variablePortsCount];
int nActuatorCount = 0;

void DisplayAssignment(const char *pszLabel, cc_assignment_t *pAssignment)
{
  printf("%s\n", pszLabel);
  printf("  id          : %d\n", pAssignment->id);
  printf("  actuator_id : %d\n", pAssignment->actuator_id);
  printf("  value       : %f\n", pAssignment->value);
  printf("  min         : %f\n", pAssignment->min);
  printf("  max         : %f\n", pAssignment->max);
  printf("  def         : %f\n", pAssignment->def);
  printf("  mode        : %x\n", pAssignment->mode);
  printf("  steps       : %u\n", pAssignment->steps);
  printf("  list_count  : %u\n", pAssignment->steps);
#ifndef CC_STRING_NOT_SUPPORTED
  // TODO
    // uint8_t list_index;
    // option_t **list_items;
    // str16_t label, unit;
#endif
}

// Callbacks
void AssignmentCB(cc_assignment_t *pAssignment)
{
  DisplayAssignment("AssignmentCB", pAssignment);
}

void UnassignmentCB(int)
{

}

void UpdateCB(cc_assignment_t *pAssignment)
{
  DisplayAssignment("UpdateCB", pAssignment);
}

void SetValueCB(cc_set_value_t *pValue)
{
  printf("SetValueCB %d, %d, %f\n", pValue->assignment_id, pValue->actuator_id, pValue->value);
}


void responseCB(void *arg) 
{
  cc_data_t *response = (cc_data_t *) arg;
  int nBytesWritten = write(nSerialPort, response->data, response->size);
  fsync(nSerialPort);
  if(nBytesWritten != response->size)
    printf("Error Should have written %d bytes to serial, only wrote %d\n", response->size, nBytesWritten);
}

void eventsCB(void *arg) 
{
  cc_event_t *event = (cc_event_t *) arg;

  switch(event->id)
  {
    case CC_EV_HANDSHAKE_FAILED :
    {
      printf("Error handshake failed\n");
      break;
    }

    case CC_EV_ASSIGNMENT :
    {
      cc_assignment_t *pAssignment = (cc_assignment_t *) event->data;
      AssignmentCB(pAssignment);
      break;
    }

    case CC_EV_UNASSIGNMENT:
    {
      int *pActuatorId = (int *)event->data;
      UnassignmentCB(*pActuatorId);
      break;
    }

    case CC_EV_UPDATE :
    {
      cc_assignment_t *pAssignment = (cc_assignment_t *) event->data;
      UpdateCB(pAssignment);
      break;
    }

    case CC_EV_DEVICE_DISABLED :
    {
      printf("Error device disabled\n");
      break;
    }

    case CC_EV_MASTER_RESETED :
    {
      printf("Error device reset\n");
      break;
    }

    case CC_CMD_SET_VALUE :
    {
      cc_set_value_t *pValue = (cc_set_value_t *) event->data;
      SetValueCB(pValue);
      break;
    }

    default:
    {
      printf("Warning unhandled event %d\n", event->id);
      break;
    }
  }
}

bool InitialiseSerial(void)
{
  nSerialPort= open("/dev/tnt1", O_RDWR);

  // Check for errors
  if (nSerialPort < 0) {
    printf("Error %i from open: %s\n", errno, strerror(errno));
    return false;
  }

  // set non blocking
  struct termios tty;

  if(tcgetattr(nSerialPort, &tty) != 0) 
  {
    printf("Error %i from tcgetattr: %s\n", errno, strerror(errno));
    close(nSerialPort);
    return false;
  }

  tty.c_cc[VTIME] = 0; 
  tty.c_cc[VMIN] = 0;

  if (tcsetattr(nSerialPort, TCSANOW, &tty) != 0) 
  {
    printf("Error %i from tcsetattr: %s\n", errno, strerror(errno));
    close(nSerialPort);
    return false;
  }

  return true;
}

void CreateActuators(void)
{
  // create device
  const char *uri = "https://github.com/moddevices/cc-arduino-lib/tree/master/examples/TestDevice"; // TODO
  cc_device_t *device = cc_device_new("TestDevice", uri);

    // configure buttons 
  for (int i = 0; i < buttonPortsCount; i++) 
  {
      char sName[40];
      sprintf(sName, "Button %d", i);
      cc_actuator_config_t actuator_config;
      actuator_config.type = CC_ACTUATOR_MOMENTARY;
      actuator_config.name = sName;
      actuator_config.value = &buttonValues[i];
      actuator_config.min = 0.0;
      actuator_config.max = 1.0;
      actuator_config.supported_modes = CC_MODE_TOGGLE | CC_MODE_TRIGGER;
      actuator_config.max_assignments = 1;

      // create and add actuator to device
      cc_actuator_t *actuator = cc_actuator_new(&actuator_config);
      cc_device_actuator_add(device, actuator);
      actuators[nActuatorCount++].pActuator = actuator;
  }

  for (int i = 0; i < variablePortsCount; i++) 
  {
    char sName[40];
    sprintf(sName, "Variable %d", i);
    cc_actuator_config_t actuator_config;
    actuator_config.type = CC_ACTUATOR_CONTINUOUS;
    actuator_config.name = sName;
    actuator_config.value = &variableValues[i];
    actuator_config.min = 0.0;
    actuator_config.max = 1023.0;
    actuator_config.supported_modes = CC_MODE_REAL | CC_MODE_INTEGER;
    actuator_config.max_assignments = 1;

    // create and add actuator to device
    cc_actuator_t *actuator = cc_actuator_new(&actuator_config);
    cc_device_actuator_add(device, actuator);
    actuators[nActuatorCount++].pActuator = actuator;
  }
}

int main(int, char**)
{
  printf("ControlChainEmulator\n");

  // initialise th serial port
  if(!InitialiseSerial())
  {
    printf("Serial port failed to inititalise\n");
    return 1;
  }

  // initialise the controlchain
  cc_init(responseCB, eventsCB);

  // Set up test actuators
  CreateActuators();

  // read loop
  while(true)
  {
    // Handle timer
    if(uTimerUS)
    {
      clock_gettime(CLOCK_MONOTONIC, &nowTime);
      uint64_t elapsedNS = (nowTime.tv_sec - startTime.tv_sec) * 1000000000L + (nowTime.tv_nsec - startTime.tv_nsec);

      if(elapsedNS/1000 > uTimerUS)
      {
        // reset timer
        uTimerUS = 0;

        // callback
        timerCallback();
      }
    }

    // Handle serial input
    uint8_t buffer [2048];
    int n = read(nSerialPort, &buffer, sizeof(buffer));
    if(n>0)
    {
      //printf("Got %d bytes\n", n);
      cc_data_t data = {buffer, (uint32_t)n};
      if (cc_parse(&data) < 0)
      {
        printf("cc_parse() failed\n");
      }
    }

    // process actuators
    cc_process();
  }

  close(nSerialPort);
}
