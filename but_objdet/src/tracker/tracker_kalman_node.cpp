/******************************************************************************
 * \file
 *
 * $Id:$
 *
 * Copyright (C) Brno University of Technology
 *
 * This file is part of software developed by dcgm-robotics@FIT group.
 *
 * Author: Tomas Hodan
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

#include <ros/ros.h> // Main header of ROS

// ObjDet API
#include "but_objdet/but_objdet.h" // Main objects of ObjDet API
#include "but_objdet/services_list.h" // Names of services provided by but_objdet package
#include "but_objdet/PredictDetections.h" // Autogenerated service class
#include "but_objdet_msgs/DetectionArray.h" // Message transfering detections/predictions

#include <opencv2/highgui/highgui.hpp>
#include <cv_bridge/cv_bridge.h>

#include "but_objdet/tracker/tracker_kalman.h"
#include "but_objdet/tracker/tracker_kalman_node.h"
#include <../../opt/ros/electric/stacks/ros_comm/utilities/rostime/include/ros/duration.h>

using namespace std;
using namespace cv;
using namespace but_objdet_msgs;

// If set to 1, detections will be visualized. If a tracker node is used, it is
// better to visualize the detections together with predictions there.
#define VISUAL_OUTPUT 1

const string imageTopic = "/cam3d/rgb/image_raw";
const string detectionTopic = "/but_objdet/detections";


namespace but_objdet
{

/* -----------------------------------------------------------------------------
 * Constructor
 */
TrackerKalmanNode::TrackerKalmanNode()
{   
    defaultTtl = 5;
    defaultTtlTime = 5000; // = 5s

    // Window name (for visualization detections and predictions)
    if(VISUAL_OUTPUT) {
        winName = "Tracker (white = detections, red = predictions)";
    }

    rosInit(); // ROS-related initialization
}


/* -----------------------------------------------------------------------------
 * Destructor
 */
TrackerKalmanNode::~TrackerKalmanNode()
{
    DetMem::iterator it;
    for (it = detectionMem.begin(); it != detectionMem.end(); it++) {
        delete it->second.kf; // Free Kalman filter
    }
    
    // Create a window to vizualize the incoming video, detections and predictions
    if(VISUAL_OUTPUT) {
        namedWindow(winName, CV_WINDOW_AUTOSIZE);
    }
}


/* -----------------------------------------------------------------------------
 * ROS-related initialization
 */
void TrackerKalmanNode::rosInit()
{
    // Create and advertise a service for prediction of detections
    predictionSRV = nh.advertiseService(BUT_OBJDET_PredictDetections_SRV,
        &TrackerKalmanNode::predictDetections, this);
    
    // Subscribe to a topic with detections (published by a detector node)
    detSub = nh.subscribe(detectionTopic, 10, &TrackerKalmanNode::newDataCallback, this);
    
    if(VISUAL_OUTPUT) {
        // Subscribe to a topic with images
        imgSub = nh.subscribe(imageTopic, 10, &TrackerKalmanNode::newImageCallback, this);
    }
    
    // Inform that the tracker is running (it will be written into console)
    ROS_INFO("Tracker is running...");
}


/* -----------------------------------------------------------------------------
 * Function implementing the prediction service
 */
