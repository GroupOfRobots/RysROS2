cmake_minimum_required(VERSION 3.5)

### Package name
project(rys_executor)

### Compile options
include(../cmake_modules/compile_options.cmake)
add_compile_options(-pthread)

### Package settings
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(rys_interfaces REQUIRED)

set(HEADER_FILES
	src/helper_3dmath.hpp
	src/I2Cdev.hpp
	src/IMU.hpp
	src/MotorsController.hpp
	src/MPU6050.hpp
	src/MyExecutor.hpp
)
set(SOURCE_FILES
	src/I2Cdev.cpp
	src/IMU.cpp
	src/MotorsController.cpp
	src/MPU6050.cpp
	src/MyExecutor.cpp
)
set(AMENT_DEPENDENCIES
	rclcpp
	sensor_msgs
	std_msgs
	rys_interfaces
)

# ROS Node 'library'
add_library(${PROJECT_NAME}_node SHARED ${SOURCE_FILES})
target_compile_definitions(${PROJECT_NAME}_node PRIVATE "COMPOSITION_BUILDING_DLL")
ament_target_dependencies(${PROJECT_NAME}_node ${AMENT_DEPENDENCIES})
ament_export_libraries(${PROJECT_NAME}_node)

# Header files
FILE(GLOB ${PROJECT_NAME}_headers ${HEADER_FILES})

add_executable(main
	src/main.cpp
)
target_link_libraries(main ${PROJECT_NAME}_node)
ament_target_dependencies(main
	rclcpp
	std_msgs
	rys_interfaces
)

## Install targets
# node class object into lib/
install(TARGETS
	${PROJECT_NAME}_node
	ARCHIVE DESTINATION lib
	LIBRARY DESTINATION lib
	RUNTIME DESTINATION bin
)
# header files into include/
install(FILES ${${PROJECT_NAME}_headers}
	DESTINATION include/${PROJECT_NAME}
)

install(TARGETS
	main
	DESTINATION lib/${PROJECT_NAME}
)

ament_package()