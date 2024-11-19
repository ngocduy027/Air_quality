cmake_minimum_required(VERSION 3.5)

idf_component_register(SRCS "src/ADS1115_WE.cpp" 
                       INCLUDE_DIRS "src"
                       REQUIRES arduino)

project(ADS1115_WE)
