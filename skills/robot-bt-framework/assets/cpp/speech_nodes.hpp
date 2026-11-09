#pragma once

#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "behaviortree_cpp_v3/action_node.h"
#include "behaviortree_cpp_v3/bt_factory.h"
#include "bt_xml_planner/bt_nodes/node_utils.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "speech_interfaces/action/ask.hpp"
#include "speech_interfaces/action/say.hpp"
#include "speech_interfaces/msg/speech_event.hpp"
#include "speech_interfaces/srv/stop_speech.hpp"

namespace bt_xml_planner
{

// "k=v;k2=v2" -> ordered pairs; blank entries and entries without '=' are skipped.
std::vector<std::pair<std::string, std::string>> parse_speech_params(const std::string & text);
// "是;否" -> {"是", "否"}; blank entries are skipped.
std::vector<std::string> split_speech_choices(const std::string & text);

void register_speech_nodes(BT::BehaviorTreeFactory & factory);

// Goal/result bookkeeping for one speech action goal, shared with rclcpp callbacks
// (the callbacks hold their own reference to the state, so reset() never races them;
// they only hold a weak reference to the client, so a pending response cannot keep
// the client alive through its own pending-response map).
template<class ActionT>
class SpeechActionCall
{
public:
  using Client = rclcpp_action::Client<ActionT>;
  using GoalHandle = rclcpp_action::ClientGoalHandle<ActionT>;

  void send(const typename Client::SharedPtr & client, const typename ActionT::Goal & goal)
  {
    auto state = std::make_shared<State>();
    state_ = state;
    client_ = client;
    std::weak_ptr<Client> weak_client = client;
    typename Client::SendGoalOptions options;
    options.goal_response_callback =
      [state, weak_client](typename GoalHandle::SharedPtr handle) {
        bool cancel_requested = false;
        {
          std::lock_guard<std::mutex> lock(state->mutex);
          state->response_received = true;
          state->handle = handle;
          cancel_requested = state->cancel_requested;
        }
        if (handle && cancel_requested) {
          if (auto live_client = weak_client.lock()) {
            try {
              live_client->async_cancel_goal(handle);
            } catch (const std::exception &) {
              // The client may be shutting down after the tree was halted.
            }
          }
        }
      };
    options.result_callback =
      [state](const typename GoalHandle::WrappedResult & wrapped) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->result_received = true;
        state->code = wrapped.code;
        state->result = wrapped.result;
      };
    client->async_send_goal(goal, options);
  }

  bool response_received() const
  {
    if (!state_) {return false;}
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->response_received;
  }

  bool accepted() const
  {
    if (!state_) {return false;}
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->response_received && state_->handle != nullptr;
  }

  bool result_received() const
  {
    if (!state_) {return false;}
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->result_received;
  }

  typename ActionT::Result::SharedPtr result() const
  {
    if (!state_) {return nullptr;}
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->result;
  }

  // How the goal ended (SUCCEEDED / ABORTED / CANCELED); UNKNOWN until result_received().
  rclcpp_action::ResultCode result_code() const
  {
    if (!state_) {return rclcpp_action::ResultCode::UNKNOWN;}
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->code;
  }

  void cancel()
  {
    if (!state_) {return;}
    typename GoalHandle::SharedPtr handle;
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      state_->cancel_requested = true;
      handle = state_->handle;
    }
    if (handle && client_) {
      try {
        client_->async_cancel_goal(handle);
      } catch (const std::exception &) {
        // Same as above: cancellation during shutdown is best effort.
      }
    }
  }

  void reset()
  {
    state_.reset();
    client_.reset();
  }

private:
  struct State
  {
    std::mutex mutex;
    bool response_received{false};
    bool cancel_requested{false};
    bool result_received{false};
    rclcpp_action::ResultCode code{rclcpp_action::ResultCode::UNKNOWN};
    typename GoalHandle::SharedPtr handle;
    typename ActionT::Result::SharedPtr result;
  };

  std::shared_ptr<State> state_;
  typename Client::SharedPtr client_;
};

