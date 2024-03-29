#include <Arduino.h>
#define USE_HARDWARE_SERIAL true
#define USE_SONAR_PELLET_SENSOR true

#if !USE_HARDWARE_SERIAL
  #include <SoftwareSerial.h>
#endif

#if USE_SONAR_PELLET_SENSOR
  #include <NewPing.h>
#endif

struct deviceCommand {
  bool readSecondByte;
  byte firstByteAddress[2];
  byte secondByteAddress[2];
};

//Heater Living (Foghet Evo ARIA)
deviceCommand device1Commands[] = {
  {false, {0x20, 0x00}, {0x00, 0x00}}, //Heater state 0 = Off, 1 to 7 start
  {false, {0x00, 0xE7}, {0x00, 0x00}}, //Power + 1 (Values: 2 - 6)
  {true, {0x00, 0x5F}, {0x00, 0x60}}, //Temperature Smokegas, second byte for temperatures higher than 255. (Multiply second value by 256)
  {false, {0x20, 0x71}, {0x00, 0x00}}, //circulating ventilator
  {false, {0x20, 0x64}, {0x00, 0x00}}, //Standby on/off
  {false, {0x20, 0xEB}, {0x00, 0x00}}, //Mode 0 = Wood, 1 = Pellet
};

//Heater cellar (Odette)
deviceCommand device2Commands [] = {
  {false, {0x00, 0x21}, {0x00, 0x00}}, //Heater state 00: Off 01-06: Ignition/Start up 07: Running 09: Cleaning
  {false, {0x20, 0xD0}, {0x00, 0x00}}, //Power 1 to 1
  {false, {0x00, 0x5C}, {0x00, 0x00}}, //Smoke temperature in degrees
  {false, {0x20, 0xE4}, {0x00, 0x00}}, //Standby activated 0501: Activ 0400: Inactive
};

int commandPosition;
byte buffer2[2];
byte buffer4[4];

const int countCommandsDevice1 = 6;
byte responsesDevice1 [countCommandsDevice1][4];

const int countCommandsDevice2 = 4;
byte responsesDevice2 [countCommandsDevice2][4];

const int countCommands = countCommandsDevice1 > countCommandsDevice2 ? countCommandsDevice1 : countCommandsDevice2;

bool forceSendValuesAtNextRun = true;
bool forceSendValues = false;
bool firstRunComplete = false;
const bool debugMode = false;
const byte akByte = 0xFD;
const byte nakByteArduino = 0xFE;
const byte nakByteHeater = 0xFF;

const byte stateByte = 0x00; //Type state
const byte powerByte = 0x01; //Type power
const byte smokeTemperatureByte = 0x02; //Type smoke temperature
const byte ambientTemperatureByte = 0x03; //Type ambient temperature
const byte ventilationByte = 0x04; //Type ventilation
const byte standbyByte = 0x05; //Type standby
const byte modeByte = 0x06; //Type mode wood or pellet
const byte pelletLevelByte = 0x07; //Type pellet level

#if !USE_HARDWARE_SERIAL
  const int DEVICE_1_RX_PIN = 8;
  const int DEVICE_1_TX_PIN = 9;
  
  const int DEVICE_2_RX_PIN = 10;
  const int DEVICE_2_TX_PIN = 11;
#endif

#define serialUsb Serial

#if USE_HARDWARE_SERIAL
  #define serialDevice1 Serial1
  #define serialDevice2 Serial2
#else
  SoftwareSerial serialDevice1 = SoftwareSerial(DEVICE_1_RX_PIN, DEVICE_1_TX_PIN); // RX, TX
  SoftwareSerial serialDevice2 = SoftwareSerial(DEVICE_2_RX_PIN, DEVICE_2_TX_PIN); // RX, TX
#endif

#if USE_SONAR_PELLET_SENSOR
  unsigned int device1PelletLevel;
  unsigned int device2PelletLevel;
  #define TRIGGER_PIN_1 5
  #define ECHO_PIN_1 4
  #define TRIGGER_PIN_2 3
  #define ECHO_PIN_2 2
  #define MAX_DISTANCE 200
 
  NewPing sonarDevice1(TRIGGER_PIN_1, ECHO_PIN_1, MAX_DISTANCE);
  NewPing sonarDevice2(TRIGGER_PIN_2, ECHO_PIN_2, MAX_DISTANCE);
#endif

