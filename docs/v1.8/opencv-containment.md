# OpenCV containment

v1.8 adds no new OpenCV operation. It preserves the v1.5 threshold provider and contains its process-global thread setting.

The protected sequence is:

```text
lock provider mutex
capture OpenCV thread state
set OpenCV internal threads to one
execute provider call
restore state
unlock provider mutex
```

The complete call is serialized where required. Restoration runs during exception unwinding. Direct OpenCV calls outside SmartParallel remain outside the guarantee and may still race with process-global configuration.
