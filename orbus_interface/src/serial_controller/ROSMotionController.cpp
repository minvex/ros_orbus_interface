/* 
 * File:   ROSMotionController.cpp
 * Author: Raffaello Bonghi
 * 
 * Created on 15 November 2013, 18:34
 */

#include "serial_controller/ROSMotionController.h"
#include <limits>

#define NUMBER_PUB 10
#define SGN(x)  ( ((x) < 0) ?  -1 : ( ((x) == 0 ) ? 0 : 1) )

using namespace std;

ROSMotionController::ROSMotionController(const ros::NodeHandle& nh, ParserPacket* serial)
: ROSController(nh, serial), positon_joint_left(0), positon_joint_right(0) {

    string param_type_board = "Motor Control";
    if (type_board.compare(param_type_board) == 0) {
        nh_.setParam("info/type_board", type_board);
    } else {
        throw (controller_exception("Other board: " + type_board));
    }

    serial->addCallback(&ROSMotionController::motionPacket, this, HASHMAP_MOTION);
    addVectorPacketRequest(&ROSMotionController::updatePacket, this);
    addParameterPacketRequest(&ROSMotionController::addParameter, this);
    addTimerEvent(&ROSMotionController::timerEvent, this);
    addAliveOperation(&ROSMotionController::aliveOperation, this);

    //Open Publisher
    //- Command receive
    pub_pose = nh_.advertise<ros_serial_bridge::Pose>("pose", NUMBER_PUB,
            boost::bind(&ROSController::connectCallback, this, _1));
    pub_enable = nh_.advertise<ros_serial_bridge::Enable>("enable", NUMBER_PUB,
            boost::bind(&ROSController::connectCallback, this, _1));
    pub_motor_left = nh_.advertise<ros_serial_bridge::Motor>("motor/" + left_string, NUMBER_PUB,
            boost::bind(&ROSController::connectCallback, this, _1));
    pub_motor_right = nh_.advertise<ros_serial_bridge::Motor>("motor/" + right_string, NUMBER_PUB,
            boost::bind(&ROSController::connectCallback, this, _1));
    //-- Conventional (Using TF, NAV)
    pub_twist = nh_.advertise<geometry_msgs::Twist>("velocity", NUMBER_PUB,
            boost::bind(&ROSController::connectCallback, this, _1));
    pub_odom = nh_.advertise<nav_msgs::Odometry>("odom", NUMBER_PUB,
            boost::bind(&ROSController::connectCallback, this, _1));
    //JointState position
    pub_joint = nh_.advertise<sensor_msgs::JointState>(joint_string, 1,
            boost::bind(&ROSController::connectCallback, this, _1));

    //Open Subscriber
    //- Command
    sub_pose = nh_.subscribe("cmd_pose", 1, &ROSMotionController::poseCallback, this);
    sub_enable = nh_.subscribe("cmd_enable", 1, &ROSMotionController::enableCallback, this);
    //-- Conventional (Using TF, NAV)
    sub_pose_estimate = nh_.subscribe("cmd_odom", 1, &ROSMotionController::poseTFCallback, this);
    sub_twist = nh_.subscribe("cmd_vel", 1, &ROSMotionController::twistCallback, this);

    //Open Service
    srv_pid = nh_.advertiseService("pid", &ROSMotionController::pidServiceCallback, this);
    srv_parameter = nh_.advertiseService("parameter", &ROSMotionController::parameterServiceCallback, this);
    srv_constraint = nh_.advertiseService("constraint", &ROSMotionController::constraintServiceCallback, this);
    srv_emergency = nh_.advertiseService("emergency", &ROSMotionController::emergencyServiceCallback, this);

    status[0] = STATE_CONTROL_DISABLE;
    status[1] = STATE_CONTROL_DISABLE;
}

ROSMotionController::~ROSMotionController() {
    serial_->clearCallback(HASHMAP_MOTION);
    clearVectorPacketRequest();
    clearParameterPacketRequest();
    clearTimerEvent();
    clearAliveOperation();
}