void clearBuffers()
{
  buffer2[0] = 0;
  buffer2[1] = 0;

  buffer4[0] = 0;
  buffer4[1] = 0;
  buffer4[2] = 0;
  buffer4[3] = 0;
}

void printHex(int num, int precision) {
 char tmp[16];
 char format[128];

 sprintf(format, "%%.%dX", precision);

 sprintf(tmp, format, num);
 serialUsb.print(tmp);
}

byte calculateModulo(byte b[], int count)
{
  byte checksum = 0;
  for (int i=0; i < count; i++) {
    checksum += b[i] % 256; 
  }
  return checksum;
}

void serialWaitForTransmission()
{
  if (serialUsb.available() > 0 && serialUsb.available() < 4)
  {
    //Wait some time for message transmission
    delay(10);
  }
}

boolean compareArrays(byte *a, byte *b){
  int n;
  int len_a = sizeof(a);
  int len_b = sizeof(b);
  
  // if their lengths are different, return false
  if (len_a != len_b) return false;

  // test each element to be the same. if not, return false
  for (n=0;n<len_a;n++) if (a[n]!=b[n]) return false;

  //ok, if we have not returned yet, they are equal :)
  return true;
}

void sendAkNakMessage(byte deviceId, byte commandType, bool acknowledge, byte nakByte)
{
  if (debugMode)
  {
    if (acknowledge)
    {
      serialUsb.println("Aknowlage command " + String(commandType));
    }
    else
    {
      serialUsb.println("Not aknowlage command " + String(commandType));
    }
  }
  else
  {
    byte akNakMessage[5] = { deviceId, 0x00, commandType, 0x00, 0x00 };
  
    if (acknowledge)
    {
      akNakMessage[1] = akByte;
    }
    else
    {
      akNakMessage[1] = nakByte;
    }
  
    akNakMessage[4] = calculateModulo(akNakMessage, 4);
  
    serialUsb.write(akNakMessage, 5);
  }
}

bool sendCommand(int deviceId, byte commandBytes[], byte commandType, byte expectedResponse[])
{
  clearBuffers();
  
  int countRead = 0;
  
  if (deviceId == 1)
  {
    //On this device we need to flush the serial port after every byte.
    serialDevice1.write(commandBytes[0]);
    serialDevice1.flush();
    serialDevice1.write(commandBytes[1]);
    serialDevice1.flush();
    serialDevice1.write(commandBytes[2]);
    serialDevice1.flush();
    serialDevice1.write(commandBytes[3]);

    countRead = serialDevice1.readBytes(buffer2, 2);
  }
  else if (deviceId == 2)
  {
    serialDevice2.write(commandBytes, 4);
    countRead = serialDevice2.readBytes(buffer2, 2);
  }

  if (countRead == 2)
  {
    if (debugMode)
    {
      serialUsb.print("Command response: ");
      printHex(buffer2[0], 2);
      printHex(buffer2[1], 2);
      serialUsb.println();
    } 
    else
    {
      sendAkNakMessage(deviceId, commandType, compareArrays(buffer2, expectedResponse), nakByteHeater);
    }

    return true;
  }

  return false;
}

