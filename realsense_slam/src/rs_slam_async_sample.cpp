
/********************************************************************************
INTEL CORPORATION PROPRIETARY INFORMATION This software is supplied under the
terms of a license agreement or nondisclosure agreement with Intel Corporation
and may not be copied or disclosed except in accordance with the terms of that
agreement.
Copyright(c) 2011-2016 Intel Corporation. All Rights Reserved.

*********************************************************************************/
#include "rs_slam_async_sample.h"

std::string pkgpath;
std::string trajectoryFilename;
std::string relocalizationFilename;
std::string occupancyFilename;
double resolution;

ros::Publisher pub_pose2d, pub_poseMatrix;
geometry_msgs::Pose2D pose2d;
ros::Publisher mapPub;

class slam_tracking_event_handler : public rs::slam::tracking_event_handler
{
public:
  slam_tracking_event_handler()
  {
    ROS_INFO("tracking_event_handler");
  }
  void on_restart()
  {
    ROS_INFO("Restart--------------------------------");
    fflush(stdout);
  }
  ~slam_tracking_event_handler()
  {
    ROS_INFO("deconstructure");
  }
};
void ConvertToPG(rs::slam::PoseMatrix4f & pose, rs::slam::tracking_accuracy trackingAccuracy, Eigen::Vector3f & gravity, stRobotPG & robotPG)
{
  Eigen::Vector3f g = gravity;
  Eigen::Vector3f g_startG(0.0f, 1.0f, 0.0f);
  Eigen::Vector3f x3D(1.0f, 0.0f, 0.0f);
  Eigen::Vector3f y3D(0.0f, 1.0f, 0.0f);
  Eigen::Vector3f z3D(0.0f, 0.0f, 1.0f);
  Eigen::Vector3f x3D_(pose.at(0, 0), pose.at(0, 1), pose.at(0, 2));
  Eigen::Vector3f y3D_(pose.at(1, 0), pose.at(1, 1), pose.at(1, 2));
  Eigen::Vector3f z3D_(pose.at(2, 0), pose.at(2, 1), pose.at(2, 2));
  Eigen::Vector3f z3DTo_(z3D.dot(x3D_), z3D.dot(y3D_), z3D.dot(z3D_));
  Eigen::Vector3f ProjX = g.cross(z3D);
  Eigen::Vector3f ProjY = ProjX.cross(g);
  Eigen::Vector2f v(z3DTo_.dot(ProjX), z3DTo_.dot(ProjY));
  Eigen::Vector2f v_(z3D.dot(ProjX), z3D.dot(ProjY));
  float theta1 = atan2(v.x(), v.y());
  float theta2 = atan2(v_.x(), v_.y());
  float theta = theta2 - theta1;
  if (theta < -CV_PI)
  {
    theta += 2 * CV_PI;
  }
  else if (theta >= CV_PI)
  {
    theta -= 2 * CV_PI;
  }
  Eigen::Vector3f origin_(pose.at(0, 3), pose.at(1, 3), pose.at(2, 3));
  Eigen::Vector3f ProjX0 = g_startG.cross(z3D);
  Eigen::Vector3f ProjY0 = ProjX0.cross(g_startG);
  float x = origin_.dot(ProjX0);
  float y = origin_.dot(ProjY0);
  robotPG.initdefault();
  robotPG.theta = theta;
  robotPG.x = x;
  robotPG.y = y;
}