void ROSMotionController::addParameter(std::vector<information_packet_t>* list_send) {
    //Constraint/Left
    ROS_INFO("%s/constraint/%s",joint_string.c_str(), left_string.c_str());
    if (nh_.hasParam(joint_string + "/constraint/" + left_string)) {
        ROS_INFO("Sync parameter constraint/%s: ROS -> ROBOT", left_string.c_str());
        motor_t constraint = get_constraint(left_string);
        list_send->push_back(serial_->createDataPacket(CONSTRAINT_L, HASHMAP_MOTION, (abstract_message_u*) & constraint));
    } else {
        ROS_INFO("Sync parameter constraint/%s: ROBOT -> ROS", left_string.c_str());
        list_send->push_back(serial_->createPacket(CONSTRAINT_L, REQUEST, HASHMAP_MOTION));
    }
    //Constraint/Right
    ROS_INFO("%s/constraint/%s",joint_string.c_str(), right_string.c_str());
    if (nh_.hasParam(joint_string + "/constraint/" + right_string)) {
        ROS_INFO("Sync parameter constraint/%s: ROS -> ROBOT", right_string.c_str());
        motor_t constraint = get_constraint(right_string);
        list_send->push_back(serial_->createDataPacket(CONSTRAINT_R, HASHMAP_MOTION, (abstract_message_u*) & constraint));
    } else {
        ROS_INFO("Sync parameter constraint/%s: ROBOT -> ROS", right_string.c_str());
        list_send->push_back(serial_->createPacket(CONSTRAINT_R, REQUEST, HASHMAP_MOTION));
    }
    //Load PID/Left
    if (nh_.hasParam("pid/" + left_string)) {
        ROS_INFO("Sync parameter pid/%s: ROS -> ROBOT", left_string.c_str());
        pid_control_t pid_l = get_pid(left_string);
        list_send->push_back(serial_->createDataPacket(PID_CONTROL_L, HASHMAP_MOTION, (abstract_message_u*) & pid_l));
    } else {
        ROS_INFO("Sync parameter pid/%s: ROBOT -> ROS", left_string.c_str());
        list_send->push_back(serial_->createPacket(PID_CONTROL_L, REQUEST, HASHMAP_MOTION));
    }
    //Load PID/Right
    if (nh_.hasParam("pid/" + right_string)) {
        ROS_INFO("Sync parameter pid/%s: ROS -> ROBOT", right_string.c_str());
        pid_control_t pid_r = get_pid(right_string);
        list_send->push_back(serial_->createDataPacket(PID_CONTROL_R, HASHMAP_MOTION, (abstract_message_u*) & pid_r));
    } else {
        ROS_INFO("Sync parameter pid/%s: ROBOT -> ROS", right_string.c_str());
        list_send->push_back(serial_->createPacket(PID_CONTROL_R, REQUEST, HASHMAP_MOTION));
    }
    //Parameter unicycle
    if (nh_.hasParam("structure")) {
        ROS_INFO("Sync parameter structure: ROS -> ROBOT");
        parameter_unicycle_t parameter_unicycle = get_unicycle_parameter();
        list_send->push_back(serial_->createDataPacket(PARAMETER_UNICYCLE, HASHMAP_MOTION, (abstract_message_u*) & parameter_unicycle));
    } else {
        ROS_INFO("Sync parameter structure: ROBOT -> ROS");
        list_send->push_back(serial_->createPacket(PARAMETER_UNICYCLE, REQUEST, HASHMAP_MOTION));
    }
    //Parameter motors LEFT
    if (nh_.hasParam(joint_string + "/" + left_string)) {
        ROS_INFO("Sync parameter parameter motor/%s: ROS -> ROBOT",left_string.c_str());
        parameter_motor_t parameter_motor = get_motor_parameter(left_string);
        list_send->push_back(serial_->createDataPacket(PARAMETER_MOTOR_L, HASHMAP_MOTION, (abstract_message_u*) & parameter_motor));
    } else {
        ROS_INFO("Sync parameter parameter motor/%s: ROBOT -> ROS",left_string.c_str());
        list_send->push_back(serial_->createPacket(PARAMETER_MOTOR_L, REQUEST, HASHMAP_MOTION));
    }
    //Parameter motors RIGHT
    if (nh_.hasParam(joint_string + "/" + right_string)) {
        ROS_INFO("Sync parameter parameter motor/%s: ROS -> ROBOT",right_string.c_str());
        parameter_motor_t parameter_motor = get_motor_parameter(right_string);
        list_send->push_back(serial_->createDataPacket(PARAMETER_MOTOR_R, HASHMAP_MOTION, (abstract_message_u*) & parameter_motor));
    } else {
        ROS_INFO("Sync parameter parameter motor/%s: ROBOT -> ROS",right_string.c_str());
        list_send->push_back(serial_->createPacket(PARAMETER_MOTOR_R, REQUEST, HASHMAP_MOTION));
    }
    //Names TF
    if (nh_.hasParam(tf_string)) {
        ROS_INFO("Sync parameter %s: load", tf_string.c_str());
        nh_.param<std::string>(tf_string + "/" + odometry_string, tf_odometry_string_, tf_odometry_string_);
        nh_.param<std::string>(tf_string + "/" + base_link_string, tf_base_link_string_, tf_base_link_string_);
        nh_.param<std::string>(tf_string + "/" + joint_string, tf_joint_string_, tf_joint_string_);
    } else {
        ROS_INFO("Sync parameter %s: set", tf_string.c_str());
        tf_odometry_string_ = odometry_string;
        tf_base_link_string_ = base_link_string;
        tf_joint_string_ = joint_string;
        nh_.setParam(tf_string + "/" + odometry_string, tf_odometry_string_);
        nh_.setParam(tf_string + "/" + base_link_string, tf_base_link_string_);
        nh_.setParam(tf_string + "/" + joint_string, tf_joint_string_);
    }
    //Names ele
    if (!nh_.hasParam(joint_string + "/back_emf")) {
        ROS_INFO("Sync parameter %s/back_emf: set", joint_string.c_str());
        nh_.setParam(joint_string + "/back_emf/" + left_string, 1.0);
        nh_.setParam(joint_string + "/back_emf/" + right_string, 1.0);
    }
    //Set timer rate
    if (nh_.hasParam(emergency_string)) {
        ROS_INFO("Sync parameter %s: ROS -> ROBOT", emergency_string.c_str());
        emergency_t emergency = get_emergency();
        list_send->push_back(serial_->createDataPacket(EMERGENCY, HASHMAP_MOTION, (abstract_message_u*) & emergency));
    } else {
        ROS_INFO("Sync parameter %s: ROBOT -> ROS", emergency_string.c_str());
        list_send->push_back(serial_->createPacket(EMERGENCY, REQUEST, HASHMAP_MOTION));
    }
    joint.header.frame_id = tf_joint_string_;
    joint.name.resize(2);
    joint.effort.resize(2);
    joint.velocity.resize(2);
    joint.position.resize(2);
    joint.name[0] = left_string;
    joint.name[1] = right_string;
}

