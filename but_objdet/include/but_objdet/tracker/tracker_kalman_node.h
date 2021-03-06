/******************************************************************************
 * \file
 *
 * $Id:$
 *
 * Copyright (C) Brno University of Technology
 *
 * This file is part of software developed by dcgm-robotics@FIT group.
 *
 * Author: David Chrapek, Tomas Hodan
 * Supervised by: Vitezslav Beran (beranv@fit.vutbr.cz), Michal Spanel (spanel@fit.vutbr.cz)
 * Date: 01/04/2012
 *
 * This file is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this file.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once
#ifndef _TRACKER_KALMAN_NODE_
#define _TRACKER_KALMAN_NODE_

#include <map>
#include <ros/ros.h> // Main header of ROS
#include <sensor_msgs/Image.h>

#include "but_objdet_msgs/DetectionArray.h"
#include "but_objdet/tracker/tracker_kalman.h"


// Indicates if to visualize detections and predictions in a window
#define VISUAL_OUTPUT 1


namespace but_objdet
{

/**
  * A structure storing data related to a detection of a particular object.
  */
struct DetM
{
    but_objdet_msgs::Detection det; // Detection
    TrackerKalman *kf; // Kalman filter for tracking of this detection
    int ttl; // Time to live
    int msTime; // Time of detection in milliseconds
};

/**
 * A class implementing the tracker node, which creates and maintains a Kalman filter
 * tracker for each detected object (if there is no detection of an object for
 * some time / number of frames, the tracker for that object is canceled).
 * It also advertises a service for prediction of the next state of detections,
 * (either of all of the currently maintained or of some specified object class or
 * object id).
 *
 * @author Tomas Hodan, Vitezslav Beran (beranv@fit.vutbr.cz), Michal Spanel (spanel@fit.vutbr.cz)
 */
class TrackerKalmanNode
{
public:
	TrackerKalmanNode();
	~TrackerKalmanNode();

private:
    /**
     * ROS related initialization called from the constructor.
     */
	void rosInit();

    /**
     * A function implementing the prediction service.
     * @param req  Service request.
     * @param res  Service response.
     * @return  Success / failure of the service.
     */
	bool predictDetections(but_objdet::PredictDetections::Request &req,
						   but_objdet::PredictDetections::Response &res);
        
    /**
     * A function implementing the get objects service.
     * @param req  Service request.
     * @param res  Service response.
     * @return  Success / failure of the service.
     */
	bool getObjects(but_objdet::GetObjects::Request &req,
						   but_objdet::GetObjects::Response &res);

    /**
     * Conversion from a ROS Time to miliseconds.
     * @param stamp  ROS Time.
     * @return  Miliseconds.
     */
	int rosTimeToMs(ros::Time stamp);

    /**
     * A callback function called when new detections are received.
     * @param detArrayMsg  DetectionArray message.
     */
	void newDataCallback(const but_objdet_msgs::DetectionArrayConstPtr &detArrayMsg);

    /**
     * A callback function called when a new Image is received. The image is used just
     * for visualization of detections and predictions, thus it doesn't influence
     * functionality of this node in any way.
     * @param imageMsg  Image message.
     */
	void newImageCallback(const sensor_msgs::ImageConstPtr &imageMsg);

    /**
     * Memory of currently considered detections.
     * 
     */
	
	typedef std::map<int, DetM> _DetMem; //m_id
        typedef std::map<int, _DetMem> DetMem; //m_class
        DetMem detectionMem;

	/**
	 * If a detection of an object didn't occur in the specified number of
	 * last detections (specified by a value of this variable),
	 * it is not considered any more.
	 */
	int defaultTtl;

    /**
	 * If a detection of an object doesn't occur again during this period,
	 * it is not considered any more.
	 */
	int defaultTtlTime;

    ros::NodeHandle nh; // NodeHandle is the main access point for communication with ROS system
	ros::ServiceServer predictionSRV;
	ros::ServiceServer objectsSRV; //service for providing objects
	ros::Subscriber detSub;
	ros::Subscriber imgSub;
	std::string winName;
};

}

#endif // _TRACKER_KALMAN_NODE_

