# SmartParallel v1.8 — Trust-the-deployment contract

The product statement is **Trust the deployment**.

The precise technical promise is:

> SmartParallel coordinates participating CPU execution paths inside one process, respects a declared CPU budget, prevents nested oversubscription, preserves deterministic resource requirements, and provides evidence explaining every resource-admission decision.

For governor-native routes, admitted active participation must not exceed the declared budget. For constrained providers, SmartParallel reports the control scope and strength rather than claiming private worker ownership.

The promise does not cover unrelated threads, other processes, operating-system scheduling, memory, accelerator resources, NUMA placement, distributed execution, hard real-time behavior, or safety certification.
