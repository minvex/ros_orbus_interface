/*
 * File:   UNAVHardware.h
 * Author: raffaello
 *
 * Created on 15 November 2013, 18:34
 */

#ifndef UNAVHARDWARE_H
#define	UNAVHARDWARE_H

#include "ORBHardware.h"

#include <urdf/model.h>

#include "hardware_interface/joint_state_interface.h"
#include "hardware_interface/joint_command_interface.h"
#include <joint_limits_interface/joint_limits_interface.h>

#include <joint_limits_interface/joint_limits_urdf.h>
#include <joint_limits_interface/joint_limits_rosparam.h>

#include "configurator/MotorPIDConfigurator.h"
#include "configurator/MotorParamConfigurator.h"
#include "configurator/MotorEmergencyConfigurator.h"

#define NUM_MOTORS 2

class UNAVHardware : public ORBHardware {
public:
    UNAVHardware(const ros::NodeHandle& nh, const ros::NodeHandle& private_nh, ParserPacket* serial);
    virtual ~UNAVHardware();

    void updateJointsFromHardware();
    void writeCommandsToHardware(ros::Duration period);

private:
    /// URDF information about robot
    boost::shared_ptr<urdf::ModelInterface> urdf_;
    /// Decode a motor command
    motor_command_map_t motor_command_;
    /// List to send messages to serial
    std::vector<packet_information_t> list_send_;

    /// ROS Control interfaces
    hardware_interface::JointStateInterface joint_state_interface_;
    hardware_interface::VelocityJointInterface velocity_joint_interface_;
    /// ROS joint limits interface
    joint_limits_interface::VelocityJointSoftLimitsInterface vel_limits_interface_;

    /// Register all control interface and joint limit interface
    void registerControlInterfaces();

    /// Setup all limits
    void setupLimits(hardware_interface::JointHandle joint_handle, ros::V_string joint_names, int i);

    void motorPacket(const unsigned char& command, const message_abstract_u* packet);
    void addParameter(std::vector<packet_information_t>* list_send);

    /**
    * Joint structure that is hooked to ros_control's InterfaceManager, to allow control via diff_drive_controller
    */
    struct Joint
    {
      MotorPIDConfigurator *configurator_pid;
      MotorParamConfigurator *configurator_param;
      MotorEmergencyConfigurator *configurator_emergency;
      // Actual state
      motor_state_t state;

      double position;
      double position_offset;
      double velocity;
      double effort;
      double velocity_command;

      Joint() : position(0), velocity(0), effort(0), velocity_command(0) { }
    } joints_[NUM_MOTORS];

};

#endif	/* UNAVHARDWARE_H */