void queryDevice1()
{
  if (commandPosition > countCommandsDevice1 - 1)
    return;

  clearBuffers();

  deviceCommand currentDeviceCommand = device1Commands[commandPosition];
  
  //On this device we need to flush the serial port after every byte.
  serialDevice1.write(currentDeviceCommand.firstByteAddress[0]);
  serialDevice1.flush();
  serialDevice1.write(currentDeviceCommand.firstByteAddress[1]);
  
  int countRead = serialDevice1.readBytes(&buffer4[0], 2);

  if (currentDeviceCommand.readSecondByte)
  {
    serialDevice1.write(currentDeviceCommand.secondByteAddress[0]);
    serialDevice1.flush();
    serialDevice1.write(currentDeviceCommand.secondByteAddress[1]);
    
    countRead += serialDevice1.readBytes(&buffer4[2], 2);
  }

  if ((!currentDeviceCommand.readSecondByte && countRead == 2)
    || (currentDeviceCommand.readSecondByte && countRead == 4))
  {
    if (forceSendValues || firstRunComplete && !compareArrays(buffer4, responsesDevice1[commandPosition]))
    {
      byte currentValue1 = buffer4[1];
      byte currentValue2 = buffer4[3];
      byte updateMessage[5];
      
      //Device 1
      updateMessage[0] = 0x01;
      
      //Parse response
      if (commandPosition == 0)
      {
        //State
        updateMessage[1] = stateByte;
        updateMessage[2] = currentValue1;
        updateMessage[3] = currentValue2;

        if (debugMode)
          serialUsb.print("Device 1 state: ");
      }
      else if (commandPosition == 1)
      {
        currentValue1 -= 1;  //Power is always + 1
        //Power
        updateMessage[1] = powerByte;
        updateMessage[2] = currentValue1;
        updateMessage[3] = currentValue2;

        if (debugMode)
          serialUsb.print("Device 1 power: ");
      }
      else if (commandPosition == 2)
      {
        //Smoke temperature
        updateMessage[1] = smokeTemperatureByte; //Type smoke temperature
        updateMessage[2] = currentValue1;
        updateMessage[3] = currentValue2;

        if (debugMode)
          serialUsb.print("Device 1 smoke temperature: ");
      }
      /*
      else if (commandPosition == 3)
      {
        //Smoke temperature
        updateMessage[1] = ambientTemperatureByte; //Type ambient temperature
        updateMessage[2] = currentValue1;
        updateMessage[3] = currentValue2;

        if (debugMode)
          serialUsb.print("Device 1 ambient temperature: ");
      }
      */
      else if (commandPosition == 3)
      {
        //Ventilation
        updateMessage[1] = ventilationByte; //Type ventilation
        updateMessage[2] = currentValue1;
        updateMessage[3] = currentValue2;

        if (debugMode)
          serialUsb.print("Device 1 ventilation: ");
      }
      else if (commandPosition == 4)
      {
        //Standby
        updateMessage[1] = standbyByte; //Type standby
        updateMessage[2] = currentValue1;
        updateMessage[3] = currentValue2;

        if (debugMode)
          serialUsb.print("Device 1 standby: ");
      }
      else if (commandPosition == 5)
      {
        //Mode wood or pellet
        updateMessage[1] = modeByte; //Type mode wood or pellet
        updateMessage[2] = currentValue1;
        updateMessage[3] = currentValue2;

        if (debugMode)
          serialUsb.print("Device 1 mode wood or pellet: ");
      }
      
      updateMessage[4] = calculateModulo(updateMessage, 4);

      if (debugMode)
      {
        serialUsb.print(String(currentValue1) + " " + String(currentValue2));
        serialUsb.println();
      }
      else
      {
        serialUsb.write(updateMessage, 5);
      }
    }

    responsesDevice1[commandPosition][0] = buffer4[0];
    responsesDevice1[commandPosition][1] = buffer4[1];
    responsesDevice1[commandPosition][2] = buffer4[2];
    responsesDevice1[commandPosition][3] = buffer4[3];
  }
}

void queryDevice2()
{
  if (commandPosition > countCommandsDevice2 - 1)
    return;

  clearBuffers();

  deviceCommand currentDeviceCommand = device2Commands[commandPosition];

  serialDevice2.write(currentDeviceCommand.firstByteAddress, 2);
    
  int countRead = serialDevice2.readBytes(&buffer4[0], 2);

  if (currentDeviceCommand.readSecondByte)
  {
    serialDevice2.write(currentDeviceCommand.secondByteAddress, 2);
    
    countRead += serialDevice1.readBytes(&buffer4[2], 2);
  }

  if ((!currentDeviceCommand.readSecondByte && countRead == 2)
    || (currentDeviceCommand.readSecondByte && countRead == 4))
  {
    if (forceSendValues || firstRunComplete && !compareArrays(buffer4, responsesDevice2[commandPosition]))
    {
      byte currentValue1 = buffer4[1];
      byte currentValue2 = buffer4[3];
      byte updateMessage[5];
      
      //Device 2
      updateMessage[0] = 0x02;
      
      //Parse response
      if (commandPosition == 0)
      {
        //State
        updateMessage[1] = stateByte; //Type state
        updateMessage[2] = currentValue1;
        updateMessage[3] = currentValue2;

        if (debugMode)
          serialUsb.print("Device 2 state: ");
      }
      else if (commandPosition == 1)
      {
        //Power
        updateMessage[1] = powerByte; //Type power
        updateMessage[2] = currentValue1;
        updateMessage[3] = currentValue2;

        if (debugMode)
          serialUsb.print("Device 2 power: ");
      }
      else if (commandPosition == 2)
      {
        //Smoke temperature
        updateMessage[1] = smokeTemperatureByte; //Type smoke temperature
        updateMessage[2] = currentValue1;
        updateMessage[3] = currentValue2;

        if (debugMode)
          serialUsb.print("Device 2 smoke temperature: ");
      }
      else if (commandPosition == 3)
      {
        //Standby
        updateMessage[1] = standbyByte; //Type standby

        if (buffer4[0] == 0x04 && buffer4[1] == 0x00)
        {
          //off
          updateMessage[2] = 0;
        }
        else if (buffer4[0] == 0x05 && buffer4[1] == 0x01)
        {
          //on
          updateMessage[2] = 1;
        }

        updateMessage[3] = currentValue2;

        if (debugMode)
          serialUsb.print("Device 2 standby: ");
      }
      
      updateMessage[4] = calculateModulo(updateMessage, 4);

      if (debugMode)
      {
        serialUsb.print(String(currentValue1) + " " + String(currentValue2));
        serialUsb.println();
      }
      else
      {
        serialUsb.write(updateMessage, 5);
      }
    }

    responsesDevice2[commandPosition][0] = buffer4[0];
    responsesDevice2[commandPosition][1] = buffer4[1];
    responsesDevice2[commandPosition][2] = buffer4[2];
    responsesDevice2[commandPosition][3] = buffer4[3];
  }
}