void ROSMotionController::motionPacket(const unsigned char& command, const abstract_message_u* packet) {
    switch (command) {
        case CONSTRAINT_L:
            nh_.setParam(joint_string + "/constraint/" + left_string + "/velocity", packet->motor.velocity);
            break;
        case CONSTRAINT_R:
            nh_.setParam(joint_string + "/constraint/" + right_string + "/velocity", packet->motor.velocity);
            break;
        case PARAMETER_UNICYCLE:
            nh_.setParam("structure/" + wheelbase_string, packet->parameter_unicycle.wheelbase);
            nh_.setParam("structure/" + radius_string + "/" + right_string, packet->parameter_unicycle.radius_r);
            nh_.setParam("structure/" + radius_string + "/" + left_string, packet->parameter_unicycle.radius_l);
            nh_.setParam("odo_mis_step", packet->parameter_unicycle.sp_min);
            break;
    case PARAMETER_MOTOR_L:
        nh_.setParam(joint_string + "/" + left_string + "/cpr", packet->parameter_motor.cpr);
        nh_.setParam(joint_string + "/" + left_string + "/ratio", packet->parameter_motor.ratio);
        nh_.setParam(joint_string + "/" + left_string + "/encoder_pos", packet->parameter_motor.encoder_pos);
        nh_.setParam(joint_string + "/" + left_string + "/volt_bridge", packet->parameter_motor.volt_bridge);
        nh_.setParam(joint_string + "/" + left_string + "/encoder_swap", packet->parameter_motor.versus);
        nh_.setParam(joint_string + "/" + left_string + "/default_enable", packet->parameter_motor.enable_set);
        break;
    case PARAMETER_MOTOR_R:
        nh_.setParam(joint_string + "/" + right_string + "/cpr", packet->parameter_motor.cpr);
        nh_.setParam(joint_string + "/" + right_string + "/ratio", packet->parameter_motor.ratio);
        nh_.setParam(joint_string + "/" + right_string + "/encoder_pos", packet->parameter_motor.encoder_pos);
        nh_.setParam(joint_string + "/" + right_string + "/volt_bridge", packet->parameter_motor.volt_bridge);
        nh_.setParam(joint_string + "/" + right_string + "/encoder_swap", packet->parameter_motor.versus);
        nh_.setParam(joint_string + "/" + right_string + "/default_enable", packet->parameter_motor.enable_set);
        break;
        case EMERGENCY:
            nh_.setParam(emergency_string + "/bridge_off", packet->emergency.bridge_off);
            nh_.setParam(emergency_string + "/slope_time", packet->emergency.slope_time);
            nh_.setParam(emergency_string + "/timeout", ((double) packet->emergency.timeout) / 1000.0);
        break;
        case PID_CONTROL_L:
            name_pid = "pid/" + left_string + "/";
            nh_.setParam(name_pid + "P", packet->pid.kp);
            nh_.setParam(name_pid + "I", packet->pid.ki);
            nh_.setParam(name_pid + "D", packet->pid.kd);
            break;
        case PID_CONTROL_R:
            name_pid = "pid/" + right_string + "/";
            nh_.setParam(name_pid + "P", packet->pid.kp);
            nh_.setParam(name_pid + "I", packet->pid.ki);
            nh_.setParam(name_pid + "D", packet->pid.kd);
            break;
        case VEL_MOTOR_MIS_L:

            break;
        case VEL_MOTOR_MIS_R:

            break;
    case MOTOR_L:
        measure[REF_MOTOR_LEFT] = packet->motor;
        //pub_motor_left.publish(motor_left);
        break;
    case MOTOR_R:
        measure[REF_MOTOR_RIGHT] = packet->motor;
        //pub_motor_right.publish(motor_right);
        break;
        case COORDINATE:
            pose.x = packet->coordinate.x;
            pose.y = packet->coordinate.y;
            pose.theta = packet->coordinate.theta;
            pose.space = packet->coordinate.space;
            pub_pose.publish(pose);
            break;
        case VELOCITY:
            twist.linear.x = packet->velocity.v;
            twist.angular.z = packet->velocity.w;
            pub_twist.publish(twist);
            break;
        case VELOCITY_MIS:
            meas_velocity = packet->velocity;
            break;
        case ENABLE:
            enable_motors.enable = packet->enable;
            pub_enable.publish(enable_motors);
            break;
    }
}

