# SmartParallel v1.8 — Deadlines and cancellation

Cancellation uses direct governor notification. A pending request subscribes its cancellation state to the governor condition variable, so cancellation latency does not depend on a polling interval.

Race outcomes are serialized under governor state:

- a grant that wins first returns a valid lease;
- cancellation that wins first removes the request and prevents a later grant;
- deadline expiration removes the request;
- shutdown wakes all pending requests with `GovernorShuttingDown`;
- every losing path leaves permit accounting unchanged.

Tests cover cancellation before enqueue, after enqueue, and races with grant, deadline, and shutdown.