IplImage * ipNavMap = NULL;
std::vector< stRobotPG > g_robotPGStack;
std::vector< CvPoint > g_relocalizationPointStack;
class slam_event_handler : public rs::core::video_module_interface::processing_event_handler
{
public:
  std::shared_ptr< rs::slam::occupancy_map > occ_map;
  slam_event_handler()
  {
    std::cout<<"created.........."<<std::endl;
  }
  void module_output_ready(rs::core::video_module_interface * sender, rs::core::correlated_sample_set * sample)
  {
    rs::slam::slam *pSP = dynamic_cast< rs::slam::slam * >(sender);
    const rs::slam::tracking_accuracy trackingAccuracy = pSP->get_tracking_accuracy();
    rs::slam::PoseMatrix4f cameraPose; 
    pSP->get_camera_pose(cameraPose);
    rs_slam_test::PoseMatrix matrix_msg;
    for (int i = 0; i < 4; i++)
    {
      for (int j = 0; j < 4; j++)
      {
        matrix_msg.matrix[i*4+j] = cameraPose.at(i,j); 
      }
    }
    pub_poseMatrix.publish(matrix_msg);
    Eigen::Vector3f gravity = Eigen::Vector3f(0, 1, 0);
    stRobotPG robotPG;
    ConvertToPG(cameraPose, trackingAccuracy, gravity, robotPG);
    pose2d.x = robotPG.x;
    pose2d.y = robotPG.y;
    pose2d.theta = robotPG.theta;
    pub_pose2d.publish(pose2d);
    g_robotPGStack.push_back(robotPG);
    int wmap = 512;
    int hmap = 512;
    if (!occ_map) {
      occ_map = pSP->create_occupancy_map(wmap * hmap);
      std::cout << " creating occupancy map: resolution: " << pSP->get_occupancy_map_resolution() << std::endl;
    }
    if (ipNavMap == NULL)
    {
      ipNavMap = cvCreateImage(cvSize(wmap, hmap), 8, 1);
      cvSet(ipNavMap, 10, NULL);
    }
    int status = pSP->get_occupancy_map_update(occ_map);
    int count = occ_map->get_tile_count();
    nav_msgs::OccupancyGrid map_msg;
    if (status >= 0 && count > 0)  
    {
      const int32_t* map = occ_map->get_tile_coordinates();
      for (int i = 0; i < count; i++)
      {
        int _x = map[3 * i] + wmap / 2;
        int _y = map[3 * i + 1] + hmap / 2;
        if (_x >= 0 && _x < wmap)
        {
          if (_y >= 0 && _y < hmap)
          {
            int V = map[3 * i + 2];
            ipNavMap->imageData[wmap*_y + _x] = V;
          }
        }                                   
      }
    }
    std::vector<signed char> vMap(ipNavMap->imageData, ipNavMap->imageData + wmap*hmap);
    map_msg.data = vMap;
    map_msg.info.resolution = resolution;     
    map_msg.info.width      = wmap;
    map_msg.info.height     = hmap;               
    mapPub.publish(map_msg);
  }

  ~slam_event_handler()
  {
    ROS_INFO("deconstructure event handler");
  }
};