void ROSMotionController::timerEvent(const ros::TimerEvent& event) {
    // Send Odometry message
    if (pub_odom.getNumSubscribers() >= 1) {
        sendOdometry(&meas_velocity, &pose);
    }
    // Send JointState message
    if (pub_joint.getNumSubscribers() >= 1)
    {
        sendJointState(&motor_left, &motor_right);
    }
}

bool ROSMotionController::aliveOperation(const ros::TimerEvent& event, std::vector<information_packet_t>* list_send) {
    if (sub_twist.getNumPublishers() >= 1) {
        list_send->push_back(serial_->createDataPacket(VEL_MOTOR_L, HASHMAP_MOTION, (abstract_message_u*) & velocity_ref[0]));
        list_send->push_back(serial_->createDataPacket(VEL_MOTOR_R, HASHMAP_MOTION, (abstract_message_u*) & velocity_ref[1]));
        return true;
    } else {
        if (status[0] != STATE_CONTROL_EMERGENCY || status[1] != STATE_CONTROL_EMERGENCY) {
            status[0] = STATE_CONTROL_EMERGENCY;
            status[1] = STATE_CONTROL_EMERGENCY;
            list_send->push_back(serial_->createDataPacket(ENABLE_MOTOR_L, HASHMAP_MOTION, (abstract_message_u*) & status[0]));
            list_send->push_back(serial_->createDataPacket(ENABLE_MOTOR_R, HASHMAP_MOTION, (abstract_message_u*) & status[1]));
            return true;
        } else {
            return false;
        }
    }
}

