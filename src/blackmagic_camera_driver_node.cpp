#include <atomic>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include <blackmagic_camera_driver/decklink_interface.hpp>
#include <ros/ros.h>

#include "DeckLinkAPI_v10_11.h"

namespace blackmagic_camera_driver
{
namespace
{
using DeckLinkIteratorHandle = BMDHandle<IDeckLinkIterator>;

int DoMain()
{
  // Get node handles
  ros::NodeHandle nh;
  ros::NodeHandle nhp("~");

  // Load parameters
  const int32_t decklink_device_index
      = nhp.param(std::string("decklink_device_index"), 0);
  const int32_t camera_number
      = nhp.param(std::string("camera_number"), 1);
  const std::string camera_topic
      = nhp.param(std::string("camera_topic"), std::string("bmd_camera"));
  const std::string camera_frame
      = nhp.param(std::string("camera_frame"), std::string("bmd_camera"));

  if (camera_number < 0)
  {
    throw std::runtime_error("camera_number < 1");
  }
  else if (camera_number > 255)
  {
    throw std::runtime_error("camera_number > 255");
  }

  const uint8_t camera_id = static_cast<uint8_t>(camera_number);

  // Discover DeckLink devices
  DeckLinkIteratorHandle device_iterator(CreateDeckLinkIteratorInstance());
  if (device_iterator)
  {
    // Find available DeckLink devices
    std::vector<DeckLinkHandle> decklink_devices;
    while (true)
    {
      IDeckLink* decklink_device_ptr = nullptr;
      if (device_iterator->Next(&decklink_device_ptr) == S_OK)
      {
        decklink_devices.emplace_back(decklink_device_ptr);
      }
      else
      {
        break;
      }
    }
    ROS_INFO_NAMED(
        ros::this_node::getName(),"Found [%zu] DeckLink device(s)",
        decklink_devices.size());

    // Get the selected device
    ROS_INFO_NAMED(
        ros::this_node::getName(), "Selecting DeckLink device [%d]",
        decklink_device_index);
    DeckLinkDevice capture_device(
        nh, camera_topic, camera_frame,
        std::move(decklink_devices.at(decklink_device_index)));

    // Start capture
    ROS_INFO_NAMED(ros::this_node::getName(), "Starting capture...");
    capture_device.StartVideoCapture();

    // const uint16_t set_focus = ConvertToFixed16(0.5);
    // ROS_INFO("mid focus command %hx", set_focus);
    // const uint8_t set_focus_bottom_byte
    //     = static_cast<uint8_t>(set_focus & 0x00ff);
    // const uint8_t set_focus_top_byte
    //     = static_cast<uint8_t>((set_focus & 0xff00) >> 8);
    // // Focus to the near limit
    // const BlackmagicSDICameraControlMessage set_focus_command(
    //     camera_id,  // Destination camera
    //     0x00,  // "Change configuration"
    //     {0x00,  // "Lens"
    //      0x00,  // "Focus"
    //      0x00, 0x00,  // Empty
    //      set_focus_bottom_byte, set_focus_top_byte});  // Mid focus

    // capture_device.EnqueueCameraCommand(set_focus_command);

    const uint8_t assign_value = 0x00;

    // Turn on OIS (if available)
    const BlackmagicSDICameraControlMessage enable_ois_command
        = BlackmagicSDICameraControlMessage::MakeCommandBool(
            camera_id, 0x00, 0x06, assign_value, true);

    capture_device.EnqueueCameraCommand(enable_ois_command);

    // Run an instantaneous autofocus
    const BlackmagicSDICameraControlMessage autofocus_command
        = BlackmagicSDICameraControlMessage::MakeCommandVoid(
            camera_id, 0x00, 0x01);

    capture_device.EnqueueCameraCommand(autofocus_command);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Spin while video callbacks run
    ros::Rate spin_rate(30.0);
    while (ros::ok())
    {
      ros::spinOnce();
      spin_rate.sleep();
    }

    const BlackmagicSDICameraControlMessage disable_ois_command
        = BlackmagicSDICameraControlMessage::MakeCommandBool(
            camera_id, 0x00, 0x06, assign_value, false);

    capture_device.EnqueueCameraCommand(disable_ois_command);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Stop capture
    ROS_INFO_NAMED(ros::this_node::getName(), "Stopping capture...");
    capture_device.StopVideoCapture();

    // Let RAII clean everything else
    ROS_INFO_NAMED(ros::this_node::getName(), "...capture complete");
  }
  else
  {
    ROS_ERROR_NAMED(
        ros::this_node::getName(),
        "Failed to create DeckLinkIterator instance");
  }
  return 0;
}
}  // namespace
}  // namespace blackmagic_camera_driver

int main(int argc, char** argv)
{
  ros::init(argc, argv, "blackmagic_camera_driver_node");
  return blackmagic_camera_driver::DoMain();
}
