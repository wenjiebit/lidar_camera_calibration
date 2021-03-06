#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <map>
#include <fstream>
#include <cmath>

#include "opencv2/opencv.hpp"
#include <pcl/point_cloud.h>
#include <pcl/io/pcd_io.h>

#include <ros/package.h>

#include <pcl_ros/point_cloud.h>
#include <boost/foreach.hpp>
#include <pcl_conversions/pcl_conversions.h>
#include <velodyne_pointcloud/point_types.h>
#include <pcl/common/eigen.h>
#include <pcl/common/transforms.h>
#include <pcl/filters/passthrough.h>
#include <pcl/sample_consensus/ransac.h>
#include <pcl/sample_consensus/sac_model_line.h>
#include <pcl/common/intersections.h>

//------------- PointT declaration ---------------//

struct myPointXYZRID{
	PCL_ADD_POINT4D
		; // quad-word XYZ
	float intensity; ///< laser intensity reading
	uint16_t ring; ///< laser ring number
	float range;EIGEN_MAKE_ALIGNED_OPERATOR_NEW // ensure proper alignment
};

POINT_CLOUD_REGISTER_POINT_STRUCT(
		myPointXYZRID, (float, x, x) (float, y, y) (float, z, z) (float, intensity, intensity) (uint16_t, ring, ring))

//------------- config utils ---------------//

struct config_settings
{
	cv::Size s;			                        //image width and height
	std::vector<std::pair<float,float>> xyz_;   //filter point cloud to region of interest
	float intensity_thresh;                     //intensity thresh
	int num_of_markers;
	bool useCameraInfo;
	cv::Mat P;

	void print()
	{
		std::cout <<  "Size of the frame: " << s.width << " x " << s.height << "\n";
		for(int i = 0; i < 3; i++)
		{
			std::cout << "Limits: " << xyz_[i].first << " to " << xyz_[i].second << std::endl;
		}
		std::cout << "Number of markers: " << num_of_markers << std::endl;
		std::cout << "Intensity threshold (between 0.0 and 1.0): " << intensity_thresh << "\nuseCameraInfo: " << useCameraInfo << "\n";
		std::cout << "Projection matrix: \n" << P << "\n";
	}
}config;

void readConfig()
{
	std::string pkg_loc = ros::package::getPath("lidar_camera_calibration");
	//std::cout<< "The conf file location: " << pkg_loc <<"/conf/config_file.txt" << std::endl;
	std::ifstream infile(pkg_loc + "/conf/config_file.txt");
	float left_limit=0.0, right_limit=0.0;

	infile >> config.s.width >> config.s.height;
	for(int i = 0; i<3; i++)
	{
		infile >> left_limit >> right_limit;
		config.xyz_.push_back(std::pair<float,float>(left_limit, right_limit));
	}

	infile >> config.intensity_thresh >> config.num_of_markers >> config.useCameraInfo;
	float p[12];
	for(int i=0; i<12; i++)
	{
		infile >> p[i];
	}
	cv::Mat(3, 4, CV_32FC1, &p).copyTo(config.P);

	infile.close();
	config.print();
}

//------------- point cloud utils ---------------//
pcl::PointCloud<pcl::PointXYZ>* toPointsXYZ(pcl::PointCloud<myPointXYZRID> point_cloud)
{
	pcl::PointCloud<pcl::PointXYZ> *new_cloud = new pcl::PointCloud<pcl::PointXYZ>();
	for (pcl::PointCloud<myPointXYZRID>::iterator pt = point_cloud.points.begin(); pt < point_cloud.points.end(); pt++)
	{
		new_cloud->push_back(pcl::PointXYZ(pt->x, pt->y, pt->z));
	}
	return new_cloud;
}


pcl::PointCloud<myPointXYZRID> transform(pcl::PointCloud<myPointXYZRID> pc, float x, float y, float z, float rot_x, float rot_y, float rot_z)
{
	Eigen::Affine3f transf = pcl::getTransformation(x, y, z, rot_x, rot_y, rot_z);
	pcl::PointCloud<myPointXYZRID> new_cloud;
	pcl::transformPointCloud(pc, new_cloud, transf);
	return new_cloud;
}


pcl::PointCloud<myPointXYZRID> normalizeIntensity(pcl::PointCloud<myPointXYZRID> point_cloud, float min, float max)
{
	float min_found = 10e6;
	float max_found = -10e6;

	for (pcl::PointCloud<myPointXYZRID>::iterator pt = point_cloud.points.begin(); pt < point_cloud.points.end(); pt++)
	{
		max_found = MAX(max_found, pt->intensity);
		min_found = MIN(min_found, pt->intensity);
	}

	for (pcl::PointCloud<myPointXYZRID>::iterator pt = point_cloud.points.begin(); pt < point_cloud.points.end(); pt++)
	{
		pt->intensity = (pt->intensity - min_found) / (max_found - min_found) * (max - min) + min;
	}
	return point_cloud;
}


pcl::PointCloud<myPointXYZRID> intensityByRangeDiff(pcl::PointCloud<myPointXYZRID> point_cloud, config_settings config)
{

	std::vector<std::vector<myPointXYZRID*>> rings(16);
	
	for(pcl::PointCloud<myPointXYZRID>::iterator pt = point_cloud.points.begin() ; pt < point_cloud.points.end(); pt++){
		pt->range = (pt->x * pt->x + pt->y * pt->y + pt->z * pt->z);
		rings[pt->ring].push_back(&(*pt));
	}

	for(std::vector<std::vector<myPointXYZRID*>>::iterator ring = rings.begin(); ring < rings.end(); ring++){
		myPointXYZRID* prev, *succ;
		if (ring->empty())
		{
			continue;
		}
		float last_intensity = (*ring->begin())->intensity;
		float new_intensity;
		(*ring->begin())->intensity = 0;
		(*(ring->end() - 1))->intensity = 0;
		for (std::vector<myPointXYZRID*>::iterator pt = ring->begin() + 1; pt < ring->end() - 1; pt++)
		{
			prev = *(pt - 1);
			succ = *(pt + 1);
			

			(*pt)->intensity = MAX( MAX( prev->range-(*pt)->range, succ->range-(*pt)->range), 0) * 10;
		}
	}
	point_cloud = normalizeIntensity(point_cloud, 0.0, 1.0);

	pcl::PointCloud<myPointXYZRID> filtered;

	for(pcl::PointCloud<myPointXYZRID>::iterator pt = point_cloud.points.begin() ; pt < point_cloud.points.end(); pt++)
	{
		if(pt->intensity  >  config.intensity_thresh)
		{
			if(pt->x >= config.xyz_[0].first && pt->x <= config.xyz_[0].second && pt->y >= config.xyz_[1].first && pt->y <= config.xyz_[1].second && pt->z >= config.xyz_[2].first && pt->z <= config.xyz_[2].second)
			{
				filtered.push_back(*pt);
			}
		}
	}

	//pcl::io::savePCDFileASCII ("/home/vishnu/PCDs/filtered.pcd", *(toPointsXYZ(filtered)));
	return filtered;
}