void ROSMotionController::updatePacket(std::vector<information_packet_t>* list_send) {
    std::string packet_string;
    if ((pub_pose.getNumSubscribers() >= 1) || (pub_odom.getNumSubscribers() >= 1)) {
        packet_string += "Odo ";
        list_send->push_back(serial_->createPacket(COORDINATE, REQUEST, HASHMAP_MOTION));
    }
    if (pub_twist.getNumSubscribers() >= 1) {
        packet_string += "Vel ";
        list_send->push_back(serial_->createPacket(VELOCITY, REQUEST, HASHMAP_MOTION));
    }
    if (pub_enable.getNumSubscribers() >= 1) {
        packet_string += "Ena ";
        list_send->push_back(serial_->createPacket(ENABLE, REQUEST, HASHMAP_MOTION));
    }
    if (pub_odom.getNumSubscribers() >= 1) {
        packet_string += "VelMis ";
        list_send->push_back(serial_->createPacket(VELOCITY_MIS, REQUEST, HASHMAP_MOTION));
    }
    if ((pub_motor_left.getNumSubscribers() >= 1) || (pub_joint.getNumSubscribers() >= 1)) {
        packet_string += "MotL ";
        list_send->push_back(serial_->createPacket(MOTOR_L, REQUEST, HASHMAP_MOTION));
    }
    if ((pub_motor_right.getNumSubscribers() >= 1) || (pub_joint.getNumSubscribers() >= 1)) {
        packet_string += "MotR ";
        list_send->push_back(serial_->createPacket(MOTOR_R, REQUEST, HASHMAP_MOTION));
    }
    //    ROS_INFO("[ %s]", packet_string.c_str());
}

void ROSMotionController::ConverToMotorVelocity(const geometry_msgs::Twist* msg, motor_control_t *motor_ref) {
    parameter_unicycle_t parameter_unicycle = get_unicycle_parameter();
    // wl = 1/rl [ 1, -d/2]
    // wr = 1/rr [ 1,  d/2]
    long int motor_ref_long[2];
    //Left motor
    motor_ref_long[0] = (long int) ((1.0f / parameter_unicycle.radius_l)*(msg->linear.x - (0.5f*parameter_unicycle.wheelbase * (msg->angular.z)))*1000);
    //Right Motor
    motor_ref_long[1] = (long int) ((1.0f / parameter_unicycle.radius_r)*(msg->linear.x + (0.5f*parameter_unicycle.wheelbase * (msg->angular.z)))*1000);

    // >>>>> Saturation on 16 bit values
    if(motor_ref_long[0] > 32767) {
        motor_ref[0] = 32767;
    } else if (motor_ref_long[0] < -32768) {
        motor_ref_long[0] = -32768;
    } else {
        motor_ref[0] = (int16_t) motor_ref_long[0];
    }

    if(motor_ref_long[1] > 32767) {
        motor_ref[1] = 32767;
    } else if (motor_ref_long[1] < -32768) {
        motor_ref_long[1] = -32768;
    } else {
        motor_ref[1] = (int16_t) motor_ref_long[1];
    }
    // <<<<< Saturation on 16 bit values

}

