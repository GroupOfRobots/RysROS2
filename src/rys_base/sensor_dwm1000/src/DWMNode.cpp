#include <iostream>
#include <functional>
#include "rclcpp/rclcpp.hpp"

#include "DWMNode.hpp"

DWMNode::DWMNode(
	const std::string & robotName,
	const std::string & nodeName,
	const bool useIPC,
	std::chrono::milliseconds rate
) : rclcpp::Node(nodeName, robotName, useIPC) {
	std::cout << "[DWM1000] NOT enabling the sensor\n";
	// this->dwm = new DWM(115, 20);
	// this->dwm->initialize(true);

	this->publisher = this->create_publisher<rys_interfaces::msg::LocationDistances>("/" + robotName + "/sensor/dwm1000", rmw_qos_profile_default);
	this->timer = this->create_wall_timer(rate, std::bind(&DWMNode::publishData, this));
	std::cout << "[DWM1000] Node ready\n";
}

DWMNode::~DWMNode() {
	delete this->dwm;
}

void DWMNode::publishData() {
	auto message = rys_interfaces::msg::LocationDistances();

	// TODO: implement ranging once beacons (anchors) work
	// float ranges[4] = this->dwm->readRange();
	message.distance0 = 0.0;
	message.distance1 = 0.0;
	message.distance2 = 0.0;
	message.distance3 = 0.0;
	// std::cout << "DWM1000: publishing distances: " << message.range0 << std::endl;

	this->publisher->publish(message);
}
