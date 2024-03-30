#include "ur_rtde/ur_rtde.h"

#include <ur_rtde/rtde_control_interface.h>
#include <ur_rtde/rtde_receive_interface.h>

#include <Eigen/Dense>
#include <memory>

#include <RobotUtilities/utilities.h>

URRTDE *URRTDE::pinstance = 0;

struct URRTDE::Implementation {
  std::shared_ptr<ur_rtde::RTDEControlInterface> rtde_control_ptr;
  std::shared_ptr<ur_rtde::RTDEReceiveInterface> rtde_receive_ptr;

  URRTDE::URRTDEConfig config{};

  std::vector<double> tcp_pose_feedback{};
  std::vector<double> tcp_pose_command{};
  RUT::Vector3d v_axis_receive{};
  RUT::Vector3d v_axis_control{};

  double dt_s{};

  RUT::TimePoint time0;

  Implementation();
  ~Implementation();

  bool initialize(RUT::TimePoint time0, const URRTDE::URRTDEConfig &config);
  bool getCartesian(double *pose_xyzq);
  bool setCartesian(const double *pose_xyzq);
  bool streamCartesian(const double *pose_xyzq);
  RUT::TimePoint rtde_init_period();
  void rtde_wait_period(RUT::TimePoint time_point);
};

URRTDE::Implementation::Implementation() {
  tcp_pose_feedback.resize(6);
  tcp_pose_command.resize(6);
}

URRTDE::Implementation::~Implementation() {
  std::cout << "[URRTDE] finishing.." << std::endl;
  delete pinstance;
}

bool URRTDE::Implementation::initialize(
    RUT::TimePoint time0, const URRTDE::URRTDEConfig &ur_rtde_config) {
  time0 = time0;
  config = ur_rtde_config;
  dt_s = 1. / config.rtde_frequency;

  /* Establish connection with UR */
  std::cout << "[URRTDE] Connecting to robot at " << config.robot_ip
            << std::endl;
  rtde_control_ptr = std::shared_ptr<ur_rtde::RTDEControlInterface>(
      new ur_rtde::RTDEControlInterface(config.robot_ip, config.rtde_frequency,
                                        {}, {}, config.rt_control_priority));
  rtde_receive_ptr = std::shared_ptr<ur_rtde::RTDEReceiveInterface>(
      new ur_rtde::RTDEReceiveInterface(config.robot_ip, config.rtde_frequency,
                                        {}, {}, {},
                                        config.rt_receive_priority));

  // Set application realtime priority
  ur_rtde::RTDEUtility::setRealtimePriority(config.interface_priority);
  std::cout << "[URRTDE] UR socket connection established.\n";
  return true;
}

bool URRTDE::Implementation::getCartesian(double *pose_xyzq) {
  tcp_pose_feedback = rtde_receive_ptr->getActualTCPPose();

  // convert from Euler to quaternion
  v_axis_receive[0] = tcp_pose_feedback[3];  // rx
  v_axis_receive[1] = tcp_pose_feedback[4];  // ry
  v_axis_receive[2] = tcp_pose_feedback[5];  // rz
  double angle = v_axis_receive.norm();
  Eigen::Quaterniond q(Eigen::AngleAxisd(angle, v_axis_receive.normalized()));

  pose_xyzq[0] = tcp_pose_feedback[0];
  pose_xyzq[1] = tcp_pose_feedback[1];
  pose_xyzq[2] = tcp_pose_feedback[2];
  pose_xyzq[3] = q.w();
  pose_xyzq[4] = q.x();
  pose_xyzq[5] = q.y();
  pose_xyzq[6] = q.z();

  // check safety
  if ((pose_xyzq[0] < config.robot_interface_config.safe_zone[0]) ||
      (pose_xyzq[0] > config.robot_interface_config.safe_zone[1]))
    return false;
  if ((pose_xyzq[1] < config.robot_interface_config.safe_zone[2]) ||
      (pose_xyzq[1] > config.robot_interface_config.safe_zone[3]))
    return false;
  if ((pose_xyzq[2] < config.robot_interface_config.safe_zone[4]) ||
      (pose_xyzq[2] > config.robot_interface_config.safe_zone[5]))
    return false;

  return true;
}

