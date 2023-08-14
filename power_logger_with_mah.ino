#include <Adafruit_INA219.h>
#include <SSD1306AsciiAvrI2c.h>
#include <SdFat.h>
#include <Encoder.h>


//declare SSD1306 OLED display variables
#define OLED_RESET 4
SSD1306AsciiAvrI2c display;

//declare INA219 variables
Adafruit_INA219 ina219;
float current_mA = 0.0, oldcurr = 0.0;
float loadvoltage = 0.0, oldvolt = 0.0;
float power_mW = 0.0, oldpow = 0.0;
float energy_mWh = 0.0, oldegy = 0.0;
float capacity_mAh = 0.0, oldcap = 0.0;
unsigned long elapsed = 0;
bool normal_mode = false;
String battery_mode="";
bool store_to_sd = true;
float battery_min=2.71;
float battery_max=4.19;
bool stop=false;


//declare microSD variables
#define CHIPSELECT 10
#define ENABLE_DEDICATED_SPI 1
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SPI_CLOCK)
#define SPI_DRIVER_SELECT 0
SdFat32 sd;
File32 measurFile;

//some rotary encoder
#define CLK 2
#define DT 9
#define SW 6
unsigned long lastButtonPress = 0;
int previousValue = 0;
unsigned long debounceDelay = 150;  // Adjust this value as needed (in milliseconds)
unsigned long lastDebounceTime = 0;

// Time interval in milliseconds
const unsigned long interval1Hz = 1000;  // 1 second
const unsigned long interval10Hz = 100;  // 1/4 second (4 Hz)

// Variables to store the last time an event occurred
unsigned long lastEventTime1Hz = 0;
unsigned long lastEventTime10Hz = 0;
Encoder myEncoder(CLK, DT);


void setup() {
  bool start=false;
 // Serial.begin(9600);
  //setup pins
  pinMode(CLK, INPUT);
  pinMode(DT, INPUT);
  pinMode(SW, INPUT_PULLUP);

  //setup the INA219
  ina219.begin();

  //setup the display
  display.begin(&Adafruit128x64, 0x3C, OLED_RESET);
  display.setFont(System5x7);
  display.setCursor(10, 1);
  display.println("Welcome Sir!!");
  delay(1000);
  display.clear();

  // disable adc
  ADCSRA = 0;
  ACSR = 0x80;

  //select the mode now
  int size = 3;
  char* mod_options[size] = { "Normal", "Discharge battery", "Charge battery"};
  //select a mode
  int choice = handleEncoderInput("Choose a Mode:", mod_options, size);
  if(choice==0) {
    normal_mode = true;
  } else {
    battery_mode = mod_options[choice];
  }
 // Serial.print("mode:");
  // Serial.println(battery_mode);
  delay(300);
  size = 2;
  char* rec_options[size] = { "Record data", "Do not record" };
  if (handleEncoderInput("Choose a Mode:", rec_options, size)) {
    store_to_sd = false;
  } else {
    store_to_sd = true;
  }

 // Serial.print("record:");
 // Serial.println(store_to_sd);

  if (store_to_sd) {
    //setup the SDcard reader
    sd.begin(CHIPSELECT);
    measurFile.open("MEAS.csv", O_WRITE | O_CREAT | O_TRUNC);
    measurFile.print("Time,Voltage,Current\n");
    measurFile.sync();
  }
  delay(300);
  //set the callibration
  size = 3;
  char* callib_options[size] = { "32V 2A", "32V 1A", "16V 400mA" };
  choice = handleEncoderInput("Callibration:", callib_options, size);
  if(choice==0) {
    ina219.setCalibration_32V_2A();
  }else if (choice==1) {
    ina219.setCalibration_32V_1A();
  }else if (choice==2) {
    ina219.setCalibration_16V_400mA();
  }

  //wait until user starts the activity
  display.println("Press to Start:");
  delay(300);
  while(!start){
    int btnState = digitalRead(SW);
    if (btnState == LOW) {
      if (millis() - lastButtonPress > 100) {
       // Serial.println("Button pressed!");
        start=true;
        display.clear();
      }
      lastButtonPress = millis();
    }
  }
}