void checkSonarPelletLevel(int deviceId)
{
#if USE_SONAR_PELLET_SENSOR
  unsigned int distance = 0;
  
  if (deviceId == 1)
  {
    unsigned int currentDistanceDevice1 = sonarDevice1.ping_cm();

    if (device1PelletLevel != currentDistanceDevice1 || forceSendValues)
    {
      device1PelletLevel = currentDistanceDevice1;
      distance = currentDistanceDevice1;
    }
  }
  else if (deviceId == 2)
  {
    unsigned int currentDistanceDevice2 = sonarDevice2.ping_cm();
    if (device2PelletLevel != currentDistanceDevice2 || forceSendValues)
    {
      device2PelletLevel = currentDistanceDevice2;
      distance = currentDistanceDevice2;
    }
  }

  if (distance != 0)
  {
    if (debugMode)
    {
      serialUsb.print("Device " + String(deviceId) + " pellet level: ");
      serialUsb.println(String(distance));
    }
    else
    {
      byte updateMessage[5];

      updateMessage[0] = deviceId;
      updateMessage[1] = pelletLevelByte; //Type pellet level
      updateMessage[2] = distance;
      updateMessage[3] = 0x00; // Not used at the moment
      updateMessage[4] = calculateModulo(updateMessage, 4);
      
      serialUsb.write(updateMessage, 5);
    }
  }
#endif
}