bool URRTDE::Implementation::setCartesian(const double *pose_xyzq_set) {
  assert(config.robot_interface_config.operationMode ==
         OPERATION_MODE_CARTESIAN);
  // convert quaternion to Euler
  double angle = 2.0 * acos(pose_xyzq_set[3]);
  v_axis_control << pose_xyzq_set[4], pose_xyzq_set[5], pose_xyzq_set[6];
  v_axis_control.normalize();
  v_axis_control *= angle;
  tcp_pose_command[0] = pose_xyzq_set[0];
  tcp_pose_command[1] = pose_xyzq_set[1];
  tcp_pose_command[2] = pose_xyzq_set[2];
  tcp_pose_command[3] = v_axis_control[0];
  tcp_pose_command[4] = v_axis_control[1];
  tcp_pose_command[5] = v_axis_control[2];
  return rtde_control_ptr->moveL(tcp_pose_command, config.linear_vel,
                            config.linear_acc);
}

bool URRTDE::Implementation::streamCartesian(const double *pose_xyzq_set) {
  assert(config.robot_interface_config.operationMode ==
         OPERATION_MODE_CARTESIAN);
  // convert quaternion to Euler
  double angle = 2.0 * acos(pose_xyzq_set[3]);
  v_axis_control << pose_xyzq_set[4], pose_xyzq_set[5], pose_xyzq_set[6];
  v_axis_control.normalize();
  v_axis_control *= angle;
  tcp_pose_command[0] = pose_xyzq_set[0];
  tcp_pose_command[1] = pose_xyzq_set[1];
  tcp_pose_command[2] = pose_xyzq_set[2];
  tcp_pose_command[3] = v_axis_control[0];
  tcp_pose_command[4] = v_axis_control[1];
  tcp_pose_command[5] = v_axis_control[2];
  return rtde_control_ptr->servoL(tcp_pose_command, config.linear_vel,
                             config.linear_acc, dt_s,
                             config.servoL_lookahead_time, config.servoL_gain);
}

RUT::TimePoint URRTDE::Implementation::rtde_init_period() {
  return rtde_control_ptr->initPeriod();
}

void URRTDE::Implementation::rtde_wait_period(RUT::TimePoint time_point) {
  rtde_control_ptr->waitPeriod(time_point);
}

URRTDE::URRTDE() : m_impl{std::make_unique<Implementation>()} {}

URRTDE::~URRTDE() {}

URRTDE *URRTDE::Instance() {
  if (pinstance == 0) {
    pinstance = new URRTDE();
  }
  return pinstance;
}

bool URRTDE::init(RUT::TimePoint time0, const URRTDEConfig &ur_rtde_config) {
  return m_impl->initialize(time0, ur_rtde_config);
}

bool URRTDE::getCartesian(double *pose_xyzq) {
  return m_impl->getCartesian(pose_xyzq);
}

bool URRTDE::setCartesian(const double *pose_xyzq) {
  return m_impl->setCartesian(pose_xyzq);
}

bool URRTDE::streamCartesian(const double *pose_xyzq) {
  return m_impl->streamCartesian(pose_xyzq);
}

RUT::TimePoint URRTDE::rtde_init_period() { return m_impl->rtde_init_period(); }

void URRTDE::rtde_wait_period(RUT::TimePoint time_point) {
  m_impl->rtde_wait_period(time_point);
}

bool URRTDE::getJoints(double *joints) {
  std::cerr << "[URRTDE] not implemented yet" << std::endl;
  return false;
}

bool URRTDE::setJoints(const double *joints) {
  std::cerr << "[URRTDE] not implemented yet" << std::endl;
  return false;
}