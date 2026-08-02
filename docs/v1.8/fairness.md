# Fairness policy

v1.8 exposes no public priorities.

Admission uses FIFO as the base order. A smaller fitting request may bypass the oldest blocked request only a bounded number of times. The oldest request is then reserved capacity after either the bypass limit or the configured reservation age is reached.

This policy targets starvation resistance, not perfect equality. It prevents indefinite large-request starvation, prevents a large request from blocking all useful small work forever, and records bypass and reservation counters.

Completion times can still differ when operations request different amounts of work. Publication evidence reports those distributions without converting them into a claim of perfect fairness.