PLUGINLIB_EXPORT_CLASS(rs_slam_test::SNodeletSlam, nodelet::Nodelet)
namespace rs_slam_test
{
  SNodeletSlam::SNodeletSlam(){}
  SNodeletSlam::~SNodeletSlam(){}
  void SNodeletSlam::onInit()
  {
    nh = getMTNodeHandle();
    pub_pose2d = nh.advertise< geometry_msgs::Pose2D >("pose2d", 2, true);
    mapPub = nh.advertise< nav_msgs::OccupancyGrid >("map", 1, true);
    pub_poseMatrix = nh.advertise< rs_slam_test::PoseMatrix >("poseMatrix", 1, true);
    pkgpath = ros::package::getPath("rs_slam_test") + "/";
    ros::NodeHandle pnh = getPrivateNodeHandle();
    pnh.param< double >("resolution", resolution, 0.05);
    pnh.param< std::string >("trajectoryFilename", trajectoryFilename, "trajectory.ppm");
    pnh.param< std::string >("relocalizationFilename", relocalizationFilename, "relocalization.bin");
    pnh.param< std::string >("occupancyFilename", occupancyFilename, "occupancy.bin");
    std::string fisheyeCameraInfoStream = "camera/fisheye/camera_info";
    std::string depthCameraInfoStream = "camera/depth/camera_info";
    sub_depthInfo = std::shared_ptr< message_filters::Subscriber< sensor_msgs::CameraInfo > >(new message_filters::Subscriber< sensor_msgs::CameraInfo >(nh, depthCameraInfoStream, 1));
    sub_fisheyeInfo = std::shared_ptr< message_filters::Subscriber<sensor_msgs::CameraInfo > >(new message_filters::Subscriber< sensor_msgs::CameraInfo >(nh, fisheyeCameraInfoStream, 1));

    info_TimeSynchronizer = std::shared_ptr< message_filters::TimeSynchronizer< sensor_msgs::CameraInfo, sensor_msgs::CameraInfo > >(new message_filters::TimeSynchronizer< sensor_msgs::CameraInfo, sensor_msgs::CameraInfo >( * sub_depthInfo, * sub_fisheyeInfo, 40));
    info_TimeSynchronizer->registerCallback(boost::bind( & SNodeletSlam::cameraInfoCallback, this, _1, _2));

    client_imu = nh.serviceClient< realsense_ros_camera::GetIMUInfo >("/camera/get_imu_info");
    client_fisheye = nh.serviceClient< realsense_ros_camera::GetFExtrinsics >("camera/get_fe_extrinsics");
                
    actual_config = {};
    ROS_INFO("end of onInit");
  }//end onInit
  void SNodeletSlam::cameraInfoCallback(const sensor_msgs::CameraInfoConstPtr& depthCameraInfo,const sensor_msgs::CameraInfoConstPtr& fisheyeCameraInfo)
  {
    sub_depthInfo->unsubscribe();
    sub_fisheyeInfo->unsubscribe();
    std::unique_ptr<rs::slam::slam> slam(new rs::slam::slam());     
    slam->set_occupancy_map_resolution(resolution);
    slam_event_handler scenePerceptionEventHandler;
    slam->register_event_handler(&scenePerceptionEventHandler);
    slam_tracking_event_handler trackingEventHandler;
    slam->register_tracking_event_handler(&trackingEventHandler);
    supported_config = {};
    if (slam->query_supported_module_config(0, supported_config) < rs::core::status_no_error)
    {
      std::cerr<<"error : failed to query the first supported module configuration" << std::endl;
      return ;
    }
    //end init supported config
    rs::core::intrinsics  depth_intrinsics, fisheye_intrinsics;
    SNodeletSlam::setCalibrationData(depthCameraInfo, depth_intrinsics);
    SNodeletSlam::setCalibrationData(fisheyeCameraInfo, fisheye_intrinsics);
    std::map< rs::core::stream_type, rs::core::intrinsics > intrinsics;
    intrinsics[rs::core::stream_type::depth] = depth_intrinsics;
    intrinsics[rs::core::stream_type::fisheye] = fisheye_intrinsics;
    SNodeletSlam::setStreamConfigIntrin(rs::core::stream_type::depth, intrinsics);
    SNodeletSlam::setStreamConfigIntrin(rs::core::stream_type::fisheye, intrinsics);	
    /* add extrins and motion intrin*/	
    realsense_ros_camera::GetFExtrinsics srv_fe;
    if (client_fisheye.call(srv_fe))
    {
      NODELET_INFO("fisheye extrinsics got");	
      rs::core::extrinsics fe2motion, dep2fe;
      SNodeletSlam::setExtrinData(srv_fe.response.fisheye, fe2motion);
      SNodeletSlam::setExtrinData(srv_fe.response.depth2fisheye, dep2fe);
      actual_config[rs::core::stream_type::fisheye].extrinsics_motion = fe2motion;
      actual_config[rs::core::stream_type::fisheye].extrinsics = dep2fe;
    }
    else
    {
      NODELET_ERROR("fisheye extrinsics missed");
    }
    realsense_ros_camera::GetIMUInfo srv_imu;
    if (client_imu.call(srv_imu))
    {
      NODELET_INFO("imu info got");
      rs::core::motion_device_intrinsics acc, gyro;
      SNodeletSlam::setMotionData(srv_imu.response.accel, acc);
      SNodeletSlam::setMotionData(srv_imu.response.gyro, gyro);
      std::map< rs::core::motion_type, rs::core::motion_device_intrinsics > motion_intrinsics;
      motion_intrinsics[rs::core::motion_type::accel] = acc;
      motion_intrinsics[rs::core::motion_type::gyro] = gyro;
      SNodeletSlam::setMotionConfigIntrin(rs::core::motion_type::accel, motion_intrinsics);
      SNodeletSlam::setMotionConfigIntrin(rs::core::motion_type::gyro, motion_intrinsics);
    }
    else{
      NODELET_ERROR("imu info missed");
    }
   //set actual config
    if (slam->set_module_config(actual_config) < rs::core::status_no_error)
    {
      NODELET_ERROR("error : failed to set the enabled module configuration");
      return ;
    }
    //slam->load_relocalization_map(relocalizationFilename);
    sub.onInit(nh, slam.get());
    sub.subscribeStreamMessages();
    sub.subscribeMotion();
    ros::Rate r(30);
    while (ros::ok()) {
      r.sleep();
    }
    std::cout << "occupancy ppm:" << slam->save_occupancy_map_as_ppm(pkgpath + trajectoryFilename, true) << std::endl;
    std::cout << "Save Relocalozation:" << slam->save_relocalization_map(pkgpath + relocalizationFilename) << std::endl;
    std::cout << "Save occupancy:" << slam->save_occupancy_map(pkgpath + occupancyFilename) << std::endl;
    slam->flush_resources();
  }//end of callback
  void SNodeletSlam::setCalibrationData(const sensor_msgs::CameraInfoConstPtr & cameraInfoMsg, rs::core::intrinsics & cameraInfo)
  {
    NODELET_INFO("setCalibrationData ");
    cameraInfo.width = cameraInfoMsg->width;
    cameraInfo.height = cameraInfoMsg->height;
    cameraInfo.fx = cameraInfoMsg->K[0];
    cameraInfo.fy = cameraInfoMsg->K[4];
    cameraInfo.ppx = cameraInfoMsg->K[2];
    cameraInfo.ppy = cameraInfoMsg->K[5];
    for (int i = 0; i < 5; i++)
      cameraInfo.coeffs[i] = (float)cameraInfoMsg->D[i];
    std::string distortion_model = cameraInfoMsg->distortion_model;
    if (distortion_model.compare("modified_brown_conrady") == 0)
    {
      cameraInfo.model = rs::core::distortion_type::modified_brown_conrady;
    }
    else if (distortion_model.compare("none") == 0)
    {
      cameraInfo.model = rs::core::distortion_type::none;
    }
    else if (distortion_model.compare("distortion_ftheta") == 0)
    {
      cameraInfo.model = rs::core::distortion_type::distortion_ftheta;
    }
    else
    {
      cameraInfo.model = rs::core::distortion_type::none;
    }
  }
  void SNodeletSlam::setStreamConfigIntrin(rs::core::stream_type stream, std::map< rs::core::stream_type, rs::core::intrinsics > intrinsics)
  {
    auto & supported_stream_config = supported_config[stream];
    if(!supported_stream_config.is_enabled || supported_stream_config.size.width != intrinsics[stream].width || supported_stream_config.size.height != intrinsics[stream].height)
    {
      ROS_INFO("size of stream is not supported by slam");
      return;
    }
    rs::core::video_module_interface::actual_image_stream_config &actual_stream_config = actual_config[stream];
    actual_config[stream].size.width = intrinsics[stream].width;
    actual_config[stream].size.height= intrinsics[stream].height;
    actual_stream_config.frame_rate = supported_stream_config.frame_rate;
    actual_stream_config.intrinsics = intrinsics[stream];
    actual_stream_config.is_enabled = true;
  }//end of setStremConfigIntrin

