# bt_xml_planner BT Plugin Development

This package follows a Nav2-style behavior-tree pattern:

- XML describes task composition and blackboard data flow.
- Plugin nodes implement reusable behavior logic.
- The executor node loads XML, loads plugin libraries, initializes the
  blackboard, ticks the tree, and publishes snapshots.
- Action/service/topic clients belong in plugin nodes when they are part of the
  reusable behavior.

## Runtime Contract

The executor initializes these blackboard entries:

```text
node
blackboard_mutex
```

Plugins should expose XML ports for their inputs and outputs. Prefer explicit
blackboard bindings in XML:

```xml
<WaitForPathTopic topic="/sub_goal" path="{path}" path_available="{path_available}"/>
<SetBoolStateService service_name="/traffic_light/set_green" state="{traffic_light_green}"/>
<FollowPath
  path="{path}"
  server_name="/follow_path"
  controller_id=""
  goal_checker_id=""
  timeout_sec="120.0"
  result="{follow_path_result}"/>
```

Task-specific state such as `{path}`, `{path_available}`, and
`{traffic_light_green}` is created by plugin nodes or XML composition, not by
the executor constructor.

## Plugin Responsibilities

Each plugin should:

1. Declare all data dependencies in `providedPorts()`.
2. Read and write task state through ports or named blackboard keys.
3. Create its own ROS action/service client when that client is part of the
   reusable behavior.
4. Return `RUNNING` while asynchronous work is pending.
5. Cancel active asynchronous work in `onHalted()`.
6. Write terminal results to an output port when useful for diagnostics.

Avoid hiding task state in a package-specific context object. If state is useful
to another node, put it on the blackboard and show the connection in XML.

## Adding a Node

1. Add a header under `include/bt_xml_planner/bt_nodes/`.
2. Add implementation in a focused `src/*_nodes.cpp`.
3. Add the source file to `bt_xml_planner_nodes` in `CMakeLists.txt`.
4. Register the XML tag in `src/bt_nodes.cpp`.
5. Add the node and its ports to `config/bt_nodes.json`.
6. Document an XML example.

## Example Tree

```xml
<Sequence name="TrafficLightFollowRoot">
  <SetBoolStateService
    service_name="/traffic_light/set_green"
    initial_state="false"
    event_topic="/service_events"
    state="{traffic_light_green}"/>
  <WaitForPathTopic
    topic="/sub_goal"
    path="{path}"
    path_available="{path_available}"/>
  <HasPendingPath path_available="{path_available}"/>
  <Fallback name="TrafficLightGate">
    <Sequence name="GreenBranch">
      <IsTrafficLightState state="{traffic_light_green}" expected="true"/>
      <FollowPath
        path="{path}"
        server_name="/follow_path"
        timeout_sec="120.0"
        result="{follow_path_result}"/>
    </Sequence>
    <WaitForTrafficLightState state="{traffic_light_green}" expected="true"/>
  </Fallback>
  <ClearPendingPath path="{path}" path_available="{path_available}"/>
</Sequence>
```
