#include <cstdlib>
#include <cstdio>
#include <math.h>
#include <algorithm>
#include <map>

#include "opencv2/opencv.hpp"

#include <ros/ros.h>
#include <cv_bridge/cv_bridge.h>
#include <image_transport/image_transport.h>
#include <sensor_msgs/image_encodings.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/CameraInfo.h>
#include <camera_info_manager/camera_info_manager.h>

#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

#include <pcl_ros/point_cloud.h>
#include <boost/foreach.hpp>
#include <pcl_conversions/pcl_conversions.h>
#include <velodyne_pointcloud/point_types.h>
#include <pcl/common/eigen.h>
#include <pcl/common/transforms.h>
#include <pcl/filters/passthrough.h>

#include "lidar_camera_calibration/Corners.h"
#include "lidar_camera_calibration/PreprocessUtils.h"
#include "lidar_camera_calibration/Find_RT.h"

using namespace cv;
using namespace std;
using namespace ros;
using namespace message_filters;
using namespace pcl;


string CAMERA_INFO_TOPIC;
string VELODYNE_TOPIC;


Mat projection_matrix;
Mat frame_rgb;

pcl::PointCloud<myPointXYZRID> point_cloud;


void callback_noCam(const sensor_msgs::PointCloud2ConstPtr& msg_pc)
{
	ROS_INFO_STREAM("Velodyne scan received at " << msg_pc->header.stamp.toSec());

	// Loading Velodyne point cloud_sub
	fromROSMsg(*msg_pc, point_cloud);
	point_cloud = transform(point_cloud, 0, 0, 0, M_PI/2, -M_PI / 2, 0);
	point_cloud = intensityByRangeDiff(point_cloud, config);
	// x := x, y := -z, z := y

	//pcl::io::savePCDFileASCII ("/home/vishnu/PCDs/msg_point_cloud.pcd", pc);  


	cv::Mat temp_mat(config.s, CV_8UC3);
	pcl::PointCloud<pcl::PointXYZ> retval = *(toPointsXYZ(point_cloud));

	getCorners(temp_mat, retval, config.P, config.num_of_markers);
	find_transformation();
	ros::shutdown();
}

void callback(const sensor_msgs::CameraInfoConstPtr& msg_info,
			  const sensor_msgs::PointCloud2ConstPtr& msg_pc)
{

	//ROS_INFO_STREAM("Image received at " << msg_img->header.stamp.toSec());
	ROS_INFO_STREAM("Camera info received at " << msg_info->header.stamp.toSec());
	ROS_INFO_STREAM("Velodyne scan received at " << msg_pc->header.stamp.toSec());

	// Loading camera image:
	//cv_bridge::CvImagePtr cv_ptr = cv_bridge::toCvCopy(msg_img, sensor_msgs::image_encodings::BGR8);
	//frame_rgb = cv_ptr->image;

	// Loading projection matrix:
	float p[12];
	float *pp = p;
	for (boost::array<double, 12ul>::const_iterator i = msg_info->P.begin(); i != msg_info->P.end(); i++)
	{
	*pp = (float)(*i);
	pp++;
	}
	cv::Mat(3, 4, CV_32FC1, &p).copyTo(projection_matrix);



	// Loading Velodyne point cloud_sub
	fromROSMsg(*msg_pc, point_cloud);
	point_cloud = transform(point_cloud, 0, 0, 0, M_PI/2, -M_PI / 2, 0);
	point_cloud = intensityByRangeDiff(point_cloud, config);
	// x := x, y := -z, z := y

	//pcl::io::savePCDFileASCII ("/home/vishnu/PCDs/msg_point_cloud.pcd", pc);  


	cv::Mat temp_mat(config.s, CV_8UC3);
	pcl::PointCloud<pcl::PointXYZ> retval = *(toPointsXYZ(point_cloud));

	getCorners(temp_mat, retval, projection_matrix, config.num_of_markers);
	find_transformation();
	ros::shutdown();
}


int main(int argc, char** argv)
{
	readConfig();
	ros::init(argc, argv, "find_transform");

	

	ros::NodeHandle n;

	if(config.useCameraInfo)
	{
		ROS_INFO_STREAM("Reading CameraInfo from topic");
		n.getParam("/lidar_camera_calibration/camera_info_topic", CAMERA_INFO_TOPIC);
		n.getParam("/lidar_camera_calibration/velodyne_topic", VELODYNE_TOPIC);

		message_filters::Subscriber<sensor_msgs::CameraInfo> info_sub(n, CAMERA_INFO_TOPIC, 1);
		message_filters::Subscriber<sensor_msgs::PointCloud2> cloud_sub(n, VELODYNE_TOPIC, 1);

		typedef sync_policies::ApproximateTime<sensor_msgs::CameraInfo, sensor_msgs::PointCloud2> MySyncPolicy;
		Synchronizer<MySyncPolicy> sync(MySyncPolicy(10), info_sub, cloud_sub);
		sync.registerCallback(boost::bind(&callback, _1, _2));

		ros::spin();
	}
	else
	{
		ROS_INFO_STREAM("Reading CameraInfo from configuration file");
  		n.getParam("/lidar_camera_calibration/velodyne_topic", VELODYNE_TOPIC);
		ros::Subscriber sub = n.subscribe(VELODYNE_TOPIC, 1000, callback_noCam);
		ros::spin();
	}

	return EXIT_SUCCESS;
}