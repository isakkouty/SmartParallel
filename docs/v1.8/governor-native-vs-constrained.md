# SmartParallel v1.8 — Governor-native versus constrained execution

Governor-native routes are controlled by SmartParallel:

- Sequential;
- ThreadPool;
- StaticThread.

For these routes SmartParallel can enforce the admitted-participation bound directly.

Cooperatively constrained providers expose weaker controls:

- oneTBB uses a task-arena concurrency upper bound;
- OpenCV uses serialized process-global single-thread containment.

Reports label each provider with control scope, control strength, restoration behavior, and whether invocation serialization is required.
