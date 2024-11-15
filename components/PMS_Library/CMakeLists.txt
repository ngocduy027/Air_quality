cmake_minimum_required(VERSION 3.5)

idf_component_register(SRCS "src/PMS.cpp" 
                       INCLUDE_DIRS "src"
                       REQUIRES arduino)

project(PMS_Library)
