#include "bt_xml_planner/bt_nodes/gate_nodes.hpp"

#include <mutex>
#include <string>

#include "bt_xml_planner/bt_nodes/node_utils.hpp"

namespace bt_xml_planner
{

namespace
{

bool expected_state(const BT::TreeNode & node)
{
  bool expected = true;
  node.getInput("expected", expected);
  return expected;
}

bool current_state(const BT::TreeNode & node)
{
  bool state = false;
  node.getInput("state", state);
  return state;
}

}  // namespace

IsBooleanState::IsBooleanState(
  const std::string & name, const BT::NodeConfiguration & config)
: BT::ConditionNode(name, config),
  blackboard_mutex_(get_blackboard_mutex(config))
{
}

BT::PortsList IsBooleanState::providedPorts()
{
  return {
    BT::InputPort<bool>(
      "state",
      "Boolean state from the blackboard"),
    BT::InputPort<bool>("expected", true, "Boolean value that returns SUCCESS"),
  };
}

BT::NodeStatus IsBooleanState::tick()
{
  std::lock_guard<std::mutex> lock(*blackboard_mutex_);
  const bool actual = current_state(*this);
  const bool expected = expected_state(*this);
  bool result = actual == expected;
  try {
    const auto callback = config().blackboard->get<ConditionResultCallback>(
      BlackboardKeys::condition_result);
    result = callback("leaf/" + name(), result);
  } catch (const std::exception &) {
    // Standalone trees may not be hosted by NavigationBtRuntime.
  }
  return result ?
         BT::NodeStatus::SUCCESS : BT::NodeStatus::FAILURE;
}

WaitForGreenLight::WaitForGreenLight(
  const std::string & name,
  const BT::NodeConfiguration & config)
: BT::StatefulActionNode(name, config),
  blackboard_mutex_(get_blackboard_mutex(config))
{
}

BT::PortsList WaitForGreenLight::providedPorts()
{
  return {
    BT::InputPort<bool>(
      "state",
      "Traffic-light state from the blackboard"),
    BT::InputPort<bool>("expected", true, "Traffic-light state that ends the wait"),
  };
}

BT::NodeStatus WaitForGreenLight::onStart()
{
  return status_from_ports();
}

BT::NodeStatus WaitForGreenLight::onRunning()
{
  return status_from_ports();
}

void WaitForGreenLight::onHalted()
{
}

BT::NodeStatus WaitForGreenLight::status_from_ports() const
{
  std::lock_guard<std::mutex> lock(*blackboard_mutex_);
  return current_state(*this) == expected_state(*this) ?
         BT::NodeStatus::FAILURE : BT::NodeStatus::RUNNING;
}

}  // namespace bt_xml_planner