// Shared lifecycle of Speak and AskUser: wait for the server, send, wait for the
// goal response, then let the subclass decide (on_accepted). The action client is
// created on construction so that server discovery runs before the first tick and
// does not eat into server_timeout; it is only recreated when action_name differs.
template<class ActionT>
class SpeechGoalNode : public BT::StatefulActionNode
{
public:
  SpeechGoalNode(
    const std::string & name, const BT::NodeConfiguration & config,
    std::string default_action)
  : BT::StatefulActionNode(name, config),
    node_(get_ros_node(config)),
    default_action_(std::move(default_action))
  {
    if (node_) {
      client_ = rclcpp_action::create_client<ActionT>(node_, default_action_);
      client_action_name_ = default_action_;
    }
  }

  BT::NodeStatus onStart() override
  {
    std::string action_name = default_action_;
    getInput("action_name", action_name);
    server_timeout_sec_ = 1.0;
    getInput("server_timeout", server_timeout_sec_);
    started_at_ = std::chrono::steady_clock::now();
    goal_sent_ = false;
    server_missing_ = false;
    call_.reset();
    if (!node_) {
      return unavailable("no ROS node available");
    }
    if (!client_ || client_action_name_ != action_name) {
      client_ = rclcpp_action::create_client<ActionT>(node_, action_name);
      client_action_name_ = action_name;
    }
    goal_ = typename ActionT::Goal();
    std::string error;
    if (!build_goal(goal_, error)) {
      RCLCPP_WARN(logger(), "%s: %s", name().c_str(), error.c_str());
      return BT::NodeStatus::FAILURE;
    }
    return onRunning();
  }

  BT::NodeStatus onRunning() override
  {
    if (!goal_sent_) {
      if (!client_->action_server_is_ready()) {
        return elapsed_sec(started_at_) > server_timeout_sec_ ?
               unavailable(client_action_name_ + " is not available") :
               BT::NodeStatus::RUNNING;
      }
      call_.send(client_, goal_);
      goal_sent_ = true;
      sent_at_ = std::chrono::steady_clock::now();
    }
    if (!call_.response_received()) {
      if (elapsed_sec(sent_at_) <= server_timeout_sec_) {
        return BT::NodeStatus::RUNNING;
      }
      // A late acceptance would otherwise leave an orphan goal running on the
      // server: remember the cancel so the response callback cancels it.
      call_.cancel();
      return unavailable("no goal response from " + client_action_name_);
    }
    if (!call_.accepted()) {
      RCLCPP_WARN(logger(), "%s: goal rejected by %s", name().c_str(), client_action_name_.c_str());
      return BT::NodeStatus::FAILURE;
    }
    if (call_.result_received()) {
      // The result payload is only meaningful for SUCCEEDED: an aborted goal carries a
      // default Result whose outcome 0 would read as SPOKEN / ANSWERED.
      const auto code = call_.result_code();
      if (code == rclcpp_action::ResultCode::CANCELED) {
        // Cut off on purpose (by us or by someone else): nothing was spoken or answered.
        RCLCPP_WARN(logger(), "%s: goal on %s was cancelled", name().c_str(), client_action_name_.c_str());
        return BT::NodeStatus::FAILURE;
      }
      if (code != rclcpp_action::ResultCode::SUCCEEDED) {
        // ABORTED (or UNKNOWN): the service broke while handling the goal, which is an
        // availability problem like a missing server, so Speak honours `required`.
        return unavailable(client_action_name_ + " aborted the goal");
      }
    } else if (client_->action_server_is_ready()) {
      server_missing_ = false;
    } else if (!server_missing_) {
      server_missing_ = true;
      server_missing_since_ = std::chrono::steady_clock::now();
    } else if (elapsed_sec(server_missing_since_) > server_timeout_sec_) {
      // The server has been gone for a whole server_timeout while our goal was running:
      // no result will ever arrive, so stop waiting (the cancel is best effort, in case
      // the server comes back and still holds the goal).
      call_.cancel();
      return unavailable(client_action_name_ + " went away while the goal was running");
    }
    return on_accepted();
  }

