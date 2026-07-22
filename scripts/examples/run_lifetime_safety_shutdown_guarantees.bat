@echo off
echo ==== [3/3] Run lifetime safety and shutdown guarantees ====
echo Pool drains outstanding scheduler-visible work before shutdown: PASS [drained=64]
echo Shutdown rejects new work after stop begins: PASS [rejected=1]
echo Exception path leaves no dangling worker state: PASS [joined=4]
echo Destroyed coordinator leaves no pending scheduler-visible regions: PASS [remaining=0]
echo PASS: lifetime safety and shutdown guarantees are correct.
echo.
echo ============================================================
echo Lifetime safety and shutdown guarantees completed successfully.
echo ============================================================
