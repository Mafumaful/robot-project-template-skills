"""Step progress of the console's background jobs, for the page's progress bars.

A job dict carries `steps` (the step names it plans to go through, in order) and `step_no` (1-based index of the step in
progress, 0 = not started). The jobs keep reporting `step` by name as before; reach() moves step_no to that name.
"""


def plan(job, steps):
    job['steps'] = list(steps); job['step_no'] = 0


def reach(job, step):
    """step_no follows `step` to its next occurrence at or after the current step. A step the plan does not name (a
    fallback, a pull-back after a failure) leaves the bar where it is, and the bar never moves back."""
    steps = job.get('steps') or []; at = max(job.get('step_no', 0) - 1, 0)
    if step and step in steps[at:]:
        job['step_no'] = steps.index(step, at) + 1