bool TrackerKalmanNode::predictDetections(but_objdet::PredictDetections::Request &req,
                                          but_objdet::PredictDetections::Response &res)
{   
    //ROS_INFO("New request: object_id: %d, class_id: %d", req.object_id, req.class_id);

    // Object ID was specified
    if(req.object_id != -1) {
        DetMem::iterator it = detectionMem.find(req.object_id);
        
        // When it was found
        if(it != detectionMem.end()) {
            but_objdet_msgs::Detection det = detectionMem[req.object_id].det;
            
            // Request time in miliseconds from the time of detection
            int predTime = rosTimeToMs(req.header.stamp) - detectionMem[req.object_id].msTime;
            
            // Get prediction
            Mat prediction = detectionMem[req.object_id].kf->predict(predTime);
            det.m_bb.x = prediction.at<float>(0);
            det.m_bb.y = prediction.at<float>(1);
            det.m_bb.width = prediction.at<float>(2);
            det.m_bb.height = prediction.at<float>(3);
            
            res.predictions.push_back(det);
        }
        else {
            ROS_WARN("A detection with requested ID is not available.");
        }
    }
    
    // Class ID was specified => return all detections from that class
    else if(req.class_id != -1) {
        DetMem::iterator it;
        for (it = detectionMem.begin(); it != detectionMem.end(); it++) {
            if(it->second.det.m_class == req.class_id) {
                
                but_objdet_msgs::Detection det = it->second.det;
        
                // Request time in miliseconds from the time of detection
                int predTime = rosTimeToMs(req.header.stamp) - it->second.msTime;
                
                // Get prediction
                Mat prediction = it->second.kf->predict(predTime);
                det.m_bb.x = prediction.at<float>(0);
                det.m_bb.y = prediction.at<float>(1);
                det.m_bb.width = prediction.at<float>(2);
                det.m_bb.height = prediction.at<float>(3);
                
                res.predictions.push_back(det);
            }
        }
    }
    
    // Nothing specified => return predictions for all stored detections
    else {
        DetMem::iterator it;
        for (it = detectionMem.begin(); it != detectionMem.end(); it++) {
            but_objdet_msgs::Detection det = it->second.det;

            // Request time in miliseconds from the time of detection
            int predTime = rosTimeToMs(req.header.stamp) - it->second.msTime;
            
            // Get prediction
            Mat prediction = it->second.kf->predict(predTime);
            det.m_bb.x = prediction.at<float>(0);
            det.m_bb.y = prediction.at<float>(1);
            det.m_bb.width = prediction.at<float>(2);
            det.m_bb.height = prediction.at<float>(3);
            
            res.predictions.push_back(det);
        }
    }
    
    return true;
}


/* -----------------------------------------------------------------------------
 * Callback function called when new detections are received
 */
void TrackerKalmanNode::newDataCallback(const but_objdet_msgs::DetectionArrayConstPtr &detArrayMsg)
{   
    //ROS_INFO("New data.");

    for(unsigned int i = 0; i < detArrayMsg->detections.size(); i++) {
        int detId = detArrayMsg->detections[i].m_id;
        
        // Check if the current detection is already in the memory
        DetMem::iterator it = detectionMem.find(detId);
        
        // When it was found
        if(it != detectionMem.end()) {
            //ROS_INFO("Object ID found!");
            detectionMem[detId].det = detArrayMsg->detections[i];
            detectionMem[detId].ttl++;
            
            int time = rosTimeToMs(detArrayMsg->header.stamp);
            int timeFromLastUpdate = time - detectionMem[detId].msTime;
            detectionMem[detId].msTime = time;
            
            // Update
            Mat newMeasurement(1, 4, CV_32F);
		    newMeasurement.at<float>(0) = detectionMem[detId].det.m_bb.x;
		    newMeasurement.at<float>(1) = detectionMem[detId].det.m_bb.y;
		    newMeasurement.at<float>(2) = detectionMem[detId].det.m_bb.width;
		    newMeasurement.at<float>(3) = detectionMem[detId].det.m_bb.height;

            detectionMem[detId].kf->update(newMeasurement, timeFromLastUpdate);
        }
        
        // When it wasn't found => add it to memory
        else {
            //ROS_INFO("Object ID not found!");
            detectionMem[detId].det = detArrayMsg->detections[i];
            detectionMem[detId].ttl = defaultTtl;
            detectionMem[detId].kf = new TrackerKalman();
            detectionMem[detId].msTime = rosTimeToMs(detArrayMsg->header.stamp);
            
		    // Initialization with the first measurement
		    Mat initMeasurement(1, 4, CV_32F);
		    initMeasurement.at<float>(0) = detectionMem[detId].det.m_bb.x;
		    initMeasurement.at<float>(1) = detectionMem[detId].det.m_bb.y;
		    initMeasurement.at<float>(2) = detectionMem[detId].det.m_bb.width;
		    initMeasurement.at<float>(3) = detectionMem[detId].det.m_bb.height;
            detectionMem[detId].kf->init(initMeasurement, true);
        }
    }
    
    // Decrease TTL to all saved detections
    DetMem::iterator it;
    vector<int> toBeRemoved; // Vector of IDs whose detection has TTL = 0
    for (it = detectionMem.begin(); it != detectionMem.end(); it++) {
        it->second.ttl--;
        //std::cout << "TTL: " << it->second.ttl << " " << detectionMem.size() << std::endl;
        
        // If it didn't show up in the specified number of last detections
        // or during the specified time period => mark it for removal
        if(it->second.ttl == 0 || (rosTimeToMs(ros::Time::now()) - it->second.msTime) > defaultTtlTime) {
            toBeRemoved.push_back(it->second.det.m_id);
        }
    }
    
    // Removed marked detections
    for(unsigned int i = 0; i < toBeRemoved.size(); i++) {
        delete detectionMem[toBeRemoved[i]].kf; // Free Kalman filter
        detectionMem.erase(toBeRemoved[i]);
    }

}


