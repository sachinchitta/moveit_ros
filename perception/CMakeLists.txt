cmake_minimum_required(VERSION 2.8.3)
project(moveit_ros_perception)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Eigen REQUIRED)
find_package(Boost REQUIRED thread signals system filesystem date_time)
find_package(catkin REQUIRED COMPONENTS
  moveit_core
  roscpp
  rosconsole
  urdf
  message_filters
  tf
  tf_conversions
  pcl_ros
  pluginlib
  object_recognition_msgs 
)

find_package(OpenMP)
find_package(octomap REQUIRED)
find_package(OpenCV)

catkin_package(
  INCLUDE_DIRS
    point_containment_filter/include
    occupancy_map_monitor/include
    pointcloud_octomap_updater/include
    semantic_world/include
    ${OCTOMAP_INCLUDE_DIRS}
  LIBRARIES
    moveit_point_containment_filter
    moveit_occupancy_map_monitor
    moveit_pointcloud_octomap_updater_core
    ${OCTOMAP_LIBRARIES}
  CATKIN_DEPENDS
    moveit_core)

include_directories(point_containment_filter/include
                    occupancy_map_monitor/include
		    pointcloud_octomap_updater/include
		    semantic_world/include
                    ${catkin_INCLUDE_DIRS} 
		    ${OpenCV_INCLUDE_DIRS}
                    )
include_directories(SYSTEM
                    ${EIGEN_INCLUDE_DIRS}
                    ${Boost_INCLUDE_DIRS}
		    )

link_directories(${Boost_LIBRARY_DIRS})
link_directories(${catkin_LIBRARY_DIRS})

add_subdirectory(point_containment_filter)
add_subdirectory(occupancy_map_monitor)
add_subdirectory(pointcloud_octomap_updater)
add_subdirectory(semantic_world)

install(
  FILES
    pointcloud_octomap_updater_plugin_description.xml
  DESTINATION
    ${CATKIN_PACKAGE_SHARE_DESTINATION})
