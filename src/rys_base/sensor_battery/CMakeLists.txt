cmake_minimum_required(VERSION 3.5)

### Package name
project(rys_sensor_battery)

### Compile options
include(../cmake_modules/compile_options.cmake)

### Package settings
# Dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rys_interfaces REQUIRED)

set(HEADER_FILES
	src/BatteryNode.hpp
)
set(SOURCE_FILES
	src/BatteryNode.cpp
)
set(AMENT_DEPENDENCIES
	rclcpp
	rys_interfaces
)

### Package definitions, install targets etc
include(../cmake_modules/package_targets.cmake)