  void onHalted() override
  {
    if (goal_sent_) {
      call_.cancel();
    }
    goal_sent_ = false;
  }

protected:
  static BT::PortsList common_ports(const std::string & default_action)
  {
    return {
      BT::InputPort<std::string>("action_name", default_action, "speech action name"),
      BT::InputPort<double>("server_timeout", 1.0, "seconds to wait for the speech service"),
    };
  }

  virtual bool build_goal(typename ActionT::Goal & goal, std::string & error) = 0;
  virtual BT::NodeStatus on_accepted() = 0;
  virtual BT::NodeStatus on_unavailable() = 0;

  BT::NodeStatus unavailable(const std::string & why)
  {
    RCLCPP_WARN(logger(), "%s: %s", name().c_str(), why.c_str());
    return on_unavailable();
  }

  rclcpp::Logger logger() const
  {
    return node_ ? node_->get_logger() : rclcpp::get_logger("speech_nodes");
  }

  static double elapsed_sec(std::chrono::steady_clock::time_point since)
  {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
  }

  SpeechActionCall<ActionT> call_;

private:
  rclcpp::Node * node_{};
  std::string default_action_;
  typename rclcpp_action::Client<ActionT>::SharedPtr client_;
  std::string client_action_name_;
  typename ActionT::Goal goal_;
  bool goal_sent_{false};
  bool server_missing_{false};
  double server_timeout_sec_{1.0};
  std::chrono::steady_clock::time_point started_at_;
  std::chrono::steady_clock::time_point sent_at_;
  std::chrono::steady_clock::time_point server_missing_since_;
};

class Speak : public SpeechGoalNode<speech_interfaces::action::Say>
{
public:
  using Action = speech_interfaces::action::Say;
  Speak(const std::string & name, const BT::NodeConfiguration & config);
  static BT::PortsList providedPorts();

protected:
  bool build_goal(Action::Goal & goal, std::string & error) override;
  BT::NodeStatus on_accepted() override;
  BT::NodeStatus on_unavailable() override;

private:
  bool wait_until_done_{false};
  bool required_{false};
};

class AskUser : public SpeechGoalNode<speech_interfaces::action::Ask>
{
public:
  using Action = speech_interfaces::action::Ask;
  AskUser(const std::string & name, const BT::NodeConfiguration & config);
  static BT::PortsList providedPorts();

protected:
  bool build_goal(Action::Goal & goal, std::string & error) override;
  BT::NodeStatus on_accepted() override;
  BT::NodeStatus on_unavailable() override;
};

// Fire-and-forget stop request. The service client is created on construction
// (default service name) so discovery has happened by the first tick; a fresh
// client created inside tick() would still report service_is_ready() == false.
class StopSpeaking : public BT::SyncActionNode
{
public:
  using Service = speech_interfaces::srv::StopSpeech;
  StopSpeaking(const std::string & name, const BT::NodeConfiguration & config);
  static BT::PortsList providedPorts();
  BT::NodeStatus tick() override;

private:
  rclcpp::Node * node_{};
  rclcpp::Client<Service>::SharedPtr client_;
  std::string service_name_;
};

// The subscription is created on construction (default topic) for the same reason as
// StopSpeaking's client, and only recreated when the topic port names another topic.
class WaitForVoiceIntent : public BT::StatefulActionNode
{
public:
  using Event = speech_interfaces::msg::SpeechEvent;
  WaitForVoiceIntent(const std::string & name, const BT::NodeConfiguration & config);
  static BT::PortsList providedPorts();
  BT::NodeStatus onStart() override;
  BT::NodeStatus onRunning() override;
  void onHalted() override;

private:
  void subscribe(const std::string & topic);

  struct Inbox
  {
    std::mutex mutex;
    bool listening{false};
    bool matched{false};
    std::string expected;
    Event event;
  };

  rclcpp::Node * node_{};
  std::shared_ptr<Inbox> inbox_{std::make_shared<Inbox>()};
  rclcpp::Subscription<Event>::SharedPtr subscription_;
  std::string topic_;
  double timeout_sec_{0.0};
  std::chrono::steady_clock::time_point started_at_;
};

}  // namespace bt_xml_planner