void checkAndSendCommands()
{
  serialWaitForTransmission();
  
  //Check if we need to send some commands
  while (serialUsb.available() >= 4)
  {
    clearBuffers();
    
    int countRead = serialUsb.readBytes((char*)buffer4, 4);
    byte expectedResponse[2];
    
    if (countRead == 4)
    {
      int intDeviceId = buffer4[0];
      int intCommandType = buffer4[1];
      int intValue = buffer4[2];
      int intSentChecksum = buffer4[3];

      int calculatedChecksum = calculateModulo(buffer4, 3);

      if (debugMode)
      {
        serialUsb.print("Incoming command: ");
        printHex(intDeviceId, 2);
        printHex(intCommandType, 2);
        printHex(intValue, 2);
        printHex(intSentChecksum, 2);
        serialUsb.println();
      }

      if (intSentChecksum != calculatedChecksum)
      {
        if (debugMode)
        {
          serialUsb.println("Command not acknowledged");
        }
        else
        {
          sendAkNakMessage(intDeviceId, intCommandType, false, nakByteArduino);
        }
        
        return;
      }

      byte command[4];
      if (intDeviceId == 0)
      {
        //General commands
        if (intCommandType == 1 && intValue == 1)
        {
          //Send all values at next run, even if they didn't change
          forceSendValuesAtNextRun = true;
          sendAkNakMessage(intDeviceId, intCommandType, true, nakByteArduino);
        }
      }
      else if (intDeviceId == 1)
      {
        //Command for device 1
        if (intCommandType == 0)
        {
          command[0] = 0x80;
          command[1] = 0xE8;
          //On / Off
          if (intValue == 0)
          {
            //Power off
            command[2]= 0xAA;
            expectedResponse[0] = 0x12;
            expectedResponse[1] = 0xAA;
          }
          else if (intValue == 1)
          {
            //Power on
            command[2]= 0x55;
            expectedResponse[0] = 0xBD;
            expectedResponse[1] = 0x55;
          }
          
          command[3] = calculateModulo(command, 3);

          if (sendCommand(1, command, intCommandType, expectedResponse))
          {
            //Update local value so update is not triggered again
            responsesDevice1[0][1] = command[2];
          }
        }
        else if (intCommandType == 1)
        {
          if (intValue > 0 && intValue <= 5)
          {
            //Set power 1 to 5
            command[0] = 0xA0;
            command[1] = 0x01;
            command[2]= intValue;
            command[3] = calculateModulo(command, 3);

            expectedResponse[0] = 0xA1 + intValue;
            expectedResponse[1] = intValue;
            
            if (sendCommand(1, command, intCommandType, expectedResponse))
            {
              //Update local value so update is not triggered again
              responsesDevice1[1][1] = command[2];
            }
          }
        }
      }
      else if (intDeviceId == 2)
      {
        //Command for device 2
        if (intCommandType == 0)
        {
          command[0] = 0x80;
          command[1] = 0xE8;
          //On / Off
          if (intValue == 0)
          {
            //Power off
            command[2]= 0xAA;
            expectedResponse[0] = 0x12;
            expectedResponse[1] = 0xAA;
          }
          else if (intValue == 1)
          {
            //Power on
            command[2]= 0x55;
            expectedResponse[0] = 0xBD;
            expectedResponse[1] = 0x55;
          }
          
          command[3] = calculateModulo(command, 3);
          
          if (sendCommand(2, command, intCommandType, expectedResponse))
          {
            //Update local value so update is not triggered again
            responsesDevice2[0][1] = command[2];
          }
        }
        else if (intCommandType == 1)
        {
          if (intValue >= 0 && intValue <= 5)
          {
            //Set power 0 to 5
            command[0] = 0xA0;
            command[1] = 0xD0;
            command[2]= intValue;
            command[3] = calculateModulo(command, 3);

            if (debugMode)
            {
              serialUsb.print("Sending power command to device 2: ");
              printHex(command[0], 2);
              printHex(command[1], 2);
              printHex(command[2], 2);
              printHex(command[3], 2);
              serialUsb.println();
            }

            expectedResponse[0] = 0x70 + intValue;
            expectedResponse[1] = intValue;
            
            if (sendCommand(2, command, intCommandType, expectedResponse))
            {
              //Update local value so update is not triggered again
              responsesDevice2[1][1] = command[2];
            }
          }
        }
      }
    }
	
	  serialWaitForTransmission();
  }
}

void serialFlush(){
  if (serialUsb.available())
  {
    while(serialUsb.available() > 0) {
      serialUsb.read();
    }
    
    sendAkNakMessage(0x00, nakByteArduino, false, nakByteArduino);
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  
  serialUsb.begin(19200);
  if (debugMode)
    serialUsb.println("START");

#if USE_HARDWARE_SERIAL
  serialDevice1.begin(1200, SERIAL_8N1_SINGLEWIRE);
  serialDevice2.begin(1200, SERIAL_8N1_SINGLEWIRE);
#else
  serialDevice1.begin(1200);
  serialDevice2.begin(1200);
#endif

  delay(10);
}

void loop() {
  queryDevice1();
  delay(10);
  queryDevice2();
  delay(10);
  checkAndSendCommands();
  serialFlush();
  
  commandPosition++;
  
  if (commandPosition == countCommands)
  {
    commandPosition = 0;
    forceSendValues = false;
    firstRunComplete = true;

    #if USE_SONAR_PELLET_SENSOR
      checkSonarPelletLevel(1);
      checkSonarPelletLevel(2);
    #endif
    
    if (forceSendValuesAtNextRun)
    {
      forceSendValues = true;
      forceSendValuesAtNextRun = false;
    }
    
    if (debugMode)
      serialUsb.println("NEW RUN");

    digitalWrite(LED_BUILTIN, HIGH);
    delay(1000);
    digitalWrite(LED_BUILTIN, LOW);
  }
  else
  {
    delay(10);
  }
}