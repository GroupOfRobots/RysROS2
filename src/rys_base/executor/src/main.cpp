#include <iostream>
#include <thread>
#include <chrono>
#include <sched.h>
#include <sys/mman.h>
#include <cstring>
#include <mutex>
#include <csignal>
#include <fstream>
#include "MyExecutor.hpp"
#include "IMU.hpp"
#include "helper_3dmath.hpp"
#include "MotorsController.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rys_interfaces/msg/steering.hpp"
#include "rys_interfaces/msg/temperature_status.hpp"
#include "rys_interfaces/msg/battery_status.hpp"
#include "rys_interfaces/msg/regulation_callback.hpp"
#include "sensor_msgs/msg/imu.hpp"
#include "rys_interfaces/srv/set_regulator_settings.hpp"
#include "rys_interfaces/srv/get_regulator_settings.hpp"

bool destruct = false;

struct SteeringData{
    float throttle;
    float rotation;
    int precision;
    bool balancing;
    SteeringData() : throttle(0), rotation(0), precision(32), balancing(false){}
};

struct RegulationCallbackStructure{
    float roll;
    float setRoll;
    float speed;
    float setSpeed;
    RegulationCallbackStructure() : roll(0), setRoll(0), speed(0), setSpeed(0){}
};

void sigintHandler(int signum) {
    if (signum == SIGINT) {
        destruct = true;
    }
}

void setRTPriority() {
    struct sched_param schedulerParams;
    schedulerParams.sched_priority = sched_get_priority_max(SCHED_FIFO)-1;
    std::cout << "Setting RT scheduling, priority " << schedulerParams.sched_priority << std::endl;
    if (sched_setscheduler(0, SCHED_FIFO, &schedulerParams) == -1) {
        std::cout << "WARNING: Setting RT scheduling failed: " << std::strerror(errno) << std::endl;
        return;
    }

    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        std::cout << "WARNING: Failed to lock memory: " << std::strerror(errno) << std::endl;
    }
}

