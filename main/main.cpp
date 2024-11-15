#include <Arduino.h>
#include <ds3231.h>
#include <PMS.h>
#include <dht.h>
#include <MQUnifiedsensor.h>
#include <SD.h>


//Define DS3231 pins
#define DS3231_SDA_PIN GPIO_NUM_21
#define DS3231_SCL_PIN GPIO_NUM_22

// Define the PMS7003 serial pins
#define PMS_RX_PIN GPIO_NUM_16  
#define PMS_TX_PIN GPIO_NUM_17 

#define DHT_GPIO GPIO_NUM_32  // Set the GPIO number where the DHT11 is connected

#define placa "ESP32"
#define Voltage_Resolution 3.3
#define ADC_Bit_Resolution 12
#define pin GPIO_NUM_27
#define type "MQ-135"
#define RatioMQ135CleanAir 3.6//RS / R0 = 3.6 ppm

#define REASSIGN_PINS
int sck = GPIO_NUM_18;
int miso = GPIO_NUM_19;
int mosi = GPIO_NUM_23;
int cs = GPIO_NUM_5;

i2c_dev_t ds3231_dev;

PMS pms(Serial1); // Use Serial1 for PMS communication
PMS::DATA data;

float humidity = 0;
float temperature = 0;

MQUnifiedsensor MQ135(placa, Voltage_Resolution, ADC_Bit_Resolution, pin, type);

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

    // Initialize DS3231
    i2cdev_init();
    ds3231_init_desc(&ds3231_dev, I2C_NUM_0, DS3231_SDA_PIN, DS3231_SCL_PIN);
    delay(100); // Add a small delay to allow I2C to stabilize

    /*// Set the time manually on the DS3231 (optional, only use if needed to set it initially)
    struct tm timeinfo = {0};
    timeinfo.tm_year = 2023 - 1900; // Year since 1900
    timeinfo.tm_mon = 3 - 1;        // Month (0-11)
    timeinfo.tm_mday = 15;           // Day of the month
    timeinfo.tm_hour = 12;           // Hour (0-23)
    timeinfo.tm_min = 0;             // Minutes
    timeinfo.tm_sec = 0;             // Seconds
    ds3231_set_time(&ds3231_dev, &timeinfo);*/

    // Read current time from DS3231
    struct tm current_time;
    esp_err_t ret_ds = ds3231_get_time(&ds3231_dev, &current_time);
    
    if (ret_ds != ESP_OK) {
        Serial.println("Failed to read time from DS3231");
        return; // Exit if reading time fails
    }

    // Print the current time and date
    printf("Current Time: %02d:%02d:%02d, Date: %02d/%02d/%04d\n", 
           current_time.tm_hour, current_time.tm_min, current_time.tm_sec, 
           current_time.tm_mday, current_time.tm_mon + 1, current_time.tm_year + 1900);

    Serial.print("Waking PMS7003 up");
    pms.passiveMode(); // Set the PMS to passive mode
    pms.wakeUp(); // Wake up the sensor

    delay(10000); // Wait for stable readings

    Serial.println("PMS7003 woken up!"); // Debug print

    MQ135.setRegressionMethod(1);
    MQ135.setA(110.47);
    MQ135.setB(-2.862);

    MQ135.init();

    //Calibrate MQ135
    Serial.print("Calibrating please wait.");
    float calcR0 = 0;
    for(int i = 1; i<=10; i ++)
    {
        MQ135.update(); // Update data, the arduino will read the voltage from the analog pin
        calcR0 += MQ135.calibrate(RatioMQ135CleanAir);
        Serial.print(".");
    }
    MQ135.setR0(calcR0/10);
    Serial.println("  done!.");
    
    if(isinf(calcR0)) {Serial.println("Warning: Conection issue, R0 is infinite (Open circuit detected) please check your wiring and supply"); while(1);}
    if(calcR0 == 0){Serial.println("Warning: Conection issue found, R0 is zero (Analog pin shorts to ground) please check your wiring and supply"); while(1);}
    /*****************************  MQ CAlibration ********************************************/ 
    MQ135.serialDebug(true);

    // Check if the log file exists
    if (!SD.exists("/aq_log.csv")) {
        Serial.println("Log file does not exist. Attempting to create it...");
        // Attempt to create the file
        writeFile(SD, "/aq_log.csv", "Date,Time,PM 1.0,PM 2.5,PM 10.0,MQ135 PPM,Temperature,Humidity\n");
    }

    while (true) {
        //PMS7003 task start here
        //pms.wakeUp();
        // Print the current time and date
        esp_err_t ret_ds = ds3231_get_time(&ds3231_dev, &current_time);
        if (ret_ds != ESP_OK) {
            Serial.println("Failed to read time from DS3231");
            return; // Exit if reading time fails
        }
        char timeData[50];
        sprintf(timeData, "%02d/%02d/%04d,%02d:%02d:%02d", current_time.tm_mday, current_time.tm_mon + 1, current_time.tm_year + 1900, current_time.tm_hour, current_time.tm_min, current_time.tm_sec);
        printf("Date: %s\n", timeData);
        //logData(timeData);

        pms.requestRead(); // Request data from the sensor
        char pmsData[20];
        sprintf(pmsData, "%.d,%.d,%.d", data.PM_AE_UG_1_0, data.PM_AE_UG_2_5, data.PM_AE_UG_10_0);
        if (pms.readUntil(data)) { // Read data until available
            Serial.println(pmsData);
            //logData(pmsData);
        } else {
            Serial.println("No data from PMS7003.");
        }

        //Serial.println("Going to sleep for 60 seconds.");
        //pms.sleep(); // Put the sensor to sleep
        delay(500); // Wait for 5 seconds before the next read

        //MQ135 task start here
        MQ135.update(); // Update data, the arduino will read the voltage from the analog pin
        MQ135.readSensor(); // Sensor will read PPM concentration using the model, a and b values set previously or from the setup
        float My_PPM = MQ135.getPPM();
        char mqData[5];
        sprintf(mqData, "%.2f", My_PPM);
        Serial.println(mqData);
        //logData(mqData);

        vTaskDelay(500 / portTICK_PERIOD_MS); //Sampling frequency

        //DHT11 task start here
        esp_err_t result = dht_read_float_data(DHT_TYPE_DHT11, DHT_GPIO, &humidity, &temperature);
        char dhtData[10];
        sprintf(dhtData, "%.1f,%.1f%%", temperature, humidity);

        // Check if reading is successful
        if (result == ESP_OK) {
            // Print the results (note that the values are scaled by 10)
            Serial.println(dhtData);
            //logData(dhtData);
        } else {
            // Handle the error (print an error message)
            printf("Failed to read data from DHT11 sensor: %d\n", result);
        }

        // Concatenate all data into a single string for logging
        char logEntry[200];
        sprintf(logEntry, "%02d/%02d/%04d,%02d:%02d:%02d,%.d,%.d,%.d,%.2f,%.1f,%.1f%%",
                current_time.tm_mday, current_time.tm_mon + 1, current_time.tm_year + 1900, 
                current_time.tm_hour, current_time.tm_min, current_time.tm_sec,
                data.PM_AE_UG_1_0, data.PM_AE_UG_2_5, data.PM_AE_UG_10_0,
                My_PPM,
                temperature, humidity);
        logData(logEntry); // Log the concatenated data

        printf("\n");
        // Wait for 2 seconds before the next read
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}