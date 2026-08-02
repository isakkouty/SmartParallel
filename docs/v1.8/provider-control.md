# Provider-control capability model

The internal capability model records:

- control scope: per call, thread, task, or process;
- control strength: exact, upper bound, advisory, serialized process-global, or unsupported;
- concurrent reconfiguration safety;
- restoration support;
- participation observability;
- serialization requirements.

This remains internal in v1.8 because providers do not share one honest universal capture/apply/restore contract.
