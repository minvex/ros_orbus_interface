/* 
 * File:   AbstractROSController.h
 * Author: raffaello
 *
 * Created on 24 October 2013, 12:20
 */

#ifndef ABSTRACTROSCONTROLLER_H
#define	ABSTRACTROSCONTROLLER_H

#include "Serial.h"
#include "ServiceSerial.h"
#include "ros/ros.h"

#include <boost/thread.hpp>
#include <tf/transform_broadcaster.h>

class AbstractROSController {
public:
    virtual void loadParameter() = 0;
private:
};

#endif	/* ABSTRACTROSCONTROLLER_H */