void ROSMotionController::twistCallback(const geometry_msgs::Twist::ConstPtr &msg) {
    vector<information_packet_t> list_send;
    ConverToMotorVelocity(msg.get(), velocity_ref);
    list_send.push_back(serial_->createDataPacket(VEL_MOTOR_L, HASHMAP_MOTION, (abstract_message_u*) & velocity_ref[0]));
    list_send.push_back(serial_->createDataPacket(VEL_MOTOR_R, HASHMAP_MOTION, (abstract_message_u*) & velocity_ref[1]));
    if (status[0] != STATE_CONTROL_VELOCITY || status[1] != STATE_CONTROL_VELOCITY) {
        status[0] = STATE_CONTROL_VELOCITY;
        status[1] = STATE_CONTROL_VELOCITY;
        list_send.push_back(serial_->createDataPacket(ENABLE_MOTOR_L, HASHMAP_MOTION, (abstract_message_u*) & status[0]));
        list_send.push_back(serial_->createDataPacket(ENABLE_MOTOR_R, HASHMAP_MOTION, (abstract_message_u*) & status[1]));
    }
    list_send.push_back(serial_->createDataPacket(VELOCITY, HASHMAP_MOTION, (abstract_message_u*) & velocity_ref));
    try {
        serial_->parserSendPacket(list_send, 3, boost::posix_time::millisec(200));
        timer_.start();
    } catch (exception &e) {
        ROS_ERROR("%s", e.what());
    }
}

void ROSMotionController::enableCallback(const ros_serial_bridge::Enable::ConstPtr &msg) {
    vector<information_packet_t> list_send;
    state_controller_t enable = msg->enable;
    status[0] = msg->enable;
    status[1] = msg->enable;
    list_send.push_back(serial_->createDataPacket(ENABLE_MOTOR_L, HASHMAP_MOTION, (abstract_message_u*) & status[0]));
    list_send.push_back(serial_->createDataPacket(ENABLE_MOTOR_R, HASHMAP_MOTION, (abstract_message_u*) & status[1]));
    try {
        serial_->parserSendPacket(list_send, 3, boost::posix_time::millisec(200));
    } catch (exception &e) {
        ROS_ERROR("%s", e.what());
    }
}

void ROSMotionController::poseCallback(const ros_serial_bridge::Pose::ConstPtr &msg) {
    saveOdometry(msg.get());
}

void ROSMotionController::poseTFCallback(const geometry_msgs::PoseWithCovarianceStamped::ConstPtr& msg) {
    ros_serial_bridge::Pose pose;
    pose.x = msg.get()->pose.pose.position.x;
    pose.y = msg.get()->pose.pose.position.y;
    pose.theta = tf::getYaw(msg.get()->pose.pose.orientation);
    pose.space = pose.space;
    saveOdometry(&pose);
}

void ROSMotionController::saveOdometry(const ros_serial_bridge::Pose* pose) {
    positon_joint_left = 0;
    positon_joint_right = 0;
    coordinate_t coordinate;
    coordinate.x = pose->x;
    coordinate.y = pose->y;
    coordinate.theta = pose->theta;
    coordinate.space = pose->space;
    try {
        serial_->parserSendPacket(serial_->createDataPacket(COORDINATE, HASHMAP_MOTION, (abstract_message_u*) & coordinate), 3, boost::posix_time::millisec(200));
    } catch (exception &e) {
        ROS_ERROR("%s", e.what());
    }
}

