#include "RangesNode.hpp"
#include <iostream>
#include <stdexcept>

RangesNode::RangesNode(
	const std::string & robotName,
	const std::string & nodeName,
	const bool useIPC,
	const std::chrono::milliseconds loopDuration,
	const uint8_t pins[5],
	const uint8_t addresses[5]
) : rclcpp::Node(nodeName, robotName, useIPC) {
	// Create sensor objects
	for (int i = 0; i < 5; ++i) {
		this->sensors[i] = new VL53L0X(pins[i]);
		this->sensorInitialized[i] = false;
		try {
			this->sensors[i]->powerOff();
		} catch (const std::exception & error) {
			std::cerr << "[RANGES] Error disabling sensor " << i << ": " << error.what() << std::endl;
		}
	}

	if (!rclcpp::ok()) {
		return;
	}

	// Initialize the sensors
	for (int i = 0; rclcpp::ok() && i < 5; ++i) {
		try {
			this->sensors[i]->initialize();
			this->sensors[i]->setTimeout(200);
			this->sensors[i]->setMeasurementTimingBudget(20000);
			this->sensors[i]->setAddress(addresses[i]);
			this->sensorInitialized[i] = true;
		} catch (const std::exception & error) {
			std::cerr << "[RANGES] Error initializing sensor " << i << ": " << error.what() << std::endl;
		}
	}

	if (!rclcpp::ok()) {
		return;
	}

	// Start continuous back-to-back measurement
	for (int i = 0; rclcpp::ok() && i < 5; ++i) {
		if (this->sensorInitialized[i]) {
			std::cout << "[RANGES] Starting VL53L0X sensor: " << i << "\n";
			this->sensors[i]->startContinuous();
		}
	}

	this->publisher = this->create_publisher<rys_interfaces::msg::Ranges>("/" + robotName + "/sensor/ranges", rmw_qos_profile_sensor_data);
	this->timer = this->create_wall_timer(loopDuration, std::bind(&RangesNode::publishData, this));
	std::cout << "[RANGES] Node ready\n";
}

RangesNode::~RangesNode() {
	for (int i = 0; i < 5; ++i) {
		if (sensorInitialized[i]) {
			sensors[i]->stopContinuous();
		}
		sensors[i]->powerOff();
		delete sensors[i];
	}
}

int RangesNode::readSensor(int sensorIndex) {
	if (!sensorInitialized[sensorIndex]) {
		return -1;
	}

	int value = -1;
	// Actual reading can throw
	try {
		value = sensors[sensorIndex]->readRangeContinuousMillimeters();
	} catch (const std::exception & error) {
		std::cout << "[RANGES] Error reading distances from sensor " << sensorIndex << ": " << error.what() << std::endl;
		return -1;
	}

	// Checking timeout can not
	if (sensors[sensorIndex]->timeoutOccurred()) {
		std::cout << "[RANGES] Sensor " << sensorIndex << " timeout!\n";
		return -1;
	}

	return value;
}

void RangesNode::publishData() {
	auto message = std::make_shared<rys_interfaces::msg::Ranges>();

	message->header.stamp = this->now();
	message->header.frame_id = "vl53l0x";

	// Read sensors - offloaded to separate function as there are identical checks for every sensor
	message->front = this->readSensor(0);
	message->back = this->readSensor(1);
	message->top = this->readSensor(2);
	message->left = this->readSensor(3);
	message->right = this->readSensor(4);

	this->publisher->publish(message);
}
