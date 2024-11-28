#define BLYNK_TEMPLATE_ID "TMPL63aGqNxKU"
#define BLYNK_TEMPLATE_NAME "Air Quality"
#define BLYNK_AUTH_TOKEN "8o6SipbVqZJVbJrYZCickzBm5d78dnN0"

#include <Arduino.h>
#include <RTClib.h>
#include <PMS.h>
#include <dht.h>
#include <MQUnifiedsensor.h>
#include <SD.h>
#include <Adafruit_SSD1306.h>
#include <ADS1115_WE.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>

char ssid[] = "Thach";
char pass[] = "duyvi028";

RTC_DS3231 rtc;
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

#define PMS_RX_PIN GPIO_NUM_17  
#define PMS_TX_PIN GPIO_NUM_16
PMS pms(Serial1); // Use Serial1 for PMS communication
PMS::DATA data; 

#define DHT_GPIO GPIO_NUM_27
float humidity = 0;
float temperature = 0;

#define placa "ESP32"
#define Voltage_Resolution 5
#define ADC_Bit_Resolution 12
#define MQ131_pin GPIO_NUM_33
#define MQ7_pin GPIO_NUM_32
#define MQ131_type "MQ-131"
#define MQ7_type "MQ-7"
#define RatioMQ131CleanAir 75
#define RatioMQ7CleanAir 27.5
MQUnifiedsensor MQ131(placa, Voltage_Resolution, ADC_Bit_Resolution, MQ131_pin, MQ131_type);
MQUnifiedsensor MQ7(placa, Voltage_Resolution, ADC_Bit_Resolution, MQ7_pin, MQ7_type);

// Conversion constants
#define CONVERSION_FACTOR 0.0409
#define MG_TO_UG 1000.0  // 1 mg = 1000 µg
float CO_MWEIGHT = 28.01;   // Carbon monoxide
float O3_MWEIGHT = 48.00;   // Ozone
// Function to convert PPM to µg/m³
float ppm_to_ugm3(double ppm, double molecular_weight) {
    return CONVERSION_FACTOR * ppm * molecular_weight * MG_TO_UG;
}

#define REASSIGN_PINS
int sck = GPIO_NUM_18;
int miso = GPIO_NUM_19;
int mosi = GPIO_NUM_23;
int cs = GPIO_NUM_5;

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

ADS1115_WE ads(0x48);

// Timing variables
unsigned long previousMillis = 0;
const long interval = 10000; // Interval for reading sensors and logging data

void readFile(fs::FS &fs, const char *path) {
    Serial.printf("Reading file: %s\n", path);
    File file = fs.open(path);
    if (!file) {
        Serial.println("Failed to open file for reading");
        return;
    }
    Serial.print("Read from file: ");
    while (file.available()) {
        Serial.write(file.read());
    }
    file.close();
}

void writeFile(fs::FS &fs, const char *path, const char *message) {
    Serial.printf("Writing file: %s\n", path);
    File file = fs.open(path, FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open file for writing");
        return;
    }
    if (file.print(message)) {
        Serial.println("File written");
    } else {
        Serial.println("Write failed");
    }
    file.close();
}

void appendFile(fs::FS &fs, const char *path, const char *message) {
    Serial.printf("Appending to file: %s\n", path);
    File file = fs.open(path, FILE_APPEND);
    if (!file) {
        Serial.println("Failed to open file for appending");
        return;
    }
    if (file.print(message)) {
        Serial.println("Message appended");
    } else {
        Serial.println("Append failed");
    }
    file.close();
}

void renameFile(fs::FS &fs, const char *path1, const char *path2) {
    Serial.printf("Renaming file %s to %s\n", path1, path2);
    if (fs.rename(path1, path2)) {
        Serial.println("File renamed");
    } else {
        Serial.println("Rename failed");
    }
}

void deleteFile(fs::FS &fs, const char *path) {
    Serial.printf("Deleting file: %s\n", path);
    if (fs.remove(path)) {
        Serial.println("File deleted");
    } else {
        Serial.println("Delete failed");
    }
}

void logData(const char* data) {
    // Open the log file in append mode
    File logFile = SD.open("/aq_log.csv", FILE_APPEND);
    if (!logFile) {
        Serial.println("Failed to open log file for appending");
        return;
    }

    // Write the data to the log file
    if (logFile.println(data)) {
        Serial.println("Data logged successfully");
    } else {
        Serial.println("Failed to write data to log file");
    }

    // Ensure data is written to the file
    logFile.flush();
    logFile.close();
}

extern "C" void app_main() {
    initArduino(); // Initialize Arduino framework

    Serial.begin(115200);   // Initialize Serial for debugging
    while (!Serial) {
        delay(10);
    }
    Serial1.begin(9600, SERIAL_8N1, PMS_RX_PIN, PMS_TX_PIN); // Initialize Serial1 for PMS
    while (!Serial1) {
        delay(10);
    }
    Serial.println("Serial and Serial1 initialized.");

    Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

    // Initialize DS3231
    if (! rtc.begin()) {
        Serial.println("Couldn't find RTC");
        Serial.flush();
        while (1) delay(10);
    }

    if (rtc.lostPower()) {
        Serial.println("RTC lost power, let's set the time!");
        // When time needs to be set on a new device, or after a power loss, the
        // following line sets the RTC to the date & time this sketch was compiled
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        // This line sets the RTC with an explicit date & time, for example to set
        // January 21, 2014 at 3am you would call:
        // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
    }

    // Initialize SDCard
    #ifdef REASSIGN_PINS
    SPI.begin(sck, miso, mosi, cs);
    if (!SD.begin(cs)) {
    #else
    if (!SD.begin()) {
    #endif
        Serial.println("Card Mount Failed");
        return;
    }
    uint8_t cardType = SD.cardType();

    if (cardType == CARD_NONE) {
        Serial.println("No SD card attached");
        return;
    }

    Serial.print("SD Card Type: ");
    if (cardType == CARD_MMC) {
        Serial.println("MMC");
    } else if (cardType == CARD_SD) {
        Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);
    Serial.printf("Total space: %lluMB\n", SD.totalBytes() / (1024 * 1024));
    Serial.printf("Used space: %lluMB\n", SD.usedBytes() / (1024 * 1024));

    // Initialize the OLED display
    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        for(;;); // Don't proceed, loop forever
    }
    display.clearDisplay(); // Clear the display buffer

    // Initialize PMS7003
    pms.passiveMode(); // Set the PMS to passive mode

    //Initiate ADS1115
    ads.init();
    if(!ads.init()){
        Serial.println("ADS1115 not connected!");
    }
    ads.setVoltageRange_mV(ADS1115_RANGE_1024);
    ads.setConvRate(ADS1115_128_SPS);
    ads.setMeasureMode(ADS1115_SINGLE);

    // Initialize MQ sensors
    MQ131.setRegressionMethod(1);
    MQ131.setA(23.943);
    MQ131.setB(-1.11);
    //MQ131.setA(110.47); MQ131.setB(-2.862);
    MQ131.init();

    MQ7.setRegressionMethod(1);
    MQ7.setA(99.042);
    MQ7.setB(-1.518);
    MQ7.init();

    /*//Set R0 manually
    float MQ7_R0 = 22.64;
    MQ7.setR0(MQ7_R0);
    
    float MQ131_R0 = 12.93;
    MQ131.setR0(MQ131_R0);*/

    // Calibrate R0
    // Explanation: 
    // In this routine the sensor will measure the resistance of the sensor supposedly before being pre-heated
    // and on clean air (Calibration conditions), setting up R0 value.
    // We recommend executing this routine only on setup in laboratory conditions.
    // This routine does not need to be executed on each restart, you can load your R0 value from eeprom.
    // Acknowledgements: https://jayconsystems.com/blog/understanding-a-gas-sensor
    Serial.print("Calibrating please wait.");
    float calc131R0 = 0;
    printf("R0_131 before: %f\n", calc131R0);
    for(int i = 1; i<=10; i ++){
        ads.setSingleChannel(0);
        //for(int i=0; i<128; i++){ // counter is 32, conversion rate is 8 SPS --> 4s
            ads.startSingleMeasurement();
            while(ads.isBusy()){delay(0);}
        //}
        float voltageR0MQ131 = ads.getResult_V(); // Get voltage in volts
        printf("MQ131 Voltage: %f\n", voltageR0MQ131);
        MQ131.externalADCUpdate(voltageR0MQ131);
        calc131R0 += MQ131.calibrate(RatioMQ131CleanAir);
        Serial.print(".");
    }
    MQ131.setR0(calc131R0/10);
    printf("R0_131 after: %f\n", calc131R0);
    Serial.println("  done!.");
    if(isinf(calc131R0)) {Serial.println("Warning: Connection issue, R0 is infinite (Open circuit detected) please check your wiring and supply"); while(1);}
    if(calc131R0 == 0){Serial.println("Warning: Connection issue found, R0 is zero (Analog pin shorts to ground) please check your wiring and supply"); while(1);}
    MQ131.serialDebug(true);
    Serial.println("Ignore Ratio = RS/R0, for this example we will use readSensorR0Rs, the ratio calculated will be R0/Rs. Thanks :)");

    Serial.print("Calibrating please wait.");
    float calc7R0 = 0;
    for(int i = 1; i<=10; i ++){
        ads.setSingleChannel(1);
        //for(int i=0; i<128; i++){ // counter is 32, conversion rate is 8 SPS --> 4s
            ads.startSingleMeasurement();
            while(ads.isBusy()){delay(0);}
        //}
        float voltageR0MQ7 = ads.getResult_V(); // Get voltage in volts
        printf("MQ7 Voltage: %f\n", voltageR0MQ7);
        MQ7.externalADCUpdate(voltageR0MQ7);
        calc7R0 += MQ7.calibrate(RatioMQ7CleanAir);
        Serial.print(".");
    }
    MQ7.setR0(calc7R0/10);
    Serial.println("  done!.");
    if(isinf(calc7R0)) {Serial.println("Warning: Connection issue, R0 is infinite (Open circuit detected) please check your wiring and supply"); while(1);}
    if(calc7R0 == 0){Serial.println("Warning: Connection issue found, R0 is zero (Analog pin shorts to ground) please check your wiring and supply"); while(1);}
    MQ7.serialDebug(true);

    // Check if the log file exists
    if (!SD.exists("/aq_log.csv")) {
        Serial.println("Log file does not exist. Attempting to create it...");
        // Attempt to create the file
        writeFile(SD, "/aq_log.csv", "Date,Time,PM 2.5,PM 10.0,O3(PPM),CO(PPM),O3(ug/m3),CO(ug/m3),Temperature,Humidity\n");
    }
    else{
        printf("File existed!\n");
    }
    
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    while (true) {
        Blynk.run();
        
        unsigned long currentMillis = millis();

        if (currentMillis - previousMillis >= interval) {
            previousMillis = currentMillis;
            //Wake up PMS7003 before reading
            //pms.wakeUp();
            //delay(3000);
            
            // DS3231 task
            DateTime now = rtc.now();
            printf("%s, %d/%d/%d %02d:%02d:%02d\n",
            daysOfTheWeek[now.dayOfTheWeek()],  
            now.day(), 
            now.month(), 
            now.year(), 
            now.hour(), 
            now.minute(), 
            now.second());

            // PMS7003 task
            pms.requestRead(); // Request data from the sensor
            if (pms.readUntil(data)) { // Read data until available
                printf("PM2.5: %.dug/m3\nPM10.0: %.dug/m3\n", data.PM_AE_UG_2_5, data.PM_AE_UG_10_0);
                Blynk.virtualWrite(V0, data.PM_AE_UG_2_5);
                Blynk.virtualWrite(V1, data.PM_AE_UG_10_0);
            } else {
                Serial.println("No data from PMS7003.");
            }
            //pms.sleep();

            // MQ sensors task
            ads.setSingleChannel(0);
            //for(int i=0; i<128; i++){ // counter is 32, conversion rate is 8 SPS --> 4s
                ads.startSingleMeasurement();
                while(ads.isBusy()){delay(0);}
            //}
            float voltageMQ131 = ads.getResult_V(); // Get voltage in volts
            printf("MQ131 Voltage: %fmV\n", voltageMQ131);
            MQ131.externalADCUpdate(voltageMQ131);
            float MQ131_PPM = MQ131.readSensor();
            printf("O3 Concentration: %.2fppm\n", MQ131_PPM);
            Blynk.virtualWrite(V2, MQ131_PPM);

            ads.setSingleChannel(1);
            //for(int i=0; i<128; i++){ // counter is 32, conversion rate is 8 SPS --> 4s
                ads.startSingleMeasurement();
                while(ads.isBusy()){delay(0);}
            //}
            float voltageMQ7 = ads.getResult_V(); // Get voltage in volts
            printf("MQ7 Voltage: %fmV\n", voltageMQ7);
            MQ7.externalADCUpdate(voltageMQ7);
            float MQ7_PPM = MQ7.readSensor();
            printf("CO Concentration: %.2fppm\n", MQ7_PPM);
            Blynk.virtualWrite(V3, MQ7_PPM);

            // DHT22 task
            esp_err_t result = dht_read_float_data(DHT_TYPE_AM2301, DHT_GPIO, &humidity, &temperature); //DHT22
            // Check if reading is successful
            if (result == ESP_OK) {
                printf("Temperature: %.1f*C, Humidity: %.1f%%\n", temperature, humidity);
                Blynk.virtualWrite(V4, temperature);
            } else {
                printf("Failed to read data from DHT22 sensor: %d\n", result);
            }

            float converted_MQ131_PPM = ppm_to_ugm3(MQ131_PPM, O3_MWEIGHT);
            float converted_MQ7_PPM = ppm_to_ugm3(MQ7_PPM, CO_MWEIGHT);

            // Concatenate all data into a single string for logging
            char logEntry[200];
            sprintf(logEntry, "%d/%d/%d,%02d:%02d:%02d,%.d,%.d,%.2f,%.2f,%.2f,%.2f,%.1f,%.1f",
                    now.day(), now.month(),now.year(), 
                    now.hour(), now.minute(), now.second(),
                    data.PM_AE_UG_2_5, data.PM_AE_UG_10_0,
                    MQ131_PPM, MQ7_PPM,
                    converted_MQ131_PPM, converted_MQ7_PPM,
                    temperature, humidity);
            logData(logEntry); // Log the concatenated data

            // Update the OLED display with sensor data
            display.clearDisplay(); // Clear the display buffer
            display.setTextSize(2); // Normal text size
            display.setTextColor(SSD1306_WHITE); // Draw white text
            display.setCursor(0, 0); // Start at top-left corner

            // Display sensors data
            display.println("AirQuality");
            display.setTextSize(1);
            display.printf("PM2.5: %.dug/m3\n", data.PM_AE_UG_2_5);
            display.printf("PM10.0: %.dug/m3\n", data.PM_AE_UG_10_0);
            display.printf("O3: %.2fPPM\n", MQ131_PPM);
            display.printf("CO: %.2fPPM\n", MQ7_PPM);
            display.printf("Temp: %.1f*C\n", temperature);
            display.printf("Humid: %.1f%%\n", humidity);

            display.display(); // Show the display buffer on the screen
            
            printf("\n");

            vTaskDelay(9000 / portTICK_PERIOD_MS); //Sampling frequency
        }
    }
}