void ROSMotionController::sendOdometry(const velocity_t* velocity, const ros_serial_bridge::Pose* pose) {
    ros::Time current_time = ros::Time::now();
    //first, we'll publish the transform over tf
    geometry_msgs::TransformStamped odom_trans;

    //since all odometry is 6DOF we'll need a quaternion created from yaw
    geometry_msgs::Quaternion odom_quat = tf::createQuaternionMsgFromYaw(pose->theta);

    odom_trans.header.stamp = current_time;
    odom_trans.header.frame_id = tf_odometry_string_;
    odom_trans.child_frame_id = tf_base_link_string_;
    odom_trans.transform.translation.x = pose->x;
    odom_trans.transform.translation.y = pose->y;
    odom_trans.transform.translation.z = 0.0;
    odom_trans.transform.rotation = odom_quat;

    //send the transform
    odom_broadcaster.sendTransform(odom_trans);

    //next, we'll publish the odometry message over ROS
    nav_msgs::Odometry odom;
    odom.header.stamp = current_time;
    odom.header.frame_id = tf_odometry_string_;

    //set the position
    odom.pose.pose.position.x = pose->x;
    odom.pose.pose.position.y = pose->y;
    odom.pose.pose.position.z = 0.0;
    odom.pose.pose.orientation = odom_quat;

    //set the velocity
    odom.child_frame_id = tf_base_link_string_;
    odom.twist.twist.linear.x = velocity->v;
    odom.twist.twist.linear.y = 0;
    odom.twist.twist.angular.z = velocity->w;

    //publish the message
    pub_odom.publish(odom);
}

void ROSMotionController::sendJointState(ros_serial_bridge::Motor* motor_left, ros_serial_bridge::Motor* motor_right) {
    ros::Time now = ros::Time::now();
    double rate = (now - old_time).toSec();
    old_time = now;
    nh_.getParam(joint_string + "/back_emf/" + left_string, k_ele_left);
    nh_.getParam(joint_string + "/back_emf/" + right_string, k_ele_right);

    joint.header.stamp = now;
    joint.velocity[0] = motor_left->measure;
    joint.velocity[1] = motor_right->measure;
    positon_joint_left = fmod(positon_joint_left + (joint.velocity[0] * rate), 2 * M_PI);
    positon_joint_right = fmod(positon_joint_right + (joint.velocity[1] * rate), 2 * M_PI);
    joint.position[0] = positon_joint_left;
    joint.position[1] = positon_joint_right;
    joint.effort[0] = k_ele_left * motor_left->current;
    joint.effort[1] = k_ele_right * motor_right->current;
    pub_joint.publish(joint);
}

pid_control_t ROSMotionController::get_pid(std::string name) {
    pid_control_t pid;
    double temp;
    std::string name_pid = "pid/" + name + "/";
    nh_.getParam(name_pid + "P", temp);
    pid.kp = temp;
    nh_.getParam(name_pid + "I", temp);
    pid.ki = temp;
    nh_.getParam(name_pid + "D", temp);
    pid.kd = temp;
    return pid;
}

parameter_motor_t ROSMotionController::get_motor_parameter(std::string name) {
    parameter_motor_t parameter;
    double temp;
    int temp2;

    nh_.getParam(joint_string + "/" + name + "/cpr", temp);
    parameter.cpr = temp;
    nh_.getParam(joint_string + "/" + name + "/ratio", temp);
    parameter.ratio = temp;
    nh_.getParam(joint_string + "/" + name + "/encoder_pos", temp);
    parameter.encoder_pos = temp;
    nh_.getParam(joint_string + "/" + name + "/volt_bridge", temp);
    parameter.volt_bridge = temp;
    nh_.getParam(joint_string + "/" + name + "/versus", temp2);
    parameter.versus = temp2;
    nh_.getParam(joint_string + "/" + name + "/default_enable", temp2);
    parameter.enable_set = temp2;
    return parameter;
}

parameter_unicycle_t ROSMotionController::get_unicycle_parameter() {
    parameter_unicycle_t parameter;
    double temp;

    nh_.getParam("structure/" + wheelbase_string, temp);
    parameter.wheelbase = temp;
    nh_.getParam("structure/" + radius_string + "/" + right_string, temp);
    parameter.radius_r = temp;
    nh_.getParam("structure/" + radius_string + "/" + left_string, temp);
    parameter.radius_l = temp;
    nh_.getParam("odo_mis_step", temp);
    parameter.sp_min = temp;
    return parameter;
}

emergency_t ROSMotionController::get_emergency() {
    emergency_t emergency;
    double temp;
    nh_.getParam(emergency_string + "/bridge_off", temp);
    emergency.bridge_off = temp;
    nh_.getParam(emergency_string + "/slope_time", temp);
    emergency.slope_time = temp;
    nh_.getParam(emergency_string + "/timeout", temp);
    emergency.timeout = (int16_t) (temp * 1000);
    return emergency;
}