  void SNodeletSlam::setMotionData(realsense_ros_camera::IMUInfo & imu_res, rs::core::motion_device_intrinsics & motion_intrin)
  {
    int index = 0;
    for (int i = 0; i < 3; ++i)
    {
      for (int j = 0; j < 4; ++j)
      {
        motion_intrin.data[i][j] = imu_res.data[index];
        ++index;
      }
      motion_intrin.noise_variances[i] = imu_res.noise_variances[i];
      motion_intrin.bias_variances[i] = imu_res.bias_variances[i];
    }
  }//end of setMotionData
  void SNodeletSlam::setMotionConfigIntrin(rs::core::motion_type motion, std::map< rs::core::motion_type, rs::core::motion_device_intrinsics > motion_intrinsics)
  {
    actual_config[motion].is_enabled = true;
    actual_config[motion].intrinsics = motion_intrinsics[motion];
  }//end of setMotionConfigIntrin
  void SNodeletSlam::setExtrinData(realsense_ros_camera::Extrinsics & fe_res, rs::core::extrinsics & extrinsics)
  {
    for (int i = 0; i < 9; ++i)
    {
      extrinsics.rotation[i] = fe_res.rotation[i];
      if (i < 3)
      {
        extrinsics.translation[i] = fe_res.translation[i];
      }
    }
  }//end of setExtrinData
}//end namespace
