cmake_minimum_required(VERSION 3.5)

idf_component_register(SRCS "src/RTClib.cpp" "src/RTC_DS3231.cpp" "src/RTC_Micros.cpp" "src/RTC_Millis.cpp"
                       INCLUDE_DIRS "src"
                       REQUIRES arduino Adafruit_BusIO)

project(RTClib)
