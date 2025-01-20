#define BLYNK_TEMPLATE_ID "TMPL63aGqNxKU"
#define BLYNK_TEMPLATE_NAME "Air Quality"
#define BLYNK_AUTH_TOKEN "8o6SipbVqZJVbJrYZCickzBm5d78dnN0"

#include <Arduino.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <esp_system.h>
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

char ssid[] = "Galaxy S23 1A77";
char pass[] = "kvzrzwi2yya4czi";

RTC_DS3231 rtc;
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

#define PMS_RX_PIN GPIO_NUM_17  
#define PMS_TX_PIN GPIO_NUM_16
PMS pms(Serial1); // Use Serial1 for PMS communication
PMS::DATA data; 

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
float CO_MWEIGHT = 28.01;  
float O3_MWEIGHT = 48.00; 
// Function to convert PPM to µg/m³
float ppm_to_ugm3(double ppm, double molecular_weight) {
    return CONVERSION_FACTOR * ppm * molecular_weight * MG_TO_UG;
}

#define DHT_GPIO GPIO_NUM_27

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

// Define event bits for each sensor
#define RTC_BIT     ( 1 << 0 )
#define PMS_BIT     ( 1 << 1 )
#define MQ131_BIT   ( 1 << 2 )
#define MQ7_BIT     ( 1 << 3 )
#define DHT_BIT     ( 1 << 4 )
#define LOG_BIT     ( 1 << 5 )
#define BLYNK_BIT   ( 1 << 6 )
#define AQI_BIT     ( 1 << 7 )
EventGroupHandle_t sensorEventGroup;
QueueHandle_t sensorDataQueue;
uint8_t sensorTask = 5;
uint8_t dataDisplayTask = 3;

typedef struct {
    uint8_t current_day;
    uint8_t current_month;
    uint8_t current_hour;
    uint8_t current_minute;
    uint8_t current_second;
    uint16_t current_year;
    uint16_t PM_AE_UG_2_5;
    uint16_t PM_AE_UG_10_0;
    float MQ131_PPM;
    float MQ7_PPM;
    float humidity;
    float temperature;
    uint16_t aqiPM25;
    uint16_t aqiPM10;
    uint16_t aqiO3;
    uint16_t aqiCO;
} SensorsData;
SensorsData dataUpdate;

int aqiSummary;

TaskHandle_t alarmTaskHandle = NULL;

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

void logData(const char* data) {
    // Open the log file in append mode
    File logFile = SD.open("/aq_log.csv", FILE_APPEND);
    if (!logFile) {
        Serial.println("Failed to open log file for appending");
        return;
    }

    // Write the data to the log file
    if (logFile.println(data)) {
        Serial.println("Data logged successfully\n");
    } else {
        Serial.println("Failed to write data to log file\n");
    }

    // Ensure data is written to the file
    logFile.flush();
    logFile.close();
}

