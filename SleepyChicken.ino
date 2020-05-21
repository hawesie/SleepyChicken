//notes on board
//black 5v
//white gnd
//grey 9
//purple 10
//blue 11

// TO make work
// 1: Start with door in open position
// 2: Connect motor pins to motor controller chip pins 3 & 6 (number from 1 in top left)
// 3: Connect swtich and control pins to board
// 4: Motor controller 4 to ground, motor controller 8 to 5V
// Motor controller pins, all pcm, but is that necessary?

// **** INCLUDES *****
#include "SleepyPi2.h"
#include <Time.h>
#include <LowPower.h>
#include <PCF8523.h>
#include <Wire.h>

const int LED_PIN = 13;

// L293D pin 1, connector blue
const int enablePin = 11;

// L293D pin 2, conector purple
const int in1Pin = 10;

// L293D pin 7, connector grey
const int in2Pin = 9;

const char *monthName[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
}; 


// Constants indicating the current state of the door
const int DOOR_OPEN = 0;
const int DOOR_CLOSING = 1;
const int DOOR_CLOSED = 2;
const int DOOR_OPENING = 3;

volatile bool  buttonPressed = false;
int door_state = DOOR_CLOSED;
int target_state = door_state;
int alarm_state = door_state;

tmElements_t tm;

const int MOTOR_RPM = 0.5;

// 0.5 RPM motor means 60 secs for a half turn.
//const unsigned long CYCLE_DURATION_MS = 60000;

//tuned
const unsigned long CYCLE_DURATION_MS = 26000;

//Seems to take 104s to do a half turn, but not on battery
//const unsigned long CYCLE_DURATION_MS = 105000;

// smaller value for testing
//const unsigned long CYCLE_DURATION_MS = 5000;

// Speed is always max speed
const int SPEED = 255;

// All times GMT, no BST changes
const int wakeTimes[][4] = {
{5, 30,  17, 30} ,
{5, 30,  17, 30} ,
{5, 30,  17, 30} ,
// Start times where BST is active, 
{3, 38,  18,  31},
{2, 53,  21, 55},
{2, 49, 21, 00},
{2, 50,  20, 30} ,
{3, 27,  19, 55} ,
// Sept
{5, 30,  19, 30} ,
// Oct
{5, 30,  19, 30} ,
// Nov
// End times where BST is active 
{5, 30,  17, 30} ,
// Dec
{5, 30,  17, 30} ,

};

// Time delta - smallest resolution of alarm is 1 minute
const TimeSpan timeDelta(60);

void alarm_isr()
{
    buttonPressed = false;
}

void button_isr()
{
  buttonPressed = true;
}




void setup()
{
  // initialize serial communication: In Arduino IDE use "Serial Monitor"
  Serial.begin(9600);
  Serial.println("Starting..."); delay(50);
  
  SleepyPi.rtcInit(true);

  // Default the clock to the time this was compiled.
  // Comment out if the clock is set by other means
  // ...get the date and time the compiler was run
  if(false) {
    if (getDate(__DATE__) && getTime(__TIME__)) {
        // and configure the RTC with this info
        SleepyPi.setTime(DateTime(F(__DATE__), F(__TIME__)));
    }  
  }
  
  // Configure "Standard" LED pin
  pinMode(LED_PIN, OUTPUT);		
  // Switch off LED
  digitalWrite(LED_PIN,LOW);		

  //Pins for motor control
  pinMode(in1Pin, OUTPUT);
  pinMode(in2Pin, OUTPUT);
  pinMode(enablePin, OUTPUT);


  
}


void goToSleep(const DateTime &nextWake) {
    
    SleepyPi.rtcClearInterrupts();
              
    buttonPressed = false;

    // Allow wake up alarm to trigger interrupt on falling edge.
    attachInterrupt(0, alarm_isr, FALLING);   // Alarm pin
    attachInterrupt(1, button_isr, HIGH);    // button pin
    
    SleepyPi.enableWakeupAlarm(true);
        
    // Setup the Alarm Time
    // Hours & Minutes i.e. 22:07 on the 24 hour clock
    SleepyPi.setAlarm(nextWake.hour(),nextWake.minute());            
              
    // Switch off LED
    digitalWrite(LED_PIN,LOW);
  
  
    // Enter power down state with ADC and BOD module disabled.
    // Wake up when wake up pin is low (which occurs when our alarm clock goes off)
    SleepyPi.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF); 
    
    // Once we get here we've woken back up
        
    // Disable external pin interrupt on wake up pin.
    detachInterrupt(0);        
    detachInterrupt(1);        
    
    // Switch on LED
    digitalWrite(LED_PIN, HIGH);

    if(!buttonPressed) {      
      SleepyPi.ackAlarm();       
      Serial.println("I've Just woken up on the alarm!");delay(50);
    }
    else {
      Serial.println("I've Just woken up on the button!");delay(50);
    }
}