constraint_t ROSMotionController::get_constraint() {
    constraint_t constraint;
    int temp;

    nh_.getParam(joint_string + "/constraint/" + right_string, temp);
    constraint.max_right = (int16_t) temp;
    nh_.getParam(joint_string + "/constraint/" + left_string, temp);
    constraint.max_left = (int16_t) temp;
    return constraint;
}

bool ROSMotionController::pidServiceCallback(ros_serial_bridge::Update::Request &req, ros_serial_bridge::Update::Response&) {
    std::string name = req.name;
    pid_control_t pid;
    std::vector<information_packet_t> list_send;
    ROS_INFO("PID UPDATE");
    if ((name.compare(left_string) == 0) || (name.compare(all_string) == 0)) {
        pid = get_pid(left_string);
        list_send.push_back(serial_->createDataPacket(PID_CONTROL_L, HASHMAP_MOTION, (abstract_message_u*) & pid));
    }
    if ((name.compare(right_string) == 0) || (name.compare(all_string) == 0)) {
        pid = get_pid(right_string);
        list_send.push_back(serial_->createDataPacket(PID_CONTROL_R, HASHMAP_MOTION, (abstract_message_u*) & pid));
    }
    try {
        serial_->parserSendPacket(list_send, 3, boost::posix_time::millisec(200));
    } catch (exception &e) {
        ROS_ERROR("%s", e.what());
    }
    return true;
}

bool ROSMotionController::parameterServiceCallback(ros_serial_bridge::Update::Request &req, ros_serial_bridge::Update::Response&) {
    std::string name = req.name;
    parameter_motor_t parameter_motor;
    std::vector<information_packet_t> list_send;
    ROS_INFO("PARAMETER UPDATE");
    if ((name.compare(left_string) == 0) || (name.compare(all_string) == 0)) {
        parameter_motor = get_motor_parameter(left_string);
        list_send.push_back(serial_->createDataPacket(PARAMETER_MOTOR_L, HASHMAP_MOTION, (abstract_message_u*) & parameter_motor));
    }
    if ((name.compare(right_string) == 0) || (name.compare(all_string) == 0)) {
        parameter_motor = get_motor_parameter(right_string);
        list_send.push_back(serial_->createDataPacket(PARAMETER_MOTOR_R, HASHMAP_MOTION, (abstract_message_u*) & parameter_motor));
    }
    if ((name.compare(paramenter_unicycle_string) == 0) || (name.compare(all_string) == 0)) {
        parameter_unicycle_t parameter_unicycle = get_unicycle_parameter();
        list_send.push_back(serial_->createDataPacket(PARAMETER_UNICYCLE, HASHMAP_MOTION, (abstract_message_u*) & parameter_unicycle));
    }
    try {
        serial_->parserSendPacket(list_send, 3, boost::posix_time::millisec(200));
    } catch (exception &e) {
        ROS_ERROR("%s", e.what());
    }
    return true;
}

bool ROSMotionController::constraintServiceCallback(std_srvs::Empty::Request&, std_srvs::Empty::Response&) {
    constraint_t constraint = get_constraint();
    try {
        serial_->parserSendPacket(serial_->createDataPacket(CONSTRAINT, HASHMAP_MOTION, (abstract_message_u*) & constraint), 3, boost::posix_time::millisec(200));
    } catch (exception &e) {
        ROS_ERROR("%s", e.what());
    }
    return true;
}

bool ROSMotionController::emergencyServiceCallback(std_srvs::Empty::Request&, std_srvs::Empty::Response&) {
    emergency_t emergency = get_emergency();
    try {
        serial_->parserSendPacket(serial_->createDataPacket(EMERGENCY, HASHMAP_MOTION, (abstract_message_u*) & emergency), 3, boost::posix_time::millisec(200));
    } catch (exception &e) {
        ROS_ERROR("%s", e.what());
    }
    return true;
}