void IMUreader(bool& activate, std::mutex& m, bool& destroy, IMU::ImuData& extData, std::mutex& dm){
    std::string name = "IMUreader";
    pthread_setname_np(pthread_self(), name.c_str());
    int numOfRuns = 0;
    float frequency = 0;
    std::chrono::time_point<std::chrono::high_resolution_clock> previous = std::chrono::high_resolution_clock::now();
    std::chrono::time_point<std::chrono::high_resolution_clock> timeNow = std::chrono::high_resolution_clock::now();
    IMU * imu = new IMU();

    while(!destroy){
        m.lock();
        if(activate){
            activate = false;
            m.unlock();

            IMU::ImuData data;
            int result = -1;
            while(result < 0 && !destroy){
                result = imu->getData(&data);
            }

            dm.lock();
            extData = data;
            dm.unlock();

            numOfRuns++;
            if (numOfRuns > 5999) {
                previous = timeNow;
                timeNow = std::chrono::high_resolution_clock::now();
                auto loopTimeSpan = std::chrono::duration_cast<std::chrono::duration<float>>(timeNow - previous);
                float loopTime = loopTimeSpan.count();
                frequency = numOfRuns/loopTime;
                std::cout << name << " Frequency " << frequency << "Hz after " << numOfRuns << " messages." << std::endl;
                numOfRuns = 0;
            }
        } else {
            m.unlock();
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    delete imu;
    std::cout << name << ": I'm dying.." << std::endl;
}

void BATreader(bool& activate, std::mutex& m, bool& destroy, VectorFloat& v, std::mutex& vm){
    std::string name = "BATreader";
    pthread_setname_np(pthread_self(), name.c_str());
    int numOfRuns = 0;
    float frequency = 0;
    std::chrono::time_point<std::chrono::high_resolution_clock> previous = std::chrono::high_resolution_clock::now();
    std::chrono::time_point<std::chrono::high_resolution_clock> timeNow = std::chrono::high_resolution_clock::now();

    const float coefficients[3] = { 734.4895, 340.7509, 214.1773 };
    const uint8_t inputNumbers[3] = { 3, 1, 6 };
    std::string filenames[3];
    for (int i = 0; i < 3; ++i) {
        filenames[i] = std::string("/sys/devices/platform/ocp/44e0d000.tscadc/TI-am335x-adc/iio:device0/in_voltage") + std::to_string(inputNumbers[i]) + std::string("_raw");
    }
    std::ifstream file;
    float voltages[3];
    int rawValue = 0;
    VectorFloat localVoltage(0, 0, 0);

    while(!destroy){
        m.lock();
        if(activate){
            activate = false;
            m.unlock();

            for (int i = 0; i < 3; ++i) {
                file.open(filenames[i], std::ios::in);
                file >> rawValue;
                file.close();
                voltages[i] = static_cast<float>(rawValue) / coefficients[i];
            }

            localVoltage = VectorFloat(voltages[0], voltages[1]-voltages[0], voltages[2]-voltages[1]);
            vm.lock();
            v = localVoltage;
            vm.unlock();
            if (localVoltage.x < 3.3 || localVoltage.y < 3.3 || localVoltage.z < 3.3){
                std::cout << name << ": Low Voltage Warning: " << localVoltage.x << " " << localVoltage.y << " " << localVoltage.z << std::endl;
                destroy = true;
                continue;
            }

            numOfRuns++;
            if (numOfRuns > 599) {
                previous = timeNow;
                timeNow = std::chrono::high_resolution_clock::now();
                auto loopTimeSpan = std::chrono::duration_cast<std::chrono::duration<float>>(timeNow - previous);
                float loopTime = loopTimeSpan.count();
                frequency = numOfRuns/loopTime;
                std::cout << name << " Frequency " << frequency << "Hz after " << numOfRuns << " messages." << std::endl;
                numOfRuns = 0;
            }
        } else {
            m.unlock();
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << name << ": I'm dying.." << std::endl;
}

void TEMPreader(bool& activate, std::mutex& m, bool& destroy, float& f, std::mutex& fm){
    std::string name = "TEMPreader";
    pthread_setname_np(pthread_self(), name.c_str());
    int numOfRuns = 0;
    float frequency = 0;
    std::chrono::time_point<std::chrono::high_resolution_clock> previous = std::chrono::high_resolution_clock::now();
    std::chrono::time_point<std::chrono::high_resolution_clock> timeNow = std::chrono::high_resolution_clock::now();

    const float coefficient = 564.7637;
    const uint8_t inputNumber = 5;
    std::string filename = std::string("/sys/devices/platform/ocp/44e0d000.tscadc/TI-am335x-adc/iio:device0/in_voltage") + std::to_string(inputNumber) + std::string("_raw");
    std::ifstream file;
    float voltageSum = 0.0;
    float voltage = 0.0;
    int rawValue = 0;
    int currentReadings = 0;

    while(!destroy){
        m.lock();
        if(activate){
            activate = false;
            m.unlock();

            file.open(filename, std::ios::in);
            file >> rawValue;
            file.close();
            voltageSum += static_cast<float>(rawValue) / coefficient;
            currentReadings++;
            if (currentReadings == 5){
                voltage = (voltageSum/5.0)*100.0;
                if (voltage > 60){
                    std::cout << name << ": Critical Temperature Warning: " << voltage << std::endl;
                    destroy = true;
                    continue;
                }
                fm.lock();
                f = voltage;
                fm.unlock();
                currentReadings = 0;
                voltageSum = 0;
            }

            numOfRuns++;
            if (numOfRuns > 599) {
                previous = timeNow;
                timeNow = std::chrono::high_resolution_clock::now();
                auto loopTimeSpan = std::chrono::duration_cast<std::chrono::duration<float>>(timeNow - previous);
                float loopTime = loopTimeSpan.count();
                frequency = numOfRuns/loopTime;
                std::cout << name << " Frequency " << frequency << "Hz after " << numOfRuns << " messages." << std::endl;
                numOfRuns = 0;
            }
        } else {
            m.unlock();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::cout << name << ": I'm dying.." << std::endl;
}

void motorsController(bool& activate, std::mutex& m, bool& destroy, float (&PIDparams)[6], std::mutex& PID_m, 
    IMU::ImuData& imu, std::mutex& imu_m, SteeringData& ster, std::mutex& ster_m, RegulationCallbackStructure& regCall, std::mutex& call_m){
    std::string name = "motors";
    pthread_setname_np(pthread_self(), name.c_str());
    int numOfRuns = 0;
    float frequency = 0;
    std::chrono::time_point<std::chrono::high_resolution_clock> previous = std::chrono::high_resolution_clock::now();
    std::chrono::time_point<std::chrono::high_resolution_clock> timeNow = std::chrono::high_resolution_clock::now();

    std::chrono::time_point<std::chrono::high_resolution_clock> previousRun = std::chrono::high_resolution_clock::now();
    std::chrono::time_point<std::chrono::high_resolution_clock> timeNowRun = std::chrono::high_resolution_clock::now();
    float loopTimeRun;
    float roll = 0;
    float previousRoll = 0;
    float rotationX = 0;
    bool layingDown = false;
    bool standingUp = false;
    int standUpMultiplier;
    bool standUpPhase = false;
    std::chrono::milliseconds standUpTimer;
    std::chrono::milliseconds rate = std::chrono::milliseconds(10);
    float linearSpeed = 0;
    // float leftSpeed = 0;
    // float rightSpeed = 0;

    float rotation = 0;
    float throttle = 0;
    int precision = 32;
    float balancing = false;
    IMU::ImuData localData;


    MotorsController * controller = new MotorsController();
    controller->enableMotors();
    controller->setInvertSpeed(true, false);
    controller->setMotorsSwapped(true);
    controller->setLQREnabled(false);
    controller->setBalancing(balancing);
    controller->setSpeedFilterFactor(1);
    controller->setAngleFilterFactor(1);
    controller->setPIDSpeedRegulatorEnabled(true);

    std::ofstream file;
    file.open("testfile");
    float time = -20000;
    float previousTargetAngle = 0;

    for (int i = 1; i<21 && !destroy; i++){
        std::cout << name << ": " << i << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    while(!destroy){
        m.lock();
        if(activate){
            activate = false;
            m.unlock();

            PID_m.lock();
            // Ku = 10, T = 60ms = 0.06s ===> K = 0.6*10 = 6, InvTi = 2/0.06 = 33.(3), Td  = 0.06/8 = 0.0075
            // initial
            // controller->setPIDParameters(0.0, 0.0, 0.0, 10.0, 0.0, 0.0);
            // working angle PID
            // controller->setPIDParameters(0.0, 0.0, 0.0, 2.0, 20.0, 0);
            //// nope
            // poorly working speed over angle PID
            // controller->setPIDParameters(0.05, 0.0, 0.00, 2.0, 20.0, 0);
            // sth maybe working
            // controller->setPIDParameters(0.1, 0.05, 0.00001, 2.0, 20.0, 0.01);
            ////
            controller->setPIDParameters(PIDparams[0], PIDparams[1], PIDparams[2], PIDparams[3], PIDparams[4], PIDparams[5]);
            PID_m.unlock();

            previousRun = timeNowRun;
            timeNowRun = std::chrono::high_resolution_clock::now();
            auto loopTimeSpanRun = std::chrono::duration_cast<std::chrono::duration<float>>(timeNowRun - previousRun);
            loopTimeRun = loopTimeSpanRun.count();
            // file << loopTimeRun << "\n";

            previousRoll = roll;
            imu_m.lock();
            localData = imu;
            imu_m.unlock();
            roll = atan2(2.0 * (localData.orientationQuaternion[0] * localData.orientationQuaternion[1] + localData.orientationQuaternion[2] * localData.orientationQuaternion[3]),
                1.0 - 2.0 * (localData.orientationQuaternion[1] * localData.orientationQuaternion[1] + localData.orientationQuaternion[2] * localData.orientationQuaternion[2]));
            rotationX = localData.angularVelocity[0];

            ster_m.lock();
            throttle = ster.throttle;
            rotation = ster.rotation;
            precision = ster.precision;
            balancing = ster.balancing;
            ster_m.unlock();
            controller->setBalancing(balancing);

            // leftSpeed = controller->getMotorSpeedLeftRaw();
            // rightSpeed = controller->getMotorSpeedRightRaw();
            // linearSpeed = (leftSpeed + rightSpeed) / 2;
            linearSpeed = (controller->getMotorSpeedLeftRaw() + controller->getMotorSpeedRightRaw()) / 2;

            layingDown = (roll > 1.0 && previousRoll > 1.0) || (roll < -1.0 && previousRoll < -1.0);
            if(layingDown && !standingUp && balancing){
                standingUp = true;
                standUpMultiplier = (roll > 1.0 ? 1 : -1);
                standUpPhase = false;
                standUpTimer = std::chrono::milliseconds(0);
            }

            if (standingUp){
                if (!standUpPhase) {
                    if (standUpTimer < std::chrono::milliseconds(1000)){
                        controller->setMotorSpeeds(0,0,32,true);
                    } else {
                        if (controller->getMotorSpeedLeftRaw() == standUpMultiplier * 1.0 &&
                                controller->getMotorSpeedRightRaw() == standUpMultiplier * 1.0){
                            standUpPhase = true;
                        } else {
                            controller->setMotorSpeeds(standUpMultiplier * 1.0, standUpMultiplier * 1.0, 32, false);
                        }
                    }
                } else {
                    if((standUpMultiplier * roll) <= 0){
                        standingUp = false;
                        controller->setMotorSpeeds(0, 0, 32, true);
                        controller->zeroPIDRegulator();
                    } else if(standUpTimer >= std::chrono::milliseconds(2000) && standUpMultiplier * roll > 1.0) {
                        standUpPhase = false;
                        standUpTimer = std::chrono::milliseconds(0);
                        controller->setMotorSpeeds(0, 0, 32, true);
                    } else {
                        controller->setMotorSpeeds(standUpMultiplier * (-1.0), standUpMultiplier * (-1.0), 32, false);
                    }
                }
                standUpTimer += rate;
            }

            if (!standingUp) {
                float finalLeftSpeed = 0;
                float finalRightSpeed = 0;
                controller->calculateSpeeds(roll, rotationX, linearSpeed, throttle, rotation, finalLeftSpeed, finalRightSpeed, loopTimeRun);
                if (balancing)
                    controller->setMotorSpeeds(finalLeftSpeed, finalRightSpeed, 32, false);
                else
                    controller->setMotorSpeeds(finalLeftSpeed, finalRightSpeed, precision, false);
            }
            call_m.lock();
            regCall.roll = roll;
            controller->getPIDPreviousTargetAngle(regCall.setRoll);
            regCall.speed = linearSpeed;
            regCall.setSpeed = throttle;
            call_m.unlock();
            time += loopTimeRun*1000;
            controller->getPIDPreviousTargetAngle(previousTargetAngle);
            file << time << " " << roll << " " << previousTargetAngle << " " << linearSpeed << " " << throttle << std::endl;
            
            numOfRuns++;
            if (numOfRuns > 5999) {
                previous = timeNow;
                timeNow = std::chrono::high_resolution_clock::now();
                auto loopTimeSpan = std::chrono::duration_cast<std::chrono::duration<float>>(timeNow - previous);
                float loopTime = loopTimeSpan.count();
                frequency = numOfRuns/loopTime;
                std::cout << name << " Frequency " << frequency << "Hz after " << numOfRuns << " messages." << std::endl;
                numOfRuns = 0;
            }
        } else {
            m.unlock();
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    file.close();
    controller->disableMotors();
    delete controller;
    std::cout << name << ": I'm dying.." << std::endl;
}

void remoteComm(bool& activate, std::mutex& m, bool& destroy, float (&PIDparams)[6], std::mutex& PID_m, IMU::ImuData& extData, std::mutex& dm, SteeringData& s, 
    std::mutex& sm, float& temperature, std::mutex& tm, VectorFloat& battery, std::mutex& bm, RegulationCallbackStructure& regCall, std::mutex& call_m){
    std::string name = "remoteComm";
    pthread_setname_np(pthread_self(), name.c_str());
    int numOfRuns = 0;
    float frequency = 0;
    std::chrono::time_point<std::chrono::high_resolution_clock> previous = std::chrono::high_resolution_clock::now();
    std::chrono::time_point<std::chrono::high_resolution_clock> timeNow = std::chrono::high_resolution_clock::now();

    float throttle = 0;
    float rotation = 0;
    int precision = 32;
    bool balancing = false;

    float newSteering = false;
    sm.lock();
    s.throttle = throttle;
    s.rotation = rotation;
    s.precision = precision;
    s.balancing = balancing;
    sm.unlock();
    const std::string robotName = "rys";
    const std::string nodeName = "remoteComm";
    const rmw_qos_profile_t qos = {
        RMW_QOS_POLICY_HISTORY_KEEP_LAST,
        1,
        RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT,
        RMW_QOS_POLICY_DURABILITY_VOLATILE,
        false
    };

    auto node = rclcpp::Node::make_shared(nodeName, robotName, false);
    auto steeringCallback =
        [&](const rys_interfaces::msg::Steering::SharedPtr message){
            throttle = message->throttle;
            rotation = message->rotation;
            precision = message->precision;
            balancing = message->balancing;
            newSteering = true;
        };
    auto sub = node->create_subscription<rys_interfaces::msg::Steering>("/" + robotName + "/control/steering", steeringCallback, qos);
    // auto pubImu = node->create_publisher<sensor_msgs::msg::Imu>("/" + robotName + "/sensor/imuInfrequent", qos);
    auto pubTemp = node->create_publisher<rys_interfaces::msg::TemperatureStatus>("/" + robotName + "/sensor/temperature", qos);
    auto pubBat = node->create_publisher<rys_interfaces::msg::BatteryStatus>("/" + robotName + "/sensor/battery", qos);
    auto pubReg = node->create_publisher<rys_interfaces::msg::RegulationCallback>("/" + robotName + "/control/regulation", qos);
    auto GetRegulatorSettingsCallback = [&](const std::shared_ptr<rmw_request_id_t> requestHeader,
                                            const std::shared_ptr<rys_interfaces::srv::GetRegulatorSettings::Request> request,
                                            std::shared_ptr<rys_interfaces::srv::GetRegulatorSettings::Response> response){
        (void) requestHeader;
        (void) request;

        response->speed_filter_factor = 1.0;
        response->angle_filter_factor = 1.0;
        response->lqr_enabled = false;
        response->pid_speed_regulator_enabled = true;
        response->lqr_linear_velocity_k = 0.0;
        response->lqr_angular_velocity_k = 0.0;
        response->lqr_angle_k = 0.0;
        PID_m.lock();
        response->pid_speed_kp = PIDparams[0];
        response->pid_speed_ki = PIDparams[1];
        response->pid_speed_kd = PIDparams[2];
        response->pid_angle_kp = PIDparams[3];
        response->pid_angle_ki = PIDparams[4];
        response->pid_angle_kd = PIDparams[5];
        PID_m.unlock();
    };
    auto getRegulatorSettingsServer = node->create_service<rys_interfaces::srv::GetRegulatorSettings>("/" + robotName + "/control/regulator_settings/get", GetRegulatorSettingsCallback);
    auto SetRegulatorSettingsCallback = [&](const std::shared_ptr<rmw_request_id_t> requestHeader,
                                            const std::shared_ptr<rys_interfaces::srv::SetRegulatorSettings::Request> request,
                                            std::shared_ptr<rys_interfaces::srv::SetRegulatorSettings::Response> response){
        (void) requestHeader;

        PID_m.lock();
        PIDparams[0] = request->pid_speed_kp;
        PIDparams[1] = request->pid_speed_ki;
        PIDparams[2] = request->pid_speed_kd;
        PIDparams[3] = request->pid_angle_kp;
        PIDparams[4] = request->pid_angle_ki;
        PIDparams[5] = request->pid_angle_kd;
        PID_m.unlock();
        response->success = true;
        response->error_text = std::string("success");
    };
    auto setRegulatorSettingsServer = node->create_service<rys_interfaces::srv::SetRegulatorSettings>("/" + robotName + "/control/regulator_settings/set", SetRegulatorSettingsCallback);

    // IMU::ImuData localData;
    // auto message = std::make_shared<sensor_msgs::msg::Imu>();
    // message->header.frame_id = "MPU6050";
    // for (int i = 0; i < 9; ++i) {
    //     message->orientation_covariance[i] = 0;
    //     message->angular_velocity_covariance[i] = 0;
    //     message->linear_acceleration_covariance[i] = 0;
    // }

    float localTemperature = 0;
    auto messageTemp = std::make_shared<rys_interfaces::msg::TemperatureStatus>();
    messageTemp->header.frame_id = "LM35";
    messageTemp->temperature_critical = false;

    VectorFloat localBattery(0, 0, 0);
    auto messageBat = std::make_shared<rys_interfaces::msg::BatteryStatus>();
    messageBat->header.frame_id = "ADC";
    messageBat->voltage_low = false;

    RegulationCallbackStructure localStruct;
    auto messageReg = std::make_shared<rys_interfaces::msg::RegulationCallback>();
    messageReg->header.frame_id = "PID";

    while(!destroy){
        m.lock();
        if(activate){
            activate = false;
            m.unlock();

            // dm.lock();
            // localData = extData;
            // dm.unlock();

            // message->header.stamp = node->now();
            // message->orientation.x = localData.orientationQuaternion[1];
            // message->orientation.y = localData.orientationQuaternion[2];
            // message->orientation.z = localData.orientationQuaternion[3];
            // message->orientation.w = localData.orientationQuaternion[0];
            // message->angular_velocity.x = localData.angularVelocity[0];
            // message->angular_velocity.y = localData.angularVelocity[1];
            // message->angular_velocity.z = localData.angularVelocity[2];
            // message->linear_acceleration.x = localData.linearAcceleration[0];
            // message->linear_acceleration.y = localData.linearAcceleration[1];
            // message->linear_acceleration.z = localData.linearAcceleration[2];

            // pubImu->publish(message);

            tm.lock();
            localTemperature = temperature;
            tm.unlock();

            messageTemp->header.stamp = node->now();
            messageTemp->temperature = localTemperature;

            pubTemp->publish(messageTemp);

            bm.lock();
            localBattery = battery;
            bm.unlock();

            messageBat->header.stamp = node->now();
            messageBat->voltage_cell1 = localBattery.x;
            messageBat->voltage_cell2 = localBattery.y;
            messageBat->voltage_cell3 = localBattery.z;

            pubBat->publish(messageBat);

            call_m.lock();
            localStruct = regCall;
            call_m.unlock();

            messageReg->header.stamp = node->now();
            messageReg->roll = localStruct.roll;
            messageReg->set_roll = localStruct.setRoll;
            messageReg->speed = localStruct.speed;
            messageReg->set_speed = localStruct.setSpeed;

            pubReg->publish(messageReg);

            rclcpp::spin_some(node);

            if(newSteering){
                sm.lock();
                s.throttle = throttle;
                s.rotation = rotation;
                s.precision = precision;
                s.balancing = balancing;
                sm.unlock();
                newSteering = false;
            }

            numOfRuns++;
            if (numOfRuns > 1199) {
                previous = timeNow;
                timeNow = std::chrono::high_resolution_clock::now();
                auto loopTimeSpan = std::chrono::duration_cast<std::chrono::duration<float>>(timeNow - previous);
                float loopTime = loopTimeSpan.count();
                frequency = numOfRuns/loopTime;
                std::cout << name << " Frequency " << frequency << "Hz after " << numOfRuns << " messages." << std::endl;
                numOfRuns = 0;
            }

        } else {
            m.unlock();
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    rclcpp::shutdown();
    std::cout << name << ": I'm dying.." << std::endl;
}

int main(int argc, char * argv[]){
    std::string name = "main";
    pthread_setname_np(pthread_self(), name.c_str());
    signal(SIGINT, sigintHandler);
    std::cout << "Initializing ROS...\n";
    rclcpp::init(argc, argv);
    std::cout << "ROS initialized.\n";
    setRTPriority();

    MyExecutor *exec = new MyExecutor(std::ref(destruct));

    VectorFloat batteryStatus(0, 0, 0);
    std::mutex b_mutex;
    bool BATreader_bool = false;
    std::mutex BATreader_mutex;
    std::thread t1(BATreader, std::ref(BATreader_bool), std::ref(BATreader_mutex), std::ref(destruct),
                    std::ref(batteryStatus), std::ref(b_mutex));

    exec->addExec(std::ref(BATreader_mutex), std::ref(BATreader_bool), std::chrono::milliseconds(1000));

    float temperature = 0;
    std::mutex t_mutex;
    bool TEMPreader_bool = false;
    std::mutex TEMPreader_mutex;
    std::thread t2(TEMPreader, std::ref(TEMPreader_bool), std::ref(TEMPreader_mutex), std::ref(destruct),
                    std::ref(temperature), std::ref(t_mutex));

    exec->addExec(std::ref(TEMPreader_mutex), std::ref(TEMPreader_bool), std::chrono::milliseconds(200));

    IMU::ImuData imuData;
    std::mutex imuData_mutex;
    bool IMUreader_bool = false;
    std::mutex IMUreader_mutex;
    std::thread t3(IMUreader, std::ref(IMUreader_bool), std::ref(IMUreader_mutex), std::ref(destruct),
                    std::ref(imuData), std::ref(imuData_mutex));

    exec->addExec(std::ref(IMUreader_mutex), std::ref(IMUreader_bool), std::chrono::milliseconds(10));

    float PIDparams[6] = {0.2, 1, 0.005, 2.0, 10.0, 0};
    // float PIDparams[6] = {0.1, 40, 0.001, 3.0, 20.0, 0};
    // float PIDparams[6] = {0.01, 100, 0.1, 3.0, 20.0, 0};
    // float PIDparams[6] = {0.05, 0.05, 0.0001, 2.0, 10.0, 0};
    // float PIDparams[6] = {0.5, 0.00001, 0.002, 2.0, 20.0, 0};
    if (argc == 8){
        if (!std::strcmp(argv[1], "-p")) {
            std::cout << "Reading custom PID parameters..." << std::endl;
            PIDparams[0] = atof(argv[2]);
            PIDparams[1] = atof(argv[3]);
            PIDparams[2] = atof(argv[4]);
            PIDparams[3] = atof(argv[5]);
            PIDparams[4] = atof(argv[6]);
            PIDparams[5] = atof(argv[7]);
        }
    }
    bool motors_bool = false;
    std::mutex motors_mutex;
    SteeringData steering_data;
    std::mutex steering_data_mutex;
    RegulationCallbackStructure regCall;
    std::mutex call_m;
    std::mutex PID_m;
    std::thread t4(motorsController, std::ref(motors_bool), std::ref(motors_mutex), std::ref(destruct),
                    std::ref(PIDparams), std::ref(PID_m),
                    std::ref(imuData), std::ref(imuData_mutex),
                    std::ref(steering_data), std::ref(steering_data_mutex),
                    std::ref(regCall), std::ref(call_m));

    exec->addExec(std::ref(motors_mutex), std::ref(motors_bool), std::chrono::milliseconds(10));

    bool remoteComm_bool = false;
    std::mutex remoteComm_mutex;
    std::thread t5(remoteComm, std::ref(remoteComm_bool), std::ref(remoteComm_mutex), std::ref(destruct),
                    std::ref(PIDparams), std::ref(PID_m),
                    std::ref(imuData), std::ref(imuData_mutex),
                    std::ref(steering_data), std::ref(steering_data_mutex),
                    std::ref(temperature), std::ref(t_mutex),
                    std::ref(batteryStatus), std::ref(b_mutex),
                    std::ref(regCall), std::ref(call_m));

    exec->addExec(std::ref(remoteComm_mutex), std::ref(remoteComm_bool), std::chrono::milliseconds(50));

    exec->list();
    exec->spin();
    t1.join();
    t2.join();
    t3.join();
    t4.join();
    t5.join();
    delete exec;
}