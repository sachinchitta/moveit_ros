/*********************************************************************
*
* Software License Agreement (BSD License)
*
*  Copyright (c) 2012, Willow Garage, Inc.
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of Willow Garage, Inc. nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*
* Author: Sachin Chitta
*********************************************************************/

#include <kinematics_reachability/kinematics_reachability.h>

namespace kinematics_reachability
{

KinematicsReachability::KinematicsReachability():node_handle_("~")
{
}

bool KinematicsReachability::initialize()
{
  visualization_publisher_ = node_handle_.advertise<visualization_msgs::MarkerArray>("workspace_markers",0,true);
  workspace_publisher_ = node_handle_.advertise<kinematics_reachability::WorkspacePoints>("workspace",0,true);
  robot_trajectory_publisher_ = node_handle_.advertise<moveit_msgs::DisplayTrajectory>("display_state",0,true);
  tool_offset_.setIdentity();
  tool_offset_inverse_.setIdentity();
  if(!kinematics_solver_.initialize())
  {
    ROS_ERROR("Could not initialize solver");  
    return false;    
  }

  while(!kinematics_solver_.isActive())
  {
    ros::Duration sleep_wait(1.0);
    sleep_wait.sleep();
  }
  
  node_handle_.param("cache_origin/x", default_cache_options_.origin.x, 0.0);
  node_handle_.param("cache_origin/y", default_cache_options_.origin.y, 0.0);
  node_handle_.param("cache_origin/z", default_cache_options_.origin.z, 0.0);

  node_handle_.param("cache_workspace_size/x", default_cache_options_.workspace_size[0], 2.0);
  node_handle_.param("cache_workspace_size/y", default_cache_options_.workspace_size[1], 2.0);
  node_handle_.param("cache_workspace_size/z", default_cache_options_.workspace_size[2], 2.0);

  node_handle_.param("cache_workspace_resolution/x", default_cache_options_.resolution[0], 0.01);
  node_handle_.param("cache_workspace_resolution/y", default_cache_options_.resolution[1], 0.01);
  node_handle_.param("cache_workspace_resolution/z", default_cache_options_.resolution[2], 0.01);
  int tmp;  
  node_handle_.param("cache_num_solutions_per_point", tmp, 1);
  default_cache_options_.max_solutions_per_grid_location = (unsigned int) tmp;
  
  if(!node_handle_.getParam("cache_filename", cache_filename_))
  {
    ROS_ERROR("Must specify cache_filename");
    return false;
  }  
  node_handle_.param<double>("cache_timeout",default_cache_timeout_,60.0);  

  // Visualization
  node_handle_.param("arrow_marker_scale/x", arrow_marker_scale_.x, 0.08);
  node_handle_.param("arrow_marker_scale/y", arrow_marker_scale_.y, 0.02);
  node_handle_.param("arrow_marker_scale/z", arrow_marker_scale_.z, 0.02);

  double sphere_marker_radius;
  node_handle_.param("sphere_marker_radius", sphere_marker_radius, 0.02);
  sphere_marker_scale_.x = sphere_marker_radius;
  sphere_marker_scale_.y = sphere_marker_radius;
  sphere_marker_scale_.z = sphere_marker_radius;

  initializeColor("reachable_color",reachable_color_,0.0,1.0,0.0);
  initializeColor("unreachable_color",unreachable_color_,1.0,0.0,0.0);
  initializeColor("evaluating_color",evaluating_color_,0.0,0.0,1.0);

  first_time_ = true;  
  use_cache_ = false;  
  ROS_INFO("Initialized: Waiting for request");  
  return true;  
}

void KinematicsReachability::initializeColor(const std::string &color_name,
                                             std_msgs::ColorRGBA &color_msg,
                                             double default_r,
                                             double default_g,
                                             double default_b)
{
  double color;  
  node_handle_.param(color_name+"/r", color, default_r);
  color_msg.r = color;  
  node_handle_.param(color_name+"/g", color, default_g);
  color_msg.g = color;  
  node_handle_.param(color_name+"/b", color, default_b);
  color_msg.b = color; 
  color_msg.a = 1.0;  
}


/////////////////////////////////////////////////////////////////////////////////////////////
// Public API Workspace Functions ///////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////


bool KinematicsReachability::computeWorkspace(kinematics_reachability::WorkspacePoints &workspace, 
                                              const geometry_msgs::Pose &tool_frame_offset,
                                              bool visualize)
{
  if(first_time_)
  {  
    if(generateCache(workspace.group_name,default_cache_timeout_,default_cache_options_,cache_filename_))
      use_cache_ = true;    
    first_time_ = false;    
  }
  
  setToolFrameOffset(tool_frame_offset);
  if(!sampleUniform(workspace))
    return false;
  if(visualize)
    visualizeWorkspaceSamples(workspace,"online");
  
  findIKSolutions(workspace,visualize);
  return true;
}

bool KinematicsReachability::computeWorkspace(kinematics_reachability::WorkspacePoints &workspace,
                                              bool visualize)
{
  geometry_msgs::Pose pose;
  pose.orientation.w = 1.0;
  return computeWorkspace(workspace,pose,visualize);
}

bool KinematicsReachability::getOnlyReachableWorkspace(kinematics_reachability::WorkspacePoints &workspace, 
                                                       const geometry_msgs::Pose &tool_frame_offset,
                                                       bool visualize)
{
  if(!computeWorkspace(workspace, tool_frame_offset,visualize))
    return false;
  removeUnreachableWorkspace(workspace);
  return true;
}


/////////////////////////////////////////////////////////////////////////////////////////////
// IK Functions //////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////

kinematics_reachability::WorkspacePoints KinematicsReachability::computeRedundantSolutions(const std::string &group_name,
                                                                                           const geometry_msgs::PoseStamped &pose_stamped,
                                                                                           double timeout,
                                                                                           bool visualize_workspace)
{
  geometry_msgs::Pose tool_offset;
  tool_offset.orientation.w = 1.0;
  return computeRedundantSolutions(group_name,pose_stamped,timeout,tool_offset);  
}

kinematics_reachability::WorkspacePoints KinematicsReachability::computeRedundantSolutions(const std::string &group_name,
                                                                                           const geometry_msgs::PoseStamped &pose_stamped,
                                                                                           double timeout,
                                                                                           const geometry_msgs::Pose &tool_offset,
                                                                                           bool visualize_workspace)
{
    kinematics_reachability::WorkspacePoints workspace;
    workspace.header = pose_stamped.header;
    workspace.group_name = group_name;    
    setToolFrameOffset(tool_offset);

    bool use_cache_old_value = use_cache_;
    use_cache_ = false;    
    ros::WallTime start_time = ros::WallTime::now();    
    unsigned int counter =0;    
    while(ros::WallTime::now()-start_time <= ros::WallDuration(timeout) && ros::ok())
    {
      moveit_msgs::MoveItErrorCodes error_code;
      moveit_msgs::RobotState solution;
      kinematics_reachability::WorkspacePoint point;
      geometry_msgs::PoseStamped desired_pose = pose_stamped;    
      findIK(group_name,desired_pose,error_code,solution);
      point.pose = desired_pose.pose;
      point.solution_code = error_code;
      if(error_code.val == error_code.SUCCESS)
      {
        ROS_INFO("Succeeded");        
        point.robot_state = solution;
        if(visualize_workspace)
        {
          visualize(point,workspace.header,counter,"");        
          counter++;          
        }      
      }
      workspace.points.push_back(point);      
    }    
    use_cache_ = use_cache_old_value;    
    return workspace;    
}


void KinematicsReachability::findIKSolutions(kinematics_reachability::WorkspacePoints &workspace,
                                             bool visualize_workspace)
{  
  for(unsigned int i=0; i < workspace.points.size(); ++i)
  {
    // Show the point being evaluated
    if(visualize_workspace)
    {
      visualize(workspace.points[i],workspace.header,i,"online",true);      
    }    

    geometry_msgs::PoseStamped ik_pose;
    ik_pose.pose = workspace.points[i].pose;
    ik_pose.header = workspace.header;
    moveit_msgs::MoveItErrorCodes error_code;
    moveit_msgs::RobotState solution;
    
    findIK(workspace.group_name,ik_pose,error_code,solution);

    workspace.points[i].pose = ik_pose.pose;    
    workspace.points[i].solution_code = error_code;

    if(error_code.val == error_code.SUCCESS)
    {      
      ROS_DEBUG("Solution   : Point %d of %d",(int) i,(int) workspace.points.size());
      workspace.points[i].robot_state = solution;
      kinematics_cache_->addToCache(workspace.points[i].pose,solution.joint_state.position,true);    
    }
    else
    {
      ROS_ERROR("No Solution: Point %d of %d",(int) i,(int) workspace.points.size());
    }
    if(visualize_workspace)
    {
      visualize(workspace.points[i],workspace.header,i,"online");
    }
    
    if(i%1000 == 0 || workspace.points.size() <= 100)
      ROS_INFO("At sample %d, (%f,%f,%f)",i,workspace.points[i].pose.position.x,workspace.points[i].pose.position.y,workspace.points[i].pose.position.z);
  }

  if(!kinematics_cache_->writeToFile(cache_filename_))
  {
    ROS_WARN("Could not write cache to file");
  }    
}


void KinematicsReachability::findIK(const std::string &group_name,
                                    geometry_msgs::PoseStamped &pose_stamped,
                                    moveit_msgs::MoveItErrorCodes &error_code,
                                    moveit_msgs::RobotState &robot_state)
{
  kinematics_msgs::GetConstraintAwarePositionIK::Request request;
  kinematics_msgs::GetConstraintAwarePositionIK::Response response;
  getDefaultIKRequest(group_name,request);
  tf::Pose tmp_pose;
  tf::poseMsgToTF(pose_stamped.pose,tmp_pose);
  tmp_pose = tmp_pose * tool_offset_inverse_;
  tf::poseTFToMsg(tmp_pose,pose_stamped.pose);  
  request.ik_request.pose_stamped = pose_stamped;
  if(use_cache_)
  {      
    if(!updateFromCache(request))
    {
      error_code.val = error_code.PLANNING_FAILED;
      return;      
    }       
  }        
  kinematics_solver_.getIK(request,response);
  error_code = response.error_code;
  robot_state = response.solution;  
}

                                        
void KinematicsReachability::getDefaultIKRequest(const std::string &group_name,
                                                 kinematics_msgs::GetConstraintAwarePositionIK::Request &req)
{
  kinematics_msgs::GetKinematicSolverInfo::Request request;
  kinematics_msgs::GetKinematicSolverInfo::Response response;

  planning_models::KinematicModelConstPtr kinematic_model = kinematics_solver_.getKinematicModel();
  planning_models::KinematicState kinematic_state(kinematic_model);
  const planning_models::KinematicModel::JointModelGroup* joint_model_group = kinematic_model->getJointModelGroup(group_name);
  planning_models::KinematicState::JointStateGroup joint_state_group(&kinematic_state,(const planning_models::KinematicModel::JointModelGroup*) joint_model_group);
  joint_state_group.setToRandomValues();
  
  req.timeout = ros::Duration(5.0);
  req.ik_request.ik_link_name = joint_model_group->getLinkModelNames().back();
  req.ik_request.ik_seed_state.joint_state.name = joint_model_group->getJointModelNames();
  joint_state_group.getGroupStateValues(req.ik_request.ik_seed_state.joint_state.position);  
}

/////////////////////////////////////////////////////////////////////////////////////////////
// Cache Functions //////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////

bool KinematicsReachability::updateFromCache(kinematics_msgs::GetConstraintAwarePositionIK::Request &request)
{
  geometry_msgs::Pose pose = request.ik_request.pose_stamped.pose;
  double distance_squared = (pose.position.x*pose.position.x + pose.position.y*pose.position.y + pose.position.z*pose.position.z);
  std::pair<double,double> distances;
  distances = kinematics_cache_->getMinMaxSquaredDistance(); 
  if(distance_squared >= distances.second)
    return false;  
  kinematics_cache_->getSolution(request.ik_request.pose_stamped.pose,
                                 0,
                                 request.ik_request.ik_seed_state.joint_state.position);
  return true;  
}

bool KinematicsReachability::generateCache(const std::string &group_name,
                                           double timeout,
                                           const kinematics_cache::KinematicsCache::Options &options,
                                           const std::string &cache_filename)
{
  if(!kinematics_cache_ || kinematics_cache_->getGroupName()!= group_name)
  {    
    std::map<std::string,kinematics::KinematicsBasePtr> kinematics_solver_map = kinematics_solver_.getPlanningSceneMonitor()->getKinematicModelLoader()->generateKinematicsSolversMap();
    if(kinematics_solver_map.find(group_name) == kinematics_solver_map.end())
    {
      ROS_ERROR("Group name: %s incorrect",group_name.c_str());      
      return false;
    }    
    kinematics::KinematicsBaseConstPtr kinematics_solver_local = kinematics_solver_map.find(group_name)->second;    
    kinematics_cache_.reset(new kinematics_cache::KinematicsCache());
    kinematics_cache_->initialize(kinematics_solver_local,
                                  kinematics_solver_.getKinematicModel(),
                                  options);    
  }  
  if(!kinematics_cache_->readFromFile(cache_filename))
  {
    ROS_INFO("Generating cache map online");    
    if(!kinematics_cache_->generateCacheMap(timeout))
    {
      return false;
    }          
    if(!kinematics_cache_->writeToFile(cache_filename))
    {
      ROS_ERROR("Could not write to file");
      return false;      
    }    
  }
  return true;  
}

/////////////////////////////////////////////////////////////////////////////////////////////
// Helper Functions //////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////

bool KinematicsReachability::isEqual(const geometry_msgs::Quaternion &orientation_1, 
                                     const geometry_msgs::Quaternion &orientation_2)
{
  tf::Quaternion quat_1,quat_2;
  tf::quaternionMsgToTF(orientation_1,quat_1);
  tf::quaternionMsgToTF(orientation_2,quat_2);
  if(quat_1.angleShortestPath(quat_2) < 0.001)
    return true;
  return false;
}

void KinematicsReachability::publishWorkspace(const kinematics_reachability::WorkspacePoints &workspace)
{
  workspace_publisher_.publish(workspace); 
}

/////////////////////////////////////////////////////////////////////////////////////////////
// Workspace Functions //////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////

void KinematicsReachability::getPositionIndex(const kinematics_reachability::WorkspacePoints &workspace,
                                              std::vector<unsigned int> &reachable_workspace,
                                              std::vector<unsigned int> &unreachable_workspace)
{
  unsigned int x_num_points,y_num_points,z_num_points;
  getNumPoints(workspace,x_num_points,y_num_points,z_num_points);
  unsigned int num_workspace_points = x_num_points*y_num_points*z_num_points*workspace.orientations.size();
  for(unsigned int i=0; i < num_workspace_points; ++i)
  {
    if(workspace.points[i].solution_code.val == workspace.points[i].solution_code.SUCCESS)
      reachable_workspace.push_back(i);
    else
      unreachable_workspace.push_back(i);
  }
}

void KinematicsReachability::removeUnreachableWorkspace(kinematics_reachability::WorkspacePoints &workspace)
{
  unsigned int remove_counter = 0;
  moveit_msgs::MoveItErrorCodes error_code;
  std::vector<kinematics_reachability::WorkspacePoint>::iterator it = workspace.points.begin();
  while(it != workspace.points.end())
  {
    if(it->solution_code.val != it->solution_code.SUCCESS)
    {
      it = workspace.points.erase(it);
      remove_counter++;
    }
    else
      ++it;
  }
  if(remove_counter)
    ROS_DEBUG("Removed %d points from workspace",remove_counter);
}

std::vector<const kinematics_reachability::WorkspacePoint*> KinematicsReachability::getPointsAtOrientation(const kinematics_reachability::WorkspacePoints &workspace,
                                                                                                           const geometry_msgs::Quaternion &orientation)
{
  std::vector<const kinematics_reachability::WorkspacePoint*> wp;
  for(unsigned int i = 0; i < workspace.points.size(); ++i)
  {
    if(isEqual(workspace.points[i].pose.orientation,orientation))
      wp.push_back(&(workspace.points[i]));
  }
  return wp;
}

std::vector<const kinematics_reachability::WorkspacePoint*> KinematicsReachability::getPointsWithinRange(const kinematics_reachability::WorkspacePoints &workspace,
                                                                                                         const double min_radius,
                                                                                                         const double max_radius)
{
  std::vector<const kinematics_reachability::WorkspacePoint*> wp;
  for(unsigned int i = 0; i < workspace.points.size(); ++i)
  {
    tf::Vector3 vector;
    tf::pointMsgToTF(workspace.points[i].pose.position,vector);
    if(vector.length() >= min_radius && vector.length() <= max_radius)
      wp.push_back(&(workspace.points[i]));
  }
  return wp;
}

void KinematicsReachability::getNumPoints(const kinematics_reachability::WorkspacePoints &workspace,
                                          unsigned int &x_num_points,
                                          unsigned int &y_num_points,
                                          unsigned int &z_num_points)
{
  double position_resolution = workspace.position_resolution;
  double x_dim = std::fabs(workspace.parameters.min_corner.x - workspace.parameters.max_corner.x);
  double y_dim = std::fabs(workspace.parameters.min_corner.y - workspace.parameters.max_corner.y);
  double z_dim = std::fabs(workspace.parameters.min_corner.z - workspace.parameters.max_corner.z);

  x_num_points = (unsigned int) (x_dim/position_resolution) + 1;
  y_num_points = (unsigned int) (y_dim/position_resolution) + 1;
  z_num_points = (unsigned int) (z_dim/position_resolution) + 1;

  ROS_DEBUG("Cache dimension (num grid points) in (x,y,z): %d %d %d",x_num_points,y_num_points,z_num_points);
}



bool KinematicsReachability::sampleUniform(kinematics_reachability::WorkspacePoints &workspace)
{
  if(workspace.orientations.empty())
  {
    ROS_ERROR("Must specify at least one orientation");
    return false;
  }

  double position_resolution = workspace.position_resolution;
  double x_min = workspace.parameters.min_corner.x;
  double y_min = workspace.parameters.min_corner.y;
  double z_min = workspace.parameters.min_corner.z;

  unsigned int x_num_points,y_num_points,z_num_points;
  getNumPoints(workspace,x_num_points,y_num_points,z_num_points);

  unsigned int num_rotations = workspace.orientations.size();
  kinematics_reachability::WorkspacePoint ws_point;
  geometry_msgs::Pose pose;

  for(unsigned int i=0; i < x_num_points; ++i)
  {
    pose.position.x = x_min + i * position_resolution;
    for(unsigned int j=0; j < y_num_points; ++j)
    {
      pose.position.y = y_min + j * position_resolution;
      for(unsigned int k=0; k < z_num_points; ++k)
      {
        pose.position.z = z_min + k * position_resolution;
        for(unsigned int m=0; m < num_rotations; ++m)
        {
          tf::Vector3 point(pose.position.x,pose.position.y,pose.position.z);
          tf::pointTFToMsg(point,ws_point.pose.position);
          ws_point.pose.orientation = workspace.orientations[m];
          workspace.points.push_back(ws_point);
        }
      }
    }
  }
  ROS_DEBUG("Generated %d samples for workspace points",(int) workspace.points.size());
  return true;
}

void KinematicsReachability::setToolFrameOffset(const geometry_msgs::Pose &pose)
{
  tf::poseMsgToTF(pose,tool_offset_);
  tool_offset_inverse_ = tool_offset_.inverse();
}

/////////////////////////////////////////////////////////////////////////////////////////////
// Visualization functions //////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////

void KinematicsReachability::getMarkers(const kinematics_reachability::WorkspacePoints &workspace,
                                        const std::string &marker_namespace,
                                        std::vector<const kinematics_reachability::WorkspacePoint*> points,
                                        visualization_msgs::MarkerArray &marker_array)
{
  for(unsigned int i=0; i < points.size(); ++i)
  {
    visualization_msgs::Marker marker = getSphereMarker(*points[i],workspace.header,i,marker_namespace);    
    marker_array.markers.push_back(marker);
  }
}

void KinematicsReachability::getPositionIndexedMarkers(const kinematics_reachability::WorkspacePoints &workspace,
                                                       const std::string &marker_namespace,
                                                       visualization_msgs::MarkerArray &marker_array)
{
  unsigned int x_num_points,y_num_points,z_num_points;
  getNumPoints(workspace,x_num_points,y_num_points,z_num_points);
  unsigned int num_rotations = workspace.orientations.size();
  unsigned int num_positions = x_num_points*y_num_points*z_num_points;

  for(unsigned int i=0; i < num_positions; ++i)
  {
    unsigned int start_index = i*num_rotations;
    unsigned int end_index = (i+1)*num_rotations;
    unsigned int num_solutions = 0;
    moveit_msgs::MoveItErrorCodes error_code;
    for(unsigned int j = start_index; j < end_index; ++j)
    {
      if(workspace.points[j].solution_code.val == workspace.points[j].solution_code.SUCCESS)
        num_solutions++;
    }
    double color_scale = num_solutions/(double) num_rotations;
    visualization_msgs::Marker marker = getSphereMarker(workspace.points[start_index],
                                                        workspace.header,
                                                        i,
                                                        marker_namespace);    
    if(num_solutions > 0)    
    {
      marker.ns = marker_namespace + "/reachable";
      marker.color.g = color_scale;
    }
    marker_array.markers.push_back(marker);
  }
}

void KinematicsReachability::getPositionIndexedArrowMarkers(const kinematics_reachability::WorkspacePoints &workspace,
                                                            const std::string &marker_namespace,
                                                            visualization_msgs::MarkerArray &marker_array)
{
  unsigned int x_num_points,y_num_points,z_num_points;
  getNumPoints(workspace,x_num_points,y_num_points,z_num_points);
  unsigned int num_rotations = workspace.orientations.size();
  unsigned int num_positions = x_num_points*y_num_points*z_num_points;

  visualization_msgs::Marker marker;
  marker.type = marker.ARROW;
  marker.action = 0;
  for(unsigned int i=0; i < num_positions; ++i)
  {
    unsigned int start_index = i*num_rotations;
    unsigned int end_index = (i+1)*num_rotations;
    moveit_msgs::MoveItErrorCodes error_code;
    for(unsigned int j = start_index; j < end_index; ++j)
    {
      visualization_msgs::Marker marker = getArrowMarker(workspace.points[j],workspace.header,j,marker_namespace);      
      marker_array.markers.push_back(marker);
    }
  }
}

visualization_msgs::Marker KinematicsReachability::getSphereMarker(const kinematics_reachability::WorkspacePoint &point,
                                                                   const std_msgs::Header &header,
                                                                   unsigned int marker_id,
                                                                   const std::string &marker_namespace,
                                                                   bool evaluating)
{
  visualization_msgs::Marker marker;
  marker.type = marker.SPHERE;
  marker.action = 0;

  if(evaluating)
  {
    marker.ns = marker_namespace + "/evaluating";
    marker.color = evaluating_color_;        
  }
  else
  {    
    if(point.solution_code.val == point.solution_code.SUCCESS)
    {
      marker.ns = marker_namespace + "/reachable";
      marker.color = reachable_color_;    
    }
    else
    {
      marker.color = unreachable_color_;    
      marker.ns = marker_namespace + "/unreachable";
    }
  }
  
  marker.header = header;
  marker.pose = point.pose;  
  marker.scale = sphere_marker_scale_;
  marker.id = marker_id;
  return marker;  
}

visualization_msgs::Marker KinematicsReachability::getArrowMarker(const kinematics_reachability::WorkspacePoint &point,
                                                                  const std_msgs::Header &header,
                                                                  unsigned int marker_id,
                                                                  const std::string &marker_namespace)
{
  visualization_msgs::Marker marker;
  marker.type = marker.ARROW;
  marker.action = 0;
  if(point.solution_code.val == point.solution_code.SUCCESS)
  {
    marker.color = reachable_color_;    
    marker.ns = marker_namespace + "/reachable";
  }
  else
  {
    marker.color = unreachable_color_;
    marker.ns = marker_namespace + "/unreachable";
  }
  marker.header = header;
  marker.pose = point.pose;
  marker.header.stamp = ros::Time::now();
  
  //     marker.scale.x = 0.08;
  //      marker.scale.y = 0.01;
  //      marker.scale.z = 0.01;      
  marker.scale = arrow_marker_scale_;
  marker.id = marker_id;
  return marker;  
}

bool KinematicsReachability::getDisplayTrajectory(const kinematics_reachability::WorkspacePoints &workspace,
                                                  moveit_msgs::DisplayTrajectory &display_trajectory)
{
  if(workspace.points.empty())
     return false;

  std::vector<unsigned int> reachable_workspace, unreachable_workspace;
  getPositionIndex(workspace,reachable_workspace,unreachable_workspace);
  ros::Duration time_from_start(0.0);
  bool first_time(true);  
  for(unsigned int i=0; i < reachable_workspace.size(); ++i)
  {
    if(first_time)
    {
        display_trajectory.trajectory.joint_trajectory.joint_names = workspace.points[reachable_workspace[i]].robot_state.joint_state.name;
        first_time = false;
    }    
    trajectory_msgs::JointTrajectoryPoint point;
    point.positions = workspace.points[reachable_workspace[i]].robot_state.joint_state.position;
    point.time_from_start = time_from_start;
    display_trajectory.trajectory.joint_trajectory.points.push_back(point);
  }
  return true;
}

bool KinematicsReachability::getDisplayTrajectory(const kinematics_reachability::WorkspacePoint &workspace_point,
                                                  moveit_msgs::DisplayTrajectory &display_trajectory)
{
  trajectory_msgs::JointTrajectoryPoint point;
  display_trajectory.trajectory.joint_trajectory.joint_names = workspace_point.robot_state.joint_state.name;
  point.positions = workspace_point.robot_state.joint_state.position;
  display_trajectory.trajectory.joint_trajectory.points.push_back(point);
  display_trajectory.trajectory.joint_trajectory.points.push_back(point);
  return true;
}

void KinematicsReachability::animateWorkspace(const kinematics_reachability::WorkspacePoints &workspace)
{
  moveit_msgs::DisplayTrajectory display_trajectory;
  if (!getDisplayTrajectory(workspace,display_trajectory))
  {
    ROS_WARN("No trajectory to display");
    return;
  }  
  robot_trajectory_publisher_.publish(display_trajectory);
  ROS_INFO("Animating trajectory");  
}

void KinematicsReachability::visualize(const kinematics_reachability::WorkspacePoints &workspace,
                                       const std::string &marker_namespace)
{
  visualization_msgs::MarkerArray marker_array;
  getPositionIndexedMarkers(workspace,marker_namespace,marker_array);
  visualization_publisher_.publish(marker_array);
}

void KinematicsReachability::visualize(const kinematics_reachability::WorkspacePoints &workspace,
                                       const std::string &marker_namespace,
                                       const geometry_msgs::Quaternion &orientation)
{
  visualization_msgs::MarkerArray marker_array;
  std::vector<const kinematics_reachability::WorkspacePoint*> points = getPointsAtOrientation(workspace,orientation);
  getMarkers(workspace,marker_namespace,points,marker_array);
  visualization_publisher_.publish(marker_array);
}

void KinematicsReachability::visualize(const kinematics_reachability::WorkspacePoint &workspace_point,
                                       const std_msgs::Header &header,
                                       unsigned int marker_id,
                                       const std::string &marker_namespace,
                                       bool evaluating)
{
  if(workspace_point.solution_code.val == workspace_point.solution_code.SUCCESS)
  {
    ROS_DEBUG("Publishing successful state");      
    moveit_msgs::DisplayTrajectory display_trajectory;  
    getDisplayTrajectory(workspace_point,display_trajectory);
    robot_trajectory_publisher_.publish(display_trajectory);
  }    

  if(!marker_namespace.empty())
  {
    visualization_msgs::MarkerArray marker_array;
    visualization_msgs::Marker marker = getSphereMarker(workspace_point,header,marker_id,marker_namespace,evaluating);
    marker_array.markers.push_back(marker);    
    if(!evaluating)
    {
      visualization_msgs::Marker delete_marker;
      delete_marker.header = header;    
      delete_marker.ns = marker_namespace + "/evaluating";
      delete_marker.id = marker_id;    
      delete_marker.action = 2;
      marker_array.markers.push_back(delete_marker);  
    }
    visualization_publisher_.publish(marker_array);
  }  
}

void KinematicsReachability::visualize(const kinematics_reachability::WorkspacePoints &workspace,
                                       const std::string &marker_namespace,
                                       const std::vector<geometry_msgs::Quaternion> &orientations)
{
  visualization_msgs::MarkerArray marker_array;
  for(unsigned int i=0; i < orientations.size(); ++i)
  {
    std::vector<const kinematics_reachability::WorkspacePoint*> points = getPointsAtOrientation(workspace,orientations[i]);
    std::ostringstream name;
    name << "orientation_" << i;
    std::string marker_name = marker_namespace+name.str();
    getMarkers(workspace,marker_name,points,marker_array);
  }
  visualization_publisher_.publish(marker_array);
}

void KinematicsReachability::visualizeWithArrows(const kinematics_reachability::WorkspacePoints &workspace,
                                                 const std::string &marker_namespace)
{
  visualization_msgs::MarkerArray marker_array;
  getPositionIndexedArrowMarkers(workspace,marker_namespace,marker_array);
  visualization_publisher_.publish(marker_array);
}

void KinematicsReachability::visualizeWorkspaceSamples(const kinematics_reachability::WorkspacePoints &workspace_in,
                                                       const std::string &marker_namespace)
{
  kinematics_reachability::WorkspacePoints workspace = workspace_in;
  
  visualization_msgs::MarkerArray marker_array;
  visualization_msgs::Marker marker;
  marker.type = marker.CUBE;
  marker.action = 0;

  marker.color.r = 0.5;
  marker.color.g = 0.5;
  marker.color.b = 0.5;
  marker.color.a = 0.2;
  
  marker.ns = marker_namespace + "/boundary";
  marker.header = workspace.header;

  marker.pose.position.x = (workspace.parameters.min_corner.x + workspace.parameters.max_corner.x)/2.0;
  marker.pose.position.y = (workspace.parameters.min_corner.y + workspace.parameters.max_corner.y)/2.0;
  marker.pose.position.z = (workspace.parameters.min_corner.z + workspace.parameters.max_corner.z)/2.0;
  marker.pose.orientation.w = 1.0;
  
  marker.scale.x = std::fabs(workspace.parameters.min_corner.x - workspace.parameters.max_corner.x);
  marker.scale.y = std::fabs(workspace.parameters.min_corner.y - workspace.parameters.max_corner.y);
  marker.scale.z = std::fabs(workspace.parameters.min_corner.z - workspace.parameters.max_corner.z);
  marker.id = 0; 
  marker_array.markers.push_back(marker);
  
  if(workspace.points.empty())
    sampleUniform(workspace);
  
  for(unsigned int i=0; i < workspace.points.size(); ++i)
  {
    visualization_msgs::Marker marker_point = getSphereMarker(workspace.points[i],workspace.header,i,marker_namespace);    
    marker.ns = marker_namespace + "evaluating";    
    marker_point.color = evaluating_color_;
    marker_array.markers.push_back(marker_point);    
  }
  ROS_INFO("Publishing initial set of markers");  
  visualization_publisher_.publish(marker_array);  
}

} // namespace