/* -----------------------------------------------------------------------------
 * Callback function called when new Image is received. The image is used just
 * for visualization of detections and predictions, thus it doesn't influence
 * functionality of this node in any way.
 */
void TrackerKalmanNode::newImageCallback(const sensor_msgs::ImageConstPtr &imageMsg)
{

    // Get an OpenCV Mat from the image message
    Mat image;
    try {
        image = cv_bridge::toCvCopy(imageMsg)->image;
    }
    catch (cv_bridge::Exception& e) {
        ROS_ERROR("cv_bridge exception: %s", e.what());
        return;
    }
    
    // Convert to 3 channels - so we can visualize BB in color
    Mat img3ch;
    if(image.channels() != 3) {
        cvtColor(image, img3ch, CV_GRAY2RGB, 3);
    }
    else {
        image.copyTo(img3ch);
    }
    
    DetMem::iterator it;
    for (it = detectionMem.begin(); it != detectionMem.end(); it++) {
        
        // Visualize detection
        Detection det = it->second.det;
        rectangle(
	        img3ch,
	        cvPoint(det.m_bb.x, det.m_bb.y),
	        cvPoint(det.m_bb.x + det.m_bb.width, det.m_bb.y + det.m_bb.height),
	        cvScalar(255,255,255)
	    );
	    
	    // Obtain and visualize corresponding prediction
	    int predTime = rosTimeToMs(ros::Time::now()) - it->second.msTime;
	    Mat prediction = it->second.kf->predict(predTime);
	    Detection pred;
        pred.m_bb.x = prediction.at<float>(0);
        pred.m_bb.y = prediction.at<float>(1);
        pred.m_bb.width = prediction.at<float>(2);
        pred.m_bb.height = prediction.at<float>(3);

        rectangle(
	        img3ch,
	        cvPoint(pred.m_bb.x, pred.m_bb.y),
	        cvPoint(pred.m_bb.x + pred.m_bb.width, pred.m_bb.y + pred.m_bb.height),
	        cvScalar(0,0,255)
	    );
    }
    
    if(VISUAL_OUTPUT) {
        imshow(winName, img3ch);
    }
}


/* =============================================================================
 * Converts ros::Time to miliseconds
 */
int TrackerKalmanNode::rosTimeToMs(ros::Time stamp)
{
    //std::cout << "Time: " << stamp.sec << " " << stamp.nsec << " " << stamp.sec * 1000 + stamp.nsec / 1000000 << std::endl;
    return stamp.sec * 1000 + stamp.nsec / 1000000;
}

}


/* =============================================================================
 * Main function
 */
int main(int argc, char **argv)
{//sleep(5);
    // ROS initialization (the last argument is the name of a ROS node)
    ros::init(argc, argv, "but_tracker_kalman");

    // Create the object managing connection with ROS system
    but_objdet::TrackerKalmanNode *tkn = new but_objdet::TrackerKalmanNode();
    
    // Enters a loop, calling message callbacks
    while(ros::ok()) {
        waitKey(10); // Process window events
        ros::spinOnce(); // Call all the message callbacks waiting to be called
    }
    
    delete tkn;
    
    return 0;
}

