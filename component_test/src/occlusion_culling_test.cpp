#include "ros/ros.h"
#include <ros/package.h>
#include <sensor_msgs/PointCloud2.h>
#include <tf/tf.h>
#include <tf_conversions/tf_eigen.h>
#include <geometry_msgs/Pose.h>
#include <eigen_conversions/eigen_msg.h>
#include <Eigen/Geometry>
#include <Eigen/Dense>
#include <visualization_msgs/Marker.h>
//PCL
#include <iostream>
#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/filters/frustum_culling.h>
#include <pcl/visualization/cloud_viewer.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/common/eigen.h>
#include <pcl/common/transforms.h>
#include <pcl/range_image/range_image.h>
#include <voxel_grid_occlusion_estimation.h>
#include "fcl_utility.h"




int main(int argc, char **argv)
{

    ros::init(argc, argv, "occlusion_culling_test");
    ros::NodeHandle n;

    ros::Publisher pub1 = n.advertise<sensor_msgs::PointCloud2>("original_point_cloud", 100);
    ros::Publisher pub2 = n.advertise<sensor_msgs::PointCloud2>("frustum_point_cloud", 100);
    ros::Publisher pub3 = n.advertise<sensor_msgs::PointCloud2>("occlusion_free_cloud", 100);

    ros::Publisher marker_pub = n.advertise<visualization_msgs::Marker>("sensor_origin", 1);


    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new pcl::PointCloud<pcl::PointXYZ>);
    pcl::PointCloud<pcl::PointXYZ>::Ptr occlusionFreeCloud(new pcl::PointCloud<pcl::PointXYZ>);
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr rayCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
    pcl::PointCloud <pcl::PointXYZ>::Ptr output (new pcl::PointCloud <pcl::PointXYZ>);


    std::string path = ros::package::getPath("component_test");
    pcl::io::loadPCDFile<pcl::PointXYZ> (path+"/src/pcd/scaled_desktop_verydensed.pcd", *cloud);

    //*****************Frustum Culling*****************
    pcl::FrustumCulling<pcl::PointXYZ> fc (true);
    fc.setInputCloud (cloud);
    fc.setVerticalFOV (45);
    fc.setHorizontalFOV (58);
    fc.setNearPlaneDistance (0.8);
    fc.setFarPlaneDistance (5.8);

    Eigen::Matrix4f camera_pose;
    camera_pose.setZero ();
    Eigen::Matrix3f R;
    Eigen::Vector3f theta(0.0,180.0,0.0);
    R = Eigen::AngleAxisf (theta[0] * M_PI / 180, Eigen::Vector3f::UnitX ()) *
            Eigen::AngleAxisf (theta[1] * M_PI / 180, Eigen::Vector3f::UnitY ()) *
            Eigen::AngleAxisf (theta[2] * M_PI / 180, Eigen::Vector3f::UnitZ ());
    camera_pose.block (0, 0, 3, 3) = R;
    Eigen::Vector3f T;
    T (0) = 4; T (1) = 1.0; T (2) = -5;
    camera_pose.block (0, 3, 3, 1) = T;
    camera_pose (3, 3) = 1;
    fc.setCameraPose (camera_pose);

    fc.filter (*output);
    //    pcl::PCDWriter writer;
    //    writer.write<pcl::PointXYZRGB> (path+"/src/pcd/frustum_bun.pcd", *output, false);

    //*****************Visualization Camera View Vector (frustum culling tool camera) *****************
    // the rviz axis is different from the frustum camera axis and range image axis
    R = Eigen::AngleAxisf (theta[0] * M_PI / 180, Eigen::Vector3f::UnitX ()) *
            Eigen::AngleAxisf (-theta[1] * M_PI / 180, Eigen::Vector3f::UnitY ()) *
            Eigen::AngleAxisf (-theta[2] * M_PI / 180, Eigen::Vector3f::UnitZ ());
    tf::Matrix3x3 rotation;
    Eigen::Matrix3d D;
    D= R.cast<double>();
    tf::matrixEigenToTF(D,rotation);
    rotation = rotation.transpose();
    tf::Quaternion orientation;
    rotation.getRotation(orientation);

    //    pcl::PointCloud<pcl::Normal>::Ptr cloud_normals (new pcl::PointCloud<pcl::Normal>);
    geometry_msgs::Pose output_vector;
    Eigen::Quaterniond q;
    geometry_msgs::Quaternion quet;
    tf::quaternionTFToEigen(orientation, q);
    tf::quaternionTFToMsg(orientation,quet);
    Eigen::Affine3d pose;
    Eigen::Vector3d a;
    a[0]= T[0];
    a[1]= T[1];
    a[2]= T[2];
    pose.translation() = a;
    tf::poseEigenToMsg(pose, output_vector);
    visualization_msgs::Marker marker;

    //*****************voxel grid occlusion estimation *****************
    Eigen::Quaternionf quat(q.w(),q.x(),q.y(),q.z());
    output->sensor_origin_  = Eigen::Vector4f(a[0],a[1],a[2],0);
    output->sensor_orientation_= quat;
    pcl::VoxelGridOcclusionEstimationT voxelFilter;
    voxelFilter.setInputCloud (output);
    //voxelFilter.setLeafSize (0.03279f, 0.03279f, 0.03279f);
    voxelFilter.setLeafSize (0.04f, 0.04f, 0.04f);
    //voxelFilter.filter(*cloud); // This filter doesn't really work, it introduces points inside the sphere !
    voxelFilter.initializeVoxelGrid();

    int state,ret;
    Eigen::Vector3i t;
    pcl::PointXYZ pt,p1,p2;
    pcl::PointXYZRGB point;
    std::vector<Eigen::Vector3i, Eigen::aligned_allocator<Eigen::Vector3i> > out_ray;
    std::vector<geometry_msgs::Point> lineSegments;
    geometry_msgs::Point linePoint;
    Eigen::Vector3i  min_b = voxelFilter.getMinBoxCoordinates ();
    Eigen::Vector3i  max_b = voxelFilter.getMaxBoxCoordinates ();
    int count = 0;
    bool foundBug = false;
    // iterate over the entire voxel grid
    for (int kk = min_b.z (); kk <= max_b.z () && !foundBug; ++kk)
    {
        for (int jj = min_b.y (); jj <= max_b.y () && !foundBug; ++jj)
        {
            for (int ii = min_b.x (); ii <= max_b.x () && !foundBug; ++ii)
            {
                Eigen::Vector3i ijk (ii, jj, kk);
                // process all free voxels
                int index = voxelFilter.getCentroidIndexAt (ijk);
                Eigen::Vector4f centroid = voxelFilter.getCentroidCoordinate (ijk);
                point = pcl::PointXYZRGB(0,244,0);
                point.x = centroid[0];
                point.y = centroid[1];
                point.z = centroid[2];
                //if(index!=-1 && point.x>1.2 && point.y<0.2 && point.y>-0.2)
                if(index!=-1 )//&& point.x<-1.2 && point.y<0.2 && point.y>-0.2)
                {
                    out_ray.clear();
                    ret = voxelFilter.occlusionEstimation( state,out_ray, ijk);
                    std::cout<<"State is:"<<state<<"\n";
                    /*
                    if(state == 1)
                    {
                        if(count++>=10)
                        {
                            foundBug = true;
                            break;
                        }
                        cout<<"Number of voxels in ray:"<<out_ray.size()<<"\n";
                        for(uint j=0; j< out_ray.size(); j++)
                        {
                            Eigen::Vector4f centroid = voxelFilter.getCentroidCoordinate (out_ray[j]);
                            pcl::PointXYZRGB p = pcl::PointXYZRGB(255,255,0);
                            p.x = centroid[0];
                            p.y = centroid[1];
                            p.z = centroid[2];
                            rayCloud->points.push_back(p);
                            std::cout<<"Ray X:"<<p.x<<" y:"<< p.y<<" z:"<< p.z<<"\n";
                        }
                    }
                    */
                    if(state != 1)
                    {
                        // estimate direction to target voxel
                        Eigen::Vector4f direction = centroid - cloud->sensor_origin_;
                        direction.normalize ();
                        // estimate entry point into the voxel grid
                        float tmin = voxelFilter.rayBoxIntersection (cloud->sensor_origin_, direction,p1,p2);
                        if(tmin!=-1)
                        {
                            // coordinate of the boundary of the voxel grid
                            Eigen::Vector4f start = cloud->sensor_origin_ + tmin * direction;
                            linePoint.x = cloud->sensor_origin_[0]; linePoint.y = cloud->sensor_origin_[1]; linePoint.z = cloud->sensor_origin_[2];
                            //std::cout<<"Box Min X:"<<linePoint.x<<" y:"<< linePoint.y<<" z:"<< linePoint.z<<"\n";
                            lineSegments.push_back(linePoint);

                            linePoint.x = start[0]; linePoint.y = start[1]; linePoint.z = start[2];
                            //std::cout<<"Box Max X:"<<linePoint.x<<" y:"<< linePoint.y<<" z:"<< linePoint.z<<"\n";
                            lineSegments.push_back(linePoint);

                            linePoint.x = start[0]; linePoint.y = start[1]; linePoint.z = start[2];
                            //std::cout<<"Box Max X:"<<linePoint.x<<" y:"<< linePoint.y<<" z:"<< linePoint.z<<"\n";
                            lineSegments.push_back(linePoint);

                            linePoint.x = centroid[0]; linePoint.y = centroid[1]; linePoint.z = centroid[2];
                            //std::cout<<"Box Max X:"<<linePoint.x<<" y:"<< linePoint.y<<" z:"<< linePoint.z<<"\n";
                            lineSegments.push_back(linePoint);

                            rayCloud->points.push_back(point);
                            pt.x = centroid[0];
                            pt.y = centroid[1];
                            pt.z = centroid[2];
                            occlusionFreeCloud->points.push_back(pt);
                        }
                    }
                }
            }
        }
    }


    //*****************Rviz Visualization ************
    ros::Rate loop_rate(10);
    while (ros::ok())
    {
        //***marker publishing***
        uint32_t shape = visualization_msgs::Marker::ARROW;
        marker.type = shape;
        marker.action = visualization_msgs::Marker::ADD;
        //visulaization using the markers
        marker.scale.x = 0.5;
        marker.scale.y = 0.1;
        marker.scale.z = 0.1;
        // Set the color -- be sure to set alpha to something non-zero!
        marker.color.r = 0.0f;
        marker.color.g = 1.0f;
        marker.color.b = 0.0f;
        marker.color.a = 1.0;
        marker.ns = "basic_shapes";
        marker.id = 2;
        // ROS_INFO("Publishing Marker");
        // Set the frame ID and timestamp. See the TF tutorials for information on these.
        marker.pose =  output_vector;
        marker.pose.orientation  = quet;//output_vector.orientation;
        marker.header.frame_id = "base_point_cloud";
        marker.header.stamp = ros::Time::now();
        marker.lifetime = ros::Duration(10);
        // Publish the marker
        marker_pub.publish(marker);

        //***frustum cull and occlusion cull publish***
        sensor_msgs::PointCloud2 cloud1;
        sensor_msgs::PointCloud2 cloud2;
        sensor_msgs::PointCloud2 cloud3;

        pcl::toROSMsg(*cloud, cloud1); //cloud of original (white) using pcl::frustumcull
        pcl::toROSMsg(*output, cloud2); //cloud of frustum cull (red) using pcl::frustumcull
        pcl::toROSMsg(*occlusionFreeCloud, cloud3); //cloud of the Occlusion cull (blue) using pcl::rangeImage
        cloud1.header.frame_id = "base_point_cloud";
        cloud2.header.frame_id = "base_point_cloud";
        cloud3.header.frame_id = "base_point_cloud";

        cloud1.header.stamp = ros::Time::now();
        cloud2.header.stamp = ros::Time::now();
        cloud3.header.stamp = ros::Time::now();

        pub1.publish(cloud1);
        pub2.publish(cloud2);
        pub3.publish(cloud3);

        ros::spinOnce();
        loop_rate.sleep();
    }
    return 0;
}

