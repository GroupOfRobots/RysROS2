#ifndef _IMU_NODE_HPP_
#define _IMU_NODE_HPP_

#include <chrono>
#include <string>

#include "rclcpp/rclcpp.hpp"

#include "sensor_msgs/msg/imu.hpp"
#include "std_msgs/msg/empty.hpp"
#include "./IMU.hpp"

class IMUNode : public rclcpp::Node {
	private:
		IMU * imu;
		bool calibration;
		float calibrationValuesSum;
		unsigned long int calibrationIterations;
		std::chrono::milliseconds calibrationDuration;
		std::chrono::time_point<std::chrono::high_resolution_clock> calibrationEndTime;

		rclcpp::TimerBase::SharedPtr timer;
		rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr imuPublisher;
		rclcpp::Subscription<std_msgs::msg::Empty>::SharedPtr calibrationSubscription;

		void timerCallback();
		void imuCalibrateCallback(const std_msgs::msg::Empty::SharedPtr message);
	public:
		IMUNode(
			const std::string & robotName,
			const std::string & nodeName,
			const std::chrono::milliseconds loopDuration,
			const std::chrono::milliseconds calibrationDuration,
			const int imuCalibrationOffsets[6]
		);
		~IMUNode();
};

#endif