void loop() {
  // Check if the 1 Hz interval has passed
  if (millis() - lastEventTime1Hz >= interval1Hz && !stop) {
    // Do something for 1 Hz
    //calculate elapsed time
    elapsed = millis() - lastEventTime1Hz;
    //calculate power
    energy_mWh += power_mW * (elapsed / 3600000.0);
    //calculate mAh
    capacity_mAh += current_mA * (elapsed / 3600000.0);
    if (store_to_sd) {
      writeFile();
    }

    // Update the last event time for 1 Hz
    lastEventTime1Hz = millis();
  }

  // Check if the 10 Hz interval has passed
  if (millis() - lastEventTime10Hz >= interval10Hz && !stop) {
    // Do something for 10 Hz
    ina219values();
    //update the voltage line on the SSD1306 display
    if (loadvoltage != oldvolt) {
      displayline(loadvoltage, 0, " V");
      oldvolt = loadvoltage;
    }

    //update the current line on the SSD1306 display
    if (current_mA != oldcurr) {
      displayline(current_mA, 2, " mA");
      oldcurr = current_mA;
    }

    //update the power line on the SSD1306 display
    if (power_mW != oldpow) {
      displayline(power_mW, 4, " mW");
      oldpow = power_mW;
    }

    //update the energy line on the SSD1306 display
    if (energy_mWh != oldegy) {
      displayline(energy_mWh, 6, " mWh");
      oldegy = energy_mWh;
    }
    //update the capcity line on the SSD1306 display
    if (capacity_mAh != oldcap) {
      displayline(capacity_mAh, 7, " mAh");
      oldcap = capacity_mAh;
    }

    // Update the last event time for 10 Hz
    lastEventTime10Hz = millis();
  }
  //check if we are in battery mode in order to stop the process
  if(battery_mode=="Discharge battery" && loadvoltage<battery_min){
    stop=true;
  }
  if(battery_mode=="Charge battery" && loadvoltage>battery_max){
    stop=true;
  }
}
void ina219values() {
  float shuntvoltage = 0.0;
  float busvoltage = 0.0;

  //turn the INA219 on
  ina219.powerSave(false);

  //get the shunt voltage, bus voltage, current and power consumed from the INA219
  shuntvoltage = ina219.getShuntVoltage_mV();
  busvoltage = ina219.getBusVoltage_V();
  current_mA = ina219.getCurrent_mA();

  //turn the INA219 off
  ina219.powerSave(true);

  //compute the load voltage
  loadvoltage = busvoltage + (shuntvoltage / 1000.0);

  //compute the power consumed
  power_mW = loadvoltage * current_mA;
}
void writeFile() {
  char buf[32], voltbuf[16] = { 0 }, curbuf[16] = { 0 };

  //prepare buffers with the voltage and current values in strings
  dtostrf(loadvoltage, 10, 3, voltbuf);
  dtostrf(current_mA, 10, 3, curbuf);

  //format a csv line : time,voltage,current\n
  sprintf(buf, "%ld,%s,%s\n", millis(), voltbuf, curbuf);

  //write the line in the file
  measurFile.write(buf);
  measurFile.sync();
}
void displayline(const float measurment, const uint8_t line_num, const char line_end[]) {
  char floatbuf[16] = { 0 };

  //format the line ([-]xxxxx.xxx [unit])
  dtostrf(measurment, 10, 3, floatbuf);
  strcat(floatbuf, line_end);

  //place the cursor and write the line
  display.setCursor(0, line_num);
  display.print(floatbuf);
}

// Function to handle rotary encoder input and update the selected index
int handleEncoderInput(const char* title, const char* options[], int numOptions) {
  bool select_needed = true;
  //clear the display
  display.clear();
  int position = 0;
  int selectedItem = 0;
  int cursor_position = 0;
  unsigned long lastButtonPress = 0;
  String highlighted = "=> ";

  while (select_needed) {
    display.setCursor(20, 0);
    display.println(title);
    for (int i = 0; i < numOptions; i++) {
      cursor_position = i + 1;  //plus one because content start at row 1 after the title
      if (position == i) {
        display.setCursor(0, cursor_position);
        display.println(highlighted + options[i]);
      } else {
        display.setCursor(20, cursor_position);
        display.println(options[i]);
      }
    }
    int currentValue = myEncoder.read();
    if (currentValue != previousValue) {
      // Check if enough time has passed since the last step
      if (millis() - lastDebounceTime >= debounceDelay) {
        //Serial.println(position);
        if (currentValue < previousValue) {
          if (position < numOptions - 1) {
            position++;
          }
        } else {
          if (position > 0) {
            position--;
          }
        }
        display.clear();

        //Serial.print("Selected option: ");
        //Serial.println(options[position]);
        // Update the last debounce time
        lastDebounceTime = millis();
      }
    }
    int btnState = digitalRead(SW);
    if (btnState == LOW) {
      if (millis() - lastButtonPress > 50) {
       // Serial.println("Button pressed!");
        select_needed = false;
        display.clear();
      }
      lastButtonPress = millis();
    }

    previousValue = currentValue;
  }
  return position;
}