visualization_msgs::Marker drawLines(std::vector<geometry_msgs::Point> links, int id, int c_color)
{
    visualization_msgs::Marker linksMarkerMsg;
    linksMarkerMsg.header.frame_id="/base_point_cloud";
    linksMarkerMsg.header.stamp=ros::Time::now();
    linksMarkerMsg.ns="link_marker";
    linksMarkerMsg.id = id;
    linksMarkerMsg.type = visualization_msgs::Marker::LINE_LIST;
    linksMarkerMsg.scale.x = 0.001;
    linksMarkerMsg.action  = visualization_msgs::Marker::ADD;
    linksMarkerMsg.lifetime  = ros::Duration(1000);
    std_msgs::ColorRGBA color;
//    color.r = 1.0f; color.g=.0f; color.b=.0f, color.a=1.0f;
    if(c_color == 1)
    {
        color.r = 1.0;
        color.g = 0.0;
        color.b = 0.0;
        color.a = 1.0;
    }
    else if(c_color == 2)
    {
        color.r = 0.0;
        color.g = 1.0;
        color.b = 0.0;
        color.a = 1.0;
    }
    else
    {
        color.r = 0.0;
        color.g = 0.0;
        color.b = 1.0;
        color.a = 1.0;
    }
    std::vector<geometry_msgs::Point>::iterator linksIterator;
    for(linksIterator = links.begin();linksIterator != links.end();linksIterator++)
    {
        linksMarkerMsg.points.push_back(*linksIterator);
        linksMarkerMsg.colors.push_back(color);
    }
   return linksMarkerMsg;
}
