/* 
 * File:   ROSSensorController.h
 * Author: raffaello
 *
 * Created on 25 October 2013, 13:27
 */

#ifndef ROSSENSORCONTROLLER_H
#define	ROSSENSORCONTROLLER_H

#include "AbstractROSController.h"

#include <sensor_msgs/LaserScan.h>
#include <geometry_msgs/PointStamped.h>
#include <tf/transform_listener.h>

#define NUMBER_PUBLISHER 10
const std::string default_laser_sharp_string = "laser_sharp";
const std::string default_base_link_string = "base_link";
const std::string laser_sharp_position_string = "sharp_pose";

class ROSSensorController : public AbstractROSController {
public:
    ROSSensorController(std::string name_node, const ros::NodeHandle& nh, Serial* serial, ServiceSerial* service_serial);
    ROSSensorController(const ROSSensorController& orig);
    virtual ~ROSSensorController();
    
    void loadParameter();
private:
    //Initialization object
    std::string name_node_; //Name for topics, params, services
    ros::NodeHandle nh_; //NameSpace for bridge controller
    Serial* serial_; //Serial object to comunicate with PIC device
    ServiceSerial* service_serial_;     //Service with board
    
    ros::Publisher pub_laser_sharp_;
    tf::TransformBroadcaster broadcaster_;
    tf::Vector3 pose_laser_sharp_;
    tf::Quaternion angle_laser_sharp_;
    
    std::string base_link_string_, laser_sharp_string_;
    
    void actionAsync(packet_t packet);
    void connectCallback(const ros::SingleSubscriberPublisher& pub);
    
    void sendLaserSharp(infrared_t infrared);
    //TODO to remove
    ros::Timer timer_;
    int count;
    void timerCallback(const ros::TimerEvent& event);
};

#endif	/* ROSSENSORCONTROLLER_H */