void rtc_task(void *pvParameters) {
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 15000 / portTICK_PERIOD_MS;
    xLastWakeTime = xTaskGetTickCount();

    while(true) {
        // DS3231 task
        DateTime now = rtc.now();
        dataUpdate.current_day = now.day();
        dataUpdate.current_month = now.month();
        dataUpdate.current_year = now.year();
        dataUpdate.current_hour = now.hour();
        dataUpdate.current_minute = now.minute();
        dataUpdate.current_second = now.second();
        printf("%s, %d/%d/%d %02d:%02d:%02d\n",
                daysOfTheWeek[now.dayOfTheWeek()],  
                dataUpdate.current_day, dataUpdate.current_month, dataUpdate.current_year,
                dataUpdate.current_hour, dataUpdate.current_minute, dataUpdate.current_second);

        xQueueSend(sensorDataQueue, &dataUpdate, portMAX_DELAY);
        xEventGroupSetBits(sensorEventGroup, RTC_BIT);
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void pms_task(void *pvParameters) {
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 15000 / portTICK_PERIOD_MS;
    xLastWakeTime = xTaskGetTickCount();

    while(true) {
        // PMS7003 task
        pms.requestRead(); // Request data from the sensor
        if (pms.readUntil(data)) { // Read data until available
            dataUpdate.PM_AE_UG_2_5 = data.PM_AE_UG_2_5;
            dataUpdate.PM_AE_UG_10_0 = data.PM_AE_UG_10_0;
            printf("PM2.5: %.dug/m3\nPM10.0: %.dug/m3\n", dataUpdate.PM_AE_UG_2_5, dataUpdate.PM_AE_UG_10_0);
        } else {
            Serial.println("No data from PMS7003.");
        }

        xQueueSend(sensorDataQueue, &dataUpdate, portMAX_DELAY);
        xEventGroupSetBits(sensorEventGroup, PMS_BIT);
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void mq131_task(void *pvParameters) {
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 15000 / portTICK_PERIOD_MS;
    xLastWakeTime = xTaskGetTickCount();

    while(true) {
        // MQ sensors task
        ads.setSingleChannel(0);
        ads.startSingleMeasurement();
        while(ads.isBusy()){delay(0);}
        float voltageMQ131 = ads.getResult_V(); // Get voltage in volts
        //printf("MQ131 Voltage: %fV\n", voltageMQ131);
        MQ131.externalADCUpdate(voltageMQ131);
        dataUpdate.MQ131_PPM = MQ131.readSensor();
        printf("O3 Concentration: %.2fppm\n", dataUpdate.MQ131_PPM);

        xQueueSend(sensorDataQueue, &dataUpdate, portMAX_DELAY);
        xEventGroupSetBits(sensorEventGroup, MQ131_BIT);
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void mq7_task(void *pvParameters) {
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 15000 / portTICK_PERIOD_MS;
    xLastWakeTime = xTaskGetTickCount();

    while(true) {
        ads.setSingleChannel(1);
        ads.startSingleMeasurement();
        while(ads.isBusy()){delay(0);}
        float voltageMQ7 = ads.getResult_V(); // Get voltage in volts
        //printf("MQ7 Voltage: %fV\n", voltageMQ7);
        MQ7.externalADCUpdate(voltageMQ7);
        dataUpdate.MQ7_PPM = MQ7.readSensor();
        printf("CO Concentration: %.2fppm\n", dataUpdate.MQ7_PPM);

        xQueueSend(sensorDataQueue, &dataUpdate, portMAX_DELAY);
        xEventGroupSetBits(sensorEventGroup, MQ7_BIT);
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void dht_task(void *pvParameters) {
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 15000 / portTICK_PERIOD_MS;
    xLastWakeTime = xTaskGetTickCount();

    while(true) {
        // DHT22 task
        esp_err_t result = dht_read_float_data(DHT_TYPE_AM2301, DHT_GPIO, &dataUpdate.humidity, &dataUpdate.temperature);
        if (result == ESP_OK) {
            printf("Temperature: %.1f*C, Humidity: %.1f%%\n", dataUpdate.temperature, dataUpdate.humidity);
        } else {
            printf("Failed to read data from DHT22 sensor: %d\n", result);
        }

        xQueueSend(sensorDataQueue, &dataUpdate, portMAX_DELAY);
        xEventGroupSetBits(sensorEventGroup, DHT_BIT);
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void display_task(void *pvParameters) {
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 15000 / portTICK_PERIOD_MS;
    xLastWakeTime = xTaskGetTickCount();

    while(true) {
        xEventGroupWaitBits(sensorEventGroup, BLYNK_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
        // Receive data from the queue
        xQueueReceive(sensorDataQueue, &dataUpdate, portMAX_DELAY);
        // Update the OLED display with sensor data
        display.clearDisplay(); 
        display.setTextSize(2); 
        display.setTextColor(SSD1306_WHITE); 
        display.setCursor(0, 0); // Start at top-left corner

        // Display sensors data
        display.printf("AQI: %d\n", aqiSummary);
        display.setTextSize(1);
        display.printf("PM2.5: %.dug/m3\n", dataUpdate.PM_AE_UG_2_5);
        display.printf("PM10.0: %.dug/m3\n", dataUpdate.PM_AE_UG_10_0);
        display.printf("O3: %.2fPPM\n", dataUpdate.MQ131_PPM);
        display.printf("CO: %.2fPPM\n", dataUpdate.MQ7_PPM);
        display.printf("Temp: %.1f*C\n", dataUpdate.temperature);
        display.printf("Humid: %.1f%%\n", dataUpdate.humidity);

        display.display(); // Show the display buffer on the screen
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void log_task(void *pvParameters) {
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 15000 / portTICK_PERIOD_MS;
    xLastWakeTime = xTaskGetTickCount();

    while(true) {
        // Wait for all sensors to complete
        xEventGroupWaitBits(sensorEventGroup, AQI_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
        xQueueReceive(sensorDataQueue, &dataUpdate, portMAX_DELAY);
        // Concatenate all data into a single string for logging
        char logEntry[200];
        sprintf(logEntry, "%d/%d/%d,%02d:%02d:%02d,%.d,%.d,%.2f,%.2f,%.1f,%.1f,%d,%d,%d,%d",
                dataUpdate.current_day, dataUpdate.current_month, dataUpdate.current_year,
                dataUpdate.current_hour, dataUpdate.current_minute, dataUpdate.current_second,
                dataUpdate.PM_AE_UG_2_5, dataUpdate.PM_AE_UG_10_0,
                dataUpdate.MQ131_PPM, dataUpdate.MQ7_PPM,
                dataUpdate.temperature, dataUpdate.humidity,
                dataUpdate.aqiPM25, dataUpdate.aqiPM10,
                dataUpdate.aqiO3, dataUpdate.aqiCO);
        logData(logEntry); // Log the concatenated data
        xEventGroupSetBits(sensorEventGroup, LOG_BIT);
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void blynk_task(void *pvParameters) {
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 15000 / portTICK_PERIOD_MS;
    xLastWakeTime = xTaskGetTickCount();

    while(true) {
        xEventGroupWaitBits(sensorEventGroup, LOG_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
        // Receive data from the queue
        xQueueReceive(sensorDataQueue, &dataUpdate, portMAX_DELAY);
        Blynk.run();
        // Send data to Blynk
        Blynk.virtualWrite(V0, dataUpdate.PM_AE_UG_2_5);
        Blynk.virtualWrite(V1, dataUpdate.PM_AE_UG_10_0);
        Blynk.virtualWrite(V2, dataUpdate.MQ131_PPM);
        Blynk.virtualWrite(V3, dataUpdate.MQ7_PPM);
        Blynk.virtualWrite(V4, dataUpdate.temperature);

        xEventGroupSetBits(sensorEventGroup, BLYNK_BIT);
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

float calculateAQI(float concentration, float c_low, float c_high, int aqi_low, int aqi_high) {
    return ((aqi_high - aqi_low) / (c_high - c_low)) * (concentration - c_low) + aqi_low;
}

void aqi_task(void *pvParameters) {
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 15000 / portTICK_PERIOD_MS;
    xLastWakeTime = xTaskGetTickCount();

    while(true) {
        // Wait for all sensors to complete
        xEventGroupWaitBits(sensorEventGroup, RTC_BIT | PMS_BIT | MQ131_BIT | MQ7_BIT | DHT_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
        for(int i = 0; i < sensorTask; i++) {
            xQueueReceive(sensorDataQueue, &dataUpdate, portMAX_DELAY);
        }
        // Calculate AQI for PM2.5 (max 500 value is 325.4 μg/m3)
        if (dataUpdate.PM_AE_UG_2_5 <= 9.0) {
            dataUpdate.aqiPM25 = calculateAQI(dataUpdate.PM_AE_UG_2_5, 0, 9.0, 0, 50);
        } else if (dataUpdate.PM_AE_UG_2_5 <= 35.4) {
            dataUpdate.aqiPM25 = calculateAQI(dataUpdate.PM_AE_UG_2_5, 9.1, 35.4, 51, 100);
        } else if (dataUpdate.PM_AE_UG_2_5 <= 55.4) {
            dataUpdate.aqiPM25 = calculateAQI(dataUpdate.PM_AE_UG_2_5, 35.5, 55.4, 101, 150);
        } else if (dataUpdate.PM_AE_UG_2_5 <= 125.4) {
            dataUpdate.aqiPM25 = calculateAQI(dataUpdate.PM_AE_UG_2_5, 55.5, 125.4, 151, 200);
        } else if (dataUpdate.PM_AE_UG_2_5 <= 225.4) {
            dataUpdate.aqiPM25 = calculateAQI(dataUpdate.PM_AE_UG_2_5, 125.5, 225.4, 201, 300);
        } else {
            dataUpdate.aqiPM25 = calculateAQI(dataUpdate.PM_AE_UG_2_5, 225.5, 325.4, 301, 500);
        }

        // Calculate AQI for PM10 (max 500 value is 604 μg/m3)
        if (dataUpdate.PM_AE_UG_10_0 <= 54) {
            dataUpdate.aqiPM10 = calculateAQI(dataUpdate.PM_AE_UG_10_0, 0, 54, 0, 50);
        } else if (dataUpdate.PM_AE_UG_10_0 <= 154) {
            dataUpdate.aqiPM10 = calculateAQI(dataUpdate.PM_AE_UG_10_0, 55, 154, 51, 100);
        } else if (dataUpdate.PM_AE_UG_10_0 <= 254) {
            dataUpdate.aqiPM10 = calculateAQI(dataUpdate.PM_AE_UG_10_0, 155, 254, 101, 150);
        } else if (dataUpdate.PM_AE_UG_10_0 <= 354) {
            dataUpdate.aqiPM10 = calculateAQI(dataUpdate.PM_AE_UG_10_0, 255, 354, 151, 200);
        } else if (dataUpdate.PM_AE_UG_10_0 <= 424) {
            dataUpdate.aqiPM10 = calculateAQI(dataUpdate.PM_AE_UG_10_0, 355, 424, 201, 300);
        } else {
            dataUpdate.aqiPM10 = calculateAQI(dataUpdate.PM_AE_UG_10_0, 425, 604, 301, 500);
        }

        // Calculate AQI for O3 (max 500 value is 0.604 ppm)
        if (dataUpdate.MQ131_PPM <= 0.054) {
            dataUpdate.aqiO3 = calculateAQI(dataUpdate.MQ131_PPM, 0, 0.054, 0, 50);
        } else if (dataUpdate.MQ131_PPM <= 0.070) {
            dataUpdate.aqiO3 = calculateAQI(dataUpdate.MQ131_PPM, 0.055, 0.070, 51, 100);
        } else if (dataUpdate.MQ131_PPM <= 0.085) {
            dataUpdate.aqiO3 = calculateAQI(dataUpdate.MQ131_PPM, 0.071, 0.085, 101, 150);
        } else if (dataUpdate.MQ131_PPM <= 0.105) {
            dataUpdate.aqiO3 = calculateAQI(dataUpdate.MQ131_PPM, 0.086, 0.105, 151, 200);
        } else if (dataUpdate.MQ131_PPM <= 0.200) {
            dataUpdate.aqiO3 = calculateAQI(dataUpdate.MQ131_PPM, 0.106, 0.200, 201, 300);
        } else {
            dataUpdate.aqiO3 = calculateAQI(dataUpdate.MQ131_PPM, 0.201, 0.604, 301, 500);
        }

        // Calculate AQI for CO (max 500 value is 50.4 ppm)
        if (dataUpdate.MQ7_PPM <= 4.4) {
            dataUpdate.aqiCO = calculateAQI(dataUpdate.MQ7_PPM, 0, 4.4, 0, 50);
        } else if (dataUpdate.MQ7_PPM <= 9.4) {
            dataUpdate.aqiCO = calculateAQI(dataUpdate.MQ7_PPM, 4.5, 9.4, 51, 100);
        } else if (dataUpdate.MQ7_PPM <= 12.4) {
            dataUpdate.aqiCO = calculateAQI(dataUpdate.MQ7_PPM, 9.5, 12.4, 101, 150);
        } else if (dataUpdate.MQ7_PPM <= 15.4) {
            dataUpdate.aqiCO = calculateAQI(dataUpdate.MQ7_PPM, 12.5, 15.4, 151, 200);
        } else if (dataUpdate.MQ7_PPM <= 30.4) {
            dataUpdate.aqiCO = calculateAQI(dataUpdate.MQ7_PPM, 15.5, 30.4, 201, 300);
        } else {
            dataUpdate.aqiCO = calculateAQI(dataUpdate.MQ7_PPM, 40.5, 50.4, 401, 500);
        }

        // Calculate summary AQI (highest value)
        aqiSummary = max(max(max(dataUpdate.aqiPM25, dataUpdate.aqiPM10), 
                         dataUpdate.aqiO3), dataUpdate.aqiCO);

        // Check if air quality is unhealthy or worse
        if (aqiSummary >= 151) {
            xTaskNotify(alarmTaskHandle, aqiSummary, eSetValueWithOverwrite);
        }
        for(int i = 0; i < dataDisplayTask; i++) {
            xQueueSend(sensorDataQueue, &dataUpdate, portMAX_DELAY);
        }
        xEventGroupSetBits(sensorEventGroup, AQI_BIT);
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void alarm_task(void *pvParameters) {
    const int LED_PIN = 2;  // Onboard LED pin for ESP32 DevKit V1
    pinMode(LED_PIN, OUTPUT);
    
    while(true) {
        uint32_t aqiValue;
        if (xTaskNotifyWait(0, 0, &aqiValue, portMAX_DELAY) == pdTRUE) {
            String warningMsg;
            if (aqiValue >= 301) {
                warningMsg = "HAZARDOUS Air Quality!";
            } else if (aqiValue >= 201) {
                warningMsg = "VERY UNHEALTHY Air Quality!";
            } else if (aqiValue >= 151) {
                warningMsg = "UNHEALTHY Air Quality!";
            }
            
            // Print warning and flash LED
            Serial.println(warningMsg);
            for(int i = 0; i < 3; i++) {
                digitalWrite(LED_PIN, HIGH);
                vTaskDelay(500 / portTICK_PERIOD_MS);
                digitalWrite(LED_PIN, LOW);
                vTaskDelay(500 / portTICK_PERIOD_MS);
            }
        }
    }
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

    // Check if the log file exists
    if (!SD.exists("/aq_log.csv")) {
        Serial.println("Log file does not exist. Attempting to create it...");
        // Attempt to create the file
        writeFile(SD, "/aq_log.csv", "Date,Time,PM 2.5,PM 10.0,O3(PPM),CO(PPM),Temperature,Humidity,AQI_PM2.5,AQI_PM10,AQI_O3,AQI_CO\n");
    }
    else{
        printf("File existed!\n");
    }

    // Initialize DS3231
    if (! rtc.begin()) {
        Serial.println("Couldn't find RTC");
        Serial.flush();
        while (1) delay(10);
    }

    //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

    if (rtc.lostPower()) {
        Serial.println("RTC lost power, let's set the time!");
        // When time needs to be set on a new device, or after a power loss, the
        // following line sets the RTC to the date & time this sketch was compiled
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        // This line sets the RTC with an explicit date & time, for example to set
        // January 21, 2014 at 3am you would call:
        // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
    }

    // Initialize PMS7003
    pms.passiveMode();
    pms.wakeUp();

    //Initiate ADS1115
    ads.init();
    if(!ads.init()){
        Serial.println("ADS1115 not connected!");
    }
    else{
        Serial.println("ADS1115 initialized!");
    }
    ads.setVoltageRange_mV(ADS1115_RANGE_1024);
    ads.setConvRate(ADS1115_128_SPS);
    ads.setMeasureMode(ADS1115_SINGLE);

    // Initialize MQ sensors
    MQ131.setRegressionMethod(1);
    MQ131.setA(23.943);
    MQ131.setB(-1.11);
    MQ131.init();

    /*//Set R0 manually
    float MQ131_R0 = 12.93;
    MQ131.setR0(MQ131_R0);*/

    // Calibrate R0
    Serial.print("Calibrating please wait.");
    float calc131R0 = 0;
    printf("R0_131 before: %f\n", calc131R0);
    for(int i = 1; i<=10; i ++){
        ads.setSingleChannel(0);
        ads.startSingleMeasurement();
        while(ads.isBusy()){delay(0);}
        float voltageR0MQ131 = ads.getResult_V(); // Get voltage in volts
        printf("MQ131 Voltage: %f\n", voltageR0MQ131);
        MQ131.externalADCUpdate(voltageR0MQ131);
        calc131R0 += MQ131.calibrate(RatioMQ131CleanAir);
        Serial.print(".");
    }
    MQ131.setR0(calc131R0/10);
    printf("R0_131 after: %f\n", calc131R0);
    Serial.println("  done!.");
    if(isinf(calc131R0)) {
        Serial.println("Warning: Connection issue, R0 is infinite (Open circuit detected) please check your wiring and supply");
        while(1);
    }
    if(calc131R0 == 0) {
        Serial.println("Warning: Connection issue found, R0 is zero (Analog pin shorts to ground) please check your wiring and supply");
        while(1);
    }
    MQ131.serialDebug(true);

    MQ7.setRegressionMethod(1);
    MQ7.setA(99.042);
    MQ7.setB(-1.518);
    MQ7.init();

    /*//Set R0 manually
    float MQ7_R0 = 22.64;
    MQ7.setR0(MQ7_R0);*/

    // Calibrate R0
    Serial.print("Calibrating please wait.");
    float calc7R0 = 0;
    for(int i = 1; i<=10; i ++){
        ads.setSingleChannel(1);
        ads.startSingleMeasurement();
        while(ads.isBusy()){delay(0);}
        float voltageR0MQ7 = ads.getResult_V(); // Get voltage in volts
        printf("MQ7 Voltage: %f\n", voltageR0MQ7);
        MQ7.externalADCUpdate(voltageR0MQ7);
        calc7R0 += MQ7.calibrate(RatioMQ7CleanAir);
        Serial.print(".");
    }
    MQ7.setR0(calc7R0/10);
    Serial.println("  done!.");
    if(isinf(calc7R0)) {
        Serial.println("Warning: Connection issue, R0 is infinite (Open circuit detected) please check your wiring and supply");
        while(1);
    }
    if(calc7R0 == 0) {
        Serial.println("Warning: Connection issue found, R0 is zero (Analog pin shorts to ground) please check your wiring and supply");
        while(1);
    }
    MQ7.serialDebug(true);

    // Initialize the OLED display
    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        for(;;); // Don't proceed, loop forever
    }
    display.clearDisplay(); // Clear the display buffer

    vTaskDelay(5000 / portTICK_PERIOD_MS); //Wait to stabilize sensors for reading

    sensorEventGroup = xEventGroupCreate();
    
    // Create the queue
    sensorDataQueue = xQueueCreate(8, sizeof(SensorsData));
    if (sensorDataQueue == NULL) {
        Serial.println("Failed to create sensor data queue");
        return;
    }

    // Sensor tasks
    xTaskCreate(&rtc_task, "rtc_task", 2304, NULL, 5, NULL);
    xTaskCreate(&pms_task, "pms_task", 2496, NULL, 5, NULL);
    xTaskCreate(&mq131_task, "mq131_task", 2560, NULL, 5, NULL);
    xTaskCreate(&mq7_task, "mq7_task", 2624, NULL, 5, NULL);
    xTaskCreate(&dht_task, "dht_task", 2688, NULL, 5, NULL);

    // Data processing tasks
    xTaskCreate(&aqi_task, "aqi_task", 2048, NULL, 4, NULL);
    xTaskCreate(&log_task, "log_task", 2944, NULL, 4, NULL);
    xTaskCreate(&blynk_task, "blynk_task", 3712, NULL, 3, NULL);
    xTaskCreate(&display_task, "display_task", 2688, NULL, 1, NULL);

    //Alarm task
    xTaskCreate(&alarm_task, "alarm_task", 2304, NULL, 5, &alarmTaskHandle);
}