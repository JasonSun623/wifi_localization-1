#include "wifi_data_collector/subscriber.h"
#include <fstream>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <geometry_msgs/Quaternion.h>

Subscriber::Subscriber(ros::NodeHandle &n, float map_resolution, std::string path_to_csv) :
  n_(n),
  maps(n, path_to_csv),
  record_(false),
  record_next_(false),
  stands_still_(false),
  record_only_stopped_(false),
  use_gps_(false),
  threshold_(1.0),
  pose_(),
  wifi_data_since_stop_(0),
  initial_gps_x_(0.0),
  initial_gps_y_(0.0)
{
  recorded_since_stop.data = false;

  pose_.pose.pose.position.x = 0.0;
  pose_.pose.pose.position.y = 0.0;
  pose_.pose.pose.position.z = 0.0;
  pose_.pose.pose.orientation.x = 0.0;
  pose_.pose.pose.orientation.y = 0.0;
  pose_.pose.pose.orientation.z = 0.0;
  pose_.pose.pose.orientation.w = 0.0;

  gps_pose_.pose.pose.position.x = -1.0;
  gps_pose_.pose.pose.position.y = -1.0;
  start_gps_pose_.pose.pose.position.x = -1.0;
  start_gps_pose_.pose.pose.position.y = -1.0;

  n.param("/wifi_data_collector/threshold", threshold_, threshold_);
  n.param("/wifi_data_collector/record_only_stopped", record_only_stopped_, record_only_stopped_);
  n.param("/wifi_data_collector/path_to_csv", path_to_csv, path_to_csv);
  n.param("/wifi_data_collector/record_wifi_signals", record_, record_);
  n.param("/wifi_data_collector/use_gps", use_gps_, use_gps_);
  n.param("/wifi_data_collector/initial_gps_x", initial_gps_x_, initial_gps_x_);
  n.param("/wifi_data_collector/initial_gps_y", initial_gps_y_, initial_gps_y_);

  // maps.add_csv_data(path_to_csv);

  ROS_INFO("Threshold at: %f",threshold_);

  max_weight_sub_ = new message_filters::Subscriber<wifi_localization::MaxWeight>(n, "max_weight", 100);
  pose_sub_ = new message_filters::Subscriber<geometry_msgs::PoseWithCovarianceStamped>(n, "amcl_pose", 100);
  wifi_data_sub_ = n.subscribe("wifi_data", 1, &Subscriber::wifiCallbackMethod, this);
  odom_sub_ = n.subscribe("odom",1, &Subscriber::odomCallbackMethod, this);
  gps_sub_ = n.subscribe("gps_odom", 1, &Subscriber::gpsCallbackMethod, this);

  recorded_since_stop_pub_ = n.advertise<std_msgs::Bool>("recorded_since_stop", 1, true);

  sync_ = new message_filters::Synchronizer<g_sync_policy>(g_sync_policy(100), *max_weight_sub_, *pose_sub_);
  sync_->registerCallback(boost::bind(&Subscriber::amclCallbackMethod, this, _1, _2));

  recording_service = n.advertiseService("record_wifi_signals", &Subscriber::recording, this);
  record_next_signal_service = n.advertiseService("record_next_wifi_signal", &Subscriber::record_next_signal, this);
}

//The callback method
void Subscriber::amclCallbackMethod(const wifi_localization::MaxWeight::ConstPtr &max_weight_msg,
                                    const geometry_msgs::PoseWithCovarianceStamped::ConstPtr &pose_msg)
{
  pose_ = *pose_msg;
  max_weight_ = max_weight_msg->max_weight;
}

void Subscriber::gpsCallbackMethod(const nav_msgs::Odometry::ConstPtr &msg)
{
  if(start_gps_pose_.pose.pose.position.x == -1.0 && start_gps_pose_.pose.pose.position.y == -1.0)
    start_gps_pose_.pose = msg->pose;
  gps_pose_.pose.pose.position.x = msg->pose.pose.position.x - start_gps_pose_.pose.pose.position.x + initial_gps_x_;
  gps_pose_.pose.pose.position.y = msg->pose.pose.position.y - start_gps_pose_.pose.pose.position.y + initial_gps_y_;
}

void Subscriber::wifiCallbackMethod(const wifi_localization::WifiState::ConstPtr& wifi_data_msg)
{
  if(use_gps_)
  {
    for (int i = 0; i < wifi_data_msg->macs.size(); i++)
    {
      std::string mac_name = wifi_data_msg->macs.at(i);
      double wifi_dbm = wifi_data_msg->strengths.at(i);
      std::string ssid = wifi_data_msg->ssids.at(i);

      maps.add_data(wifi_data_msg->header.stamp.sec, mac_name, wifi_dbm, wifi_data_msg->channels.at(i), ssid, gps_pose_);
    }
  }

  // Only record the data if max_weight is big enough and if user input mode is activated only if the user pressed the key to record.
  else if ((((max_weight_ < threshold_ && record_) || (max_weight_ < threshold_ && record_next_)) && (!record_only_stopped_||(stands_still_ && wifi_data_since_stop_ > 1))) && !wifi_data_msg->macs.empty())
  {
    for (int i = 0; i < wifi_data_msg->macs.size(); i++)
    {
      std::string mac_name = wifi_data_msg->macs.at(i);
      double wifi_dbm = wifi_data_msg->strengths.at(i);
      std::string ssid = wifi_data_msg->ssids.at(i);

      maps.add_data(wifi_data_msg->header.stamp.sec, mac_name, wifi_dbm, wifi_data_msg->channels.at(i), ssid, pose_);
    }
    if(record_next_)
    {
      record_next_ = false;
      ROS_INFO("Recording successful.");
    }
    ROS_INFO("Recorded new data.");
    if(stands_still_)
    {
      recorded_since_stop.data = true;
      recorded_since_stop_pub_.publish(recorded_since_stop);
    }
  }
  if(stands_still_)
    wifi_data_since_stop_++;
}

void Subscriber::odomCallbackMethod(const nav_msgs::Odometry::ConstPtr &msg)
{
  stands_still_ =
          msg->twist.twist.linear.x == 0.0 && msg->twist.twist.linear.y == 0.0 && msg->twist.twist.angular.z == 0.0;
  if(!stands_still_)
  {
    wifi_data_since_stop_ = 0;
    recorded_since_stop.data = false;
    recorded_since_stop_pub_.publish(recorded_since_stop);
  }
}

bool Subscriber::recording(std_srvs::SetBool::Request &req, std_srvs::SetBool::Response &res)
{
  record_ = req.data;
  res.success = true;
  return true;
}

bool Subscriber::record_next_signal(std_srvs::Trigger::Request &req, std_srvs::Trigger::Response &res)
{
  if(record_ = true)
  {
    res.success = false;
    res.message = "Service failed, because the node is already recording.";
    return false;
  }
  record_next_ = true;
  res.success = true;
  return true;
}
