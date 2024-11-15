#include <Arduino.h>
#include <RTClib.h>
#include <PMS.h>
#include <dht.h>
#include <MQUnifiedsensor.h>
#include <SD.h>
#include <Adafruit_SSD1306.h>

RTC_DS3231 rtc;
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

// Define the PMS7003 serial pins
#define PMS_RX_PIN GPIO_NUM_16  
#define PMS_TX_PIN GPIO_NUM_17 

#define DHT_GPIO GPIO_NUM_32  // Set the GPIO number where the DHT11 is connected

#define placa "ESP32"
#define Voltage_Resolution 3.3
#define ADC_Bit_Resolution 12
#define pin GPIO_NUM_27
#define type "MQ-135"
#define RatioMQ135CleanAir 3.6 // RS / R0 = 3.6 ppm

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

PMS pms(Serial1); // Use Serial1 for PMS communication
PMS::DATA data;

float humidity = 0;
float temperature = 0;

MQUnifiedsensor MQ135(placa, Voltage_Resolution, ADC_Bit_Resolution, pin, type);

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
    logFile.close(); // Close the file after writing
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
    Serial.println("Serial and Serial1 initialized."); // Debug print

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

    // Initialize DS3231


    Serial.print("Waking PMS7003 up");
    pms.passiveMode(); // Set the PMS to passive mode
    pms.wakeUp(); // Wake up the sensor

    delay(5000); // Wait for stable readings of PMS7003

    Serial.println("PMS7003 woken up!"); // Debug print

    MQ135.setRegressionMethod(1);
    MQ135.setA(110.47);
    MQ135.setB(-2.862);
    MQ135.init();

    // Calibrate MQ135
    Serial.print("Calibrating please wait.");
    float calcR0 = 0;
    for(int i = 1; i <= 10; i++) {
        MQ135.update(); // Update data, the arduino will read the voltage from the analog pin
        calcR0 += MQ135.calibrate(RatioMQ135CleanAir);
        Serial.print(".");
    }
    MQ135.setR0(calcR0 / 10);
    Serial.println("  done!.");
    
    if (isinf(calcR0)) {
        Serial.println("Warning: Connection issue, R0 is infinite (Open circuit detected) please check your wiring and supply");
        while (1);
    }
    if (calcR0 == 0) {
        Serial.println("Warning: Connection issue found, R0 is zero (Analog pin shorts to ground) please check your wiring and supply");
        while (1);
    }
    /*****************************  MQ CAlibration ********************************************/
    MQ135.serialDebug(true);

    // Check if the log file exists
    if (!SD.exists("/aq_log.csv")) {
        Serial.println("Log file does not exist. Attempting to create it...");
        // Attempt to create the file
        writeFile(SD, "/aq_log.csv", "Date,Time,PM 1.0,PM 2.5,PM 10.0,MQ135 PPM,Temperature,Humidity\n");
    }
    else{
        printf("File existed!\n");
    }

    while (true) {
        unsigned long currentMillis = millis();

        if (currentMillis - previousMillis >= interval) {
            previousMillis = currentMillis;
            //Wake up PMS7003 before reading
            printf("Waking PMS7003\n");
            pms.wakeUp();
            delay(5000);
            
            // DS3231 task
            DateTime now = rtc.now();
            printf("%d/%d/%d (%s) %02d:%02d:%02d\n", 
            now.year(), 
            now.month(), 
            now.day(), 
            daysOfTheWeek[now.dayOfTheWeek()], 
            now.hour(), 
            now.minute(), 
            now.second());

            // PMS7003 task
            pms.requestRead(); // Request data from the sensor
            if (pms.readUntil(data)) { // Read data until available
                printf("PM1.0: %.d, PM2.5: %.d, PM10.0: %.d\n", data.PM_AE_UG_1_0, data.PM_AE_UG_2_5, data.PM_AE_UG_10_0);
            } else {
                Serial.println("No data from PMS7003.");
            }
            pms.sleep();

            // MQ135 task
            MQ135.update(); // Update data, the arduino will read the voltage from the analog pin
            MQ135.readSensor(); // Sensor will read PPM concentration using the model, a and b values set previously or from the setup
            float My_PPM = MQ135.getPPM();
            printf("CO2: %.2f\n", My_PPM);

            // DHT11 task
            esp_err_t result = dht_read_float_data(DHT_TYPE_DHT11, DHT_GPIO, &humidity, &temperature);
            // Check if reading is successful
            if (result == ESP_OK) {
                printf("Temperature: %.1f, Humidity: %.1f%%\n", temperature, humidity);
            } else {
                printf("Failed to read data from DHT11 sensor: %d\n", result);
            }

            // Concatenate all data into a single string for logging
            char logEntry[200];
            sprintf(logEntry, "%d/%d/%d,%02d:%02d:%02d,%.d,%.d,%.d,%.2f,%.1f,%.1f%%",
                    now.day(), now.month(),now.year(), 
                    now.hour(), now.minute(), now.second(),
                    data.PM_AE_UG_1_0, data.PM_AE_UG_2_5, data.PM_AE_UG_10_0,
                    My_PPM,
                    temperature, humidity);
            logData(logEntry); // Log the concatenated data

            // Update the OLED display with sensor data
            display.clearDisplay(); // Clear the display buffer
            display.setTextSize(2); // Normal text size
            display.setTextColor(SSD1306_WHITE); // Draw white text
            display.setCursor(0, 0); // Start at top-left corner

            // Display the AQI and sensor data
            display.println("AQI");
            display.setTextSize(1);
            display.printf("PM2.5: %.d\n", data.PM_AE_UG_2_5);
            display.printf("PM10.0: %.d\n", data.PM_AE_UG_10_0);
            display.printf("CO2: %.2f\n", My_PPM);
            display.printf("Temp: %.1f\n", temperature);
            display.printf("Humid: %.1f%%\n", humidity);

            display.display(); // Show the display buffer on the screen
            
            printf("\n");

            //vTaskDelay(500 / portTICK_PERIOD_MS); //Sampling frequency
        }
    }
}