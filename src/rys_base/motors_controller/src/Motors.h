#ifndef _MOTORS_H_
#define _MOTORS_H_

#include <cstdint>
#include <fstream>
#include <mutex>

#define MAX_ACCELERATION 1.0f
#define MAX_MOTOR_SPEED 300000
#define DEVICE_NAME "/dev/rpmsg_pru31"

struct DataFrame {
	uint8_t enabled;
	uint8_t microstep;
	uint8_t directionLeft;
	uint8_t directionRight;
	uint32_t speedLeft;
	uint32_t speedRight;
};

class Motors {
	private:
		bool enabled;
		float speedLeft;
		float speedRight;
		uint8_t microstep;
		int32_t maxMotorSpeed;
		long distance;
		std::ofstream pruFile;
		std::mutex fileAccessMutex;

		void writeDataFrame(const DataFrame & frame);
	public:
		Motors();
		~Motors();
		void initialize();
		void enable();
		void disable();
		void updateOdometry(float);
		// speed from -1.0f to 1.0f
		void setSpeed(float, float, int);
		float getDistance();
		void resetDistance();
		float getSpeedLeft();
		float getSpeedRight();
};

#endif
