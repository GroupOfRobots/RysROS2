#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include "rclcpp/rclcpp.hpp"

#include "TemperatureNode.hpp"

#include <sched.h>
#include <sys/mman.h>

using namespace std::chrono_literals;

TemperatureNode::TemperatureNode(
	const std::string & robotName,
	const std::string & nodeName,
	const bool useIPC,
	std::chrono::milliseconds rate,
	const uint8_t inputNumber,
	const float coefficient,
	const float criticalLevel,
	const float hysteresis,
	const int readings
) : rclcpp::Node(nodeName, robotName, useIPC) {
	this->filename = std::string("/sys/devices/platform/ocp/44e0d000.tscadc/TI-am335x-adc/iio:device0/in_voltage") + std::to_string(inputNumber) + std::string("_raw");
	this->coefficient = coefficient;
	this->criticalLevel = criticalLevel;
	this->hysteresis = hysteresis;
	this->readings = readings;

	this->isCritical = true;
	this->currentReadings = 0;
	this->voltageSum = 0;

	this->publisher = this->create_publisher<rys_interfaces::msg::TemperatureStatus>("/" + robotName + "/sensor/temperature", rmw_qos_profile_default);
	this->timer = this->create_wall_timer(rate/5, std::bind(&TemperatureNode::readData, this));
	std::cout << "[TEMP] Node ready\n";
}

TemperatureNode::~TemperatureNode() {}

void TemperatureNode::readData() {
	std::ifstream file;
	int rawValue = 0;

	file.open(this->filename, std::ios::in);
	file >> rawValue;
	file.close();
	this->voltageSum += static_cast<float>(rawValue) / coefficient;
	this->currentReadings++;

	if (this->currentReadings == this->readings) {
		this->currentReadings = 0;

		// Divide sum by number of readings and multiply by 100
		// The sensor is 10mV/C, so to convert volts to degrees
		float avgTemperature = (this->voltageSum / this->readings) * 100;
		this->voltageSum = 0.0;
		this->publishData(avgTemperature);
	}
}

void TemperatureNode::publishData(float temperature) {
	auto message = std::make_shared<rys_interfaces::msg::TemperatureStatus>();

	message->header.stamp = this->now();
	message->header.frame_id = "LM35";

	message->temperature = temperature;
	if (message->temperature > this->criticalLevel) {
		this->isCritical = true;
	} else if ((message->temperature + hysteresis) < this->criticalLevel) {
		this->isCritical = false;
	}

	message->temperature_critical = this->isCritical;

	this->publisher->publish(message);
}
