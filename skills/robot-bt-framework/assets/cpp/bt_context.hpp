#pragma once

#include <functional>
#include <mutex>
#include <string>

namespace bt_xml_planner
{

using ConditionResultCallback = std::function<bool(const std::string &, bool)>;

struct BlackboardKeys
{
  static constexpr const char * node = "node";
  static constexpr const char * mutex = "blackboard_mutex";
  static constexpr const char * motion_tracker = "motion_goal_tracker";
  static constexpr const char * condition_result = "condition_result";
};

}  // namespace bt_xml_planner