DateTime getNextWakeTime() {
  // Read the time
  DateTime now = SleepyPi.readTime();
  int monthIndex = now.month() - 1;
  int openHour = wakeTimes[monthIndex][0];
  int openMinute = wakeTimes[monthIndex][1];
  int closeHour = wakeTimes[monthIndex][2];
  int closeMinute = wakeTimes[monthIndex][3];

  // horrible use of side effects

  DateTime nextWake;
  // if it's past closing time
  if(now.hour() >= closeHour && now.minute() >= closeMinute) {
    Serial.println("after close time");delay(50);
    alarm_state = DOOR_OPEN;
    nextWake = DateTime(now.year(), now.month(), now.day() + 1, openHour, openMinute);
  }
  // if it's past opening time
  else if(now.hour() >= openHour && now.minute() >= openMinute) {
    Serial.println("after open time");delay(50);
    alarm_state = DOOR_CLOSED;
    nextWake =  DateTime(now.year(), now.month(), now.day(), closeHour, closeMinute);

  }
  // else it's before opening time 
  else {
    Serial.println("before open time");delay(50);
    alarm_state = DOOR_OPEN;  
    nextWake =  DateTime(now.year(), now.month(), now.day(), openHour, openMinute);
  }

  Serial.println("now");
  printTime(now);
  Serial.println("next wake");
  printTime(nextWake);
  Serial.println("");
  delay(100);
  return nextWake;

}

boolean active = false;
unsigned long action_complete_at = 0;


void loop() 
{
  //just used to detect state change for debugging
  int entry_state = door_state;

  //If we don't have an act time
  if(0 == action_complete_at) {
    // Go to sleep until we're woken up by the clock or button
    goToSleep(getNextWakeTime());
    //not sure if the alarm_isr function gets triggered in series
    delay(200);
    Serial.println("Woken up with door state: " +state_to_string(door_state));delay(50);  
      
    // If button was pressed then we need to  switch to a new state regardless
    if(buttonPressed == true) { 
        target_state = (door_state + 2) % 4;
    }
    // If we were woken by the timer then we need to check whether we need to open or shut
    else {
      target_state = alarm_state;
    }   
    
    //pretend to do something
    Serial.println("Target state: "  + state_to_string(target_state));delay(50);  
  
    if(door_state != target_state) {
      // set future end door target time
      advance_door_state();      
      action_complete_at = millis() + CYCLE_DURATION_MS;    
    }
  }
  else {
    if(millis() > action_complete_at) {
      advance_door_state();
      action_complete_at = 0;
      stopMotor();
    }
    else {
//      Serial.println(action_complete - millis());
      
      if(door_state == DOOR_OPENING) {
        setMotor(SPEED, true);
      }
      else {
        setMotor(SPEED, false);
      }
    }
  }

  if(entry_state != door_state) {
    Serial.println(state_to_string(door_state));delay(50);
  }

  delay(200);

}

void printTime(const DateTime & now)
{
    
    // Print out the time
    Serial.print("Ok, Time = ");
    print2digits(now.hour());
    Serial.write(':');
    print2digits(now.minute());
    Serial.write(':');
    print2digits(now.second());
    Serial.print(", Date (D/M/Y) = ");
    Serial.print(now.day());
    Serial.write('/');
    Serial.print(now.month()); 
    Serial.write('/');
    Serial.print(now.year(), DEC);
    Serial.println();

    return;
}
bool getTime(const char *str)
{
  int Hour, Min, Sec;

  if (sscanf(str, "%d:%d:%d", &Hour, &Min, &Sec) != 3) return false;
  tm.Hour = Hour;
  tm.Minute = Min;
  tm.Second = Sec;
  return true;
}

bool getDate(const char *str)
{
  char Month[12];
  int Day, Year;
  uint8_t monthIndex;

  if (sscanf(str, "%s %d %d", Month, &Day, &Year) != 3) return false;
  for (monthIndex = 0; monthIndex < 12; monthIndex++) {
    if (strcmp(Month, monthName[monthIndex]) == 0) break;
  }
  if (monthIndex >= 12) return false;
  tm.Day = Day;
  tm.Month = monthIndex + 1;
  tm.Year = CalendarYrToTm(Year);
  return true;
}

void print2digits(int number) {
  if (number >= 0 && number < 10) {
    Serial.write('0');
  }
  Serial.print(number);
}

String state_to_string(int _s) {
  if(_s == DOOR_OPEN) {
    return "DOOR_OPEN";
  }
  else if(_s == DOOR_CLOSING) {
    return "DOOR_CLOSING";
  } 
  else if(_s == DOOR_CLOSED) {
    return "DOOR_CLOSED";
  }
  else if(_s == DOOR_OPENING) {
    return "DOOR_OPENING";
  }
  else {
    return "UNKNOWN";
  }
}

void advance_door_state() {
  door_state = (door_state + 1) % 4;
}


void setMotor(int speed, boolean reverse)
{
  analogWrite(enablePin, SPEED);
  digitalWrite(in1Pin, !reverse);
  digitalWrite(in2Pin, reverse);
}

void stopMotor() {
  analogWrite(enablePin, 0);
}

