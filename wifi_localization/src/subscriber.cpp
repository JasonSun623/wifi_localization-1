//
// Created by 2scholz on 01.08.16.
//

#include "subscriber.h"
#include <fstream>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>

Subscriber::Subscriber(ros::NodeHandle &n, double threshold, bool user_input, float map_resolution, std::string path_to_csv) :
  threshold_(threshold), user_input_(user_input), record_user_input_(false), n_(n), maps(n, path_to_csv), record_(false), record_next_(false)
{
  ROS_INFO("Threshold at: %f",threshold_);

  max_weight_sub_ = new message_filters::Subscriber<wifi_localization::MaxWeight>(n, "max_weight", 100);
  pose_sub_ = new message_filters::Subscriber<geometry_msgs::PoseWithCovarianceStamped>(n, "amcl_pose", 100);
  wifi_data_sub_ = n.subscribe("wifi_state", 1, &Subscriber::wifiCallbackMethod, this);

  sync_ = new message_filters::Synchronizer<g_sync_policy>(g_sync_policy(100), *max_weight_sub_, *pose_sub_);
  sync_->registerCallback(boost::bind(&Subscriber::amclCallbackMethod, this, _1, _2));

  recording_service = n.advertiseService("start_stop_recording", &Subscriber::recording, this);
  record_next_signal_service = n.advertiseService("record_next_signal", &Subscriber::record_next_signal, this);
}

void Subscriber::recordNext()
{
  record_user_input_ = true;
}

bool& Subscriber::isRecording()
{
  return record_user_input_;
}

//The callback method
void Subscriber::amclCallbackMethod(const wifi_localization::MaxWeight::ConstPtr &max_weight_msg,
                                    const geometry_msgs::PoseWithCovarianceStamped::ConstPtr &pose_msg)
{
  pos_x_ = pose_msg->pose.pose.position.x;
  pos_y_ = pose_msg->pose.pose.position.y;
  max_weight_ = max_weight_msg->max_weight;
}

void Subscriber::wifiCallbackMethod(const wifi_localization::WifiState::ConstPtr& wifi_data_msg)
{
  // Only record the data if max_weight is big enough and if user input mode is activated only if the user pressed the key to record.
  if ((max_weight_ > threshold_ && !(user_input_ && !record_user_input_)) || (max_weight_ > threshold_ && record_) || (max_weight_ > threshold_ && record_next_))
  {
    for (int i = 0; i < wifi_data_msg->macs.size(); i++)
    {
      std::string mac_name = wifi_data_msg->macs.at(i);
      double wifi_dbm = wifi_data_msg->strengths.at(i);

      maps.add_data(mac_name, pos_x_, pos_y_, wifi_dbm);

    }
    if(record_user_input_)
    {
      record_user_input_ = false;
      ROS_INFO("Recording successful.");
    }
    if(record_next_)
    {
      record_next_ = false;
      ROS_INFO("Recording successful.");
    }
  }
}

bool Subscriber::recording(std_srvs::SetBool::Request &req, std_srvs::SetBool::Response &res)
{
  record_ = req.data;
  res.success = true;
  return true;
}

bool Subscriber::record_next_signal(std_srvs::Trigger::Response &req, std_srvs::Trigger::Response &res)
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