# SmartParallel v1.8 — Resource fingerprints

Stable resource fingerprints include the declared governor budget, Runtime ceiling, operation request bounds, grant, scheduler cap, exact-grant requirement, nested mode, provider-control mode, scheduler identity, and deterministic requirement.

They exclude queue position, wait duration, lease sequence numbers, process and thread IDs, timestamps, memory addresses, competing Runtime identities, and observed participation. Observed participation remains diagnostic because scheduler participation may vary while the authenticated cap and plan remain unchanged.
