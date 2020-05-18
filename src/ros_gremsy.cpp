#include <unistd.h>
#include <ros/ros.h>
#include <sensor_msgs/Imu.h>
#include <dynamic_reconfigure/server.h>
#include <ros_gremsy/ROSGremsyConfig.h>
#include <boost/bind.hpp>
#include "gimbal_interface.h"
#include "serial_port.h"

class GimbalNode
{
public:
    // Params: (public node handler (for e.g. callbacks), private node handle (for e.g. dynamic reconfigure))
    GimbalNode(ros::NodeHandle nh, ros::NodeHandle pnh);
private:
    // Dynamic reconfigure callback
    void callbackRC(ros_gremsy::ROSGremsyConfig &config, uint32_t level);
    void gimbalStateTimerCallback(const ros::TimerEvent& event);
    sensor_msgs::Imu convertMavlinkToROSMessage(mavlink_raw_imu_t message);
    control_gimbal_axis_input_mode_t convertIntToAxisInputMode(int mode);

    // Gimbal SDK
    Gimbal_Interface* gimbal_interface_;
    // Serial Interface
    Serial_Port* serial_port_;
    // Current config
    ros_gremsy::ROSGremsyConfig config_;
    // Publishers
    ros::Publisher imu_pub;
};

GimbalNode::GimbalNode(ros::NodeHandle nh, ros::NodeHandle pnh)
{
    // Dynamic reconfigure stuff
    dynamic_reconfigure::Server<ros_gremsy::ROSGremsyConfig> server(pnh);
    dynamic_reconfigure::Server<ros_gremsy::ROSGremsyConfig>::CallbackType f;
    f = boost::bind(&GimbalNode::callbackRC, this, _1, _2);
    server.setCallback(f);

    imu_pub = nh.advertise<sensor_msgs::Imu>("/gimbal/imu/data", 10);

    int baudrate;
    std::string device;

    serial_port_ = new Serial_Port(config_.device.c_str(), config_.baudrate);
    gimbal_interface_ = new Gimbal_Interface(serial_port_);

    serial_port_->start();
	gimbal_interface_->start();

    ros::Duration(1.0).sleep(); // Wait until everythins is started (Only just in case)

    ///////////////////
    // Config Gimbal //
    ///////////////////

    // Check if gimbal is on
    if(gimbal_interface_->get_gimbal_status().mode == GIMBAL_STATE_OFF)
    {
        // Turn on gimbal
        ROS_INFO("TURN_ON!\n");
        gimbal_interface_->set_gimbal_motor_mode(TURN_ON);
    }

    // Wait until the gimbal is on
    while (gimbal_interface_->get_gimbal_status().mode != GIMBAL_STATE_ON)
    {
        ros::Duration(0.05).sleep();
    }

    // Set gimbal control modes

    control_gimbal_mode_t mode;

    switch(config_.gimbal_mode) {
        case 0 : mode = GIMBAL_OFF; break;
        case 1 : mode = LOCK_MODE; break;
        case 2 : mode = FOLLOW_MODE; break;
    }

    gimbal_interface_->set_gimbal_mode(mode);

    // Set modes for each axis

    control_gimbal_axis_mode_t tilt_axis_mode, roll_axis_mode, pan_axis_mode;

    tilt_axis_mode.input_mode = convertIntToAxisInputMode(config_.tilt_axis_input_mode);
    tilt_axis_mode.stabilize = config_.tilt_axis_stabilize;

    roll_axis_mode.input_mode = convertIntToAxisInputMode(config_.roll_axis_input_mode);
    roll_axis_mode.stabilize = config_.roll_axis_stabilize;

    pan_axis_mode.input_mode = convertIntToAxisInputMode(config_.pan_axis_input_mode);
    pan_axis_mode.stabilize = config_.pan_axis_stabilize;

    gimbal_interface_->set_gimbal_axes_mode(tilt_axis_mode, roll_axis_mode, pan_axis_mode);

    ros::Timer timer = nh.createTimer(
        ros::Duration(1/config_.state_poll_rate),
        &GimbalNode::gimbalStateTimerCallback, this);

    ros::spin();
}

void GimbalNode::gimbalStateTimerCallback(const ros::TimerEvent& event)
{
    mavlink_raw_imu_t imu_mav = gimbal_interface_->get_gimbal_raw_imu();
    imu_mav.time_usec = gimbal_interface_->get_gimbal_time_stamps().raw_imu; // TODO implement rostime
    sensor_msgs::Imu imu_ros_mag = convertMavlinkToROSMessage(imu_mav);
    imu_pub.publish(imu_ros_mag);
}

sensor_msgs::Imu GimbalNode::convertMavlinkToROSMessage(mavlink_raw_imu_t message)
{
    sensor_msgs::Imu imu_message;

    // Set accelaration data
    imu_message.linear_acceleration.x = message.xacc;
    imu_message.linear_acceleration.y = message.yacc;
    imu_message.linear_acceleration.z = message.zacc;

    // Set gyro data
    imu_message.angular_velocity.x = message.xgyro;
    imu_message.angular_velocity.y = message.ygyro;
    imu_message.angular_velocity.z = message.zgyro;

    return imu_message;
}

control_gimbal_axis_input_mode_t GimbalNode::convertIntToAxisInputMode(int mode)
{
    switch(config_.gimbal_mode) {
        case 0 : return CTRL_ANGLE_BODY_FRAME;
        case 1 : return CTRL_ANGULAR_RATE;
        case 2 : return CTRL_ANGLE_ABSOLUTE_FRAME;
    }
}

void GimbalNode::callbackRC(ros_gremsy::ROSGremsyConfig &config, uint32_t level) {
    config_ = config;
}

int main(int argc, char **argv)
{
    // Init
    ros::init(argc, argv, "ros_gremsy");
    ros::NodeHandle nh;
    GimbalNode n(nh, nh);

    return 0;
}