# SmartParallel v1.6 special-value contract

| Condition | Sum / dot | Norm | AXPY / stencil |
|---|---|---|---|
| Empty input | returns the initial value; dedicated dot returns `+0` | returns `+0` | no elements are written |
| Single element | evaluated by the selected fixed method | absolute magnitude in Accurate | pointwise contract |
| Input NaN | NaN classification propagates | NaN classification propagates | IEEE expression result |
| Positive infinity | positive infinity unless invalidly combined | positive infinity | IEEE expression result |
| Negative infinity | negative infinity unless invalidly combined | positive infinity after absolute value | IEEE expression result |
| Both infinity signs | canonical quiet NaN for compensated sum/dot | positive infinity unless NaN is present | IEEE expression result |
| `+0` / `-0` | Fast/Reproducible follow their IEEE operation order; Accurate finite zero finalizes as `+0` | returns `+0` | IEEE expression result |
| Floating overflow | IEEE infinity; compensation does not promise exact recovery after overflow | scaled Accurate method avoids avoidable square overflow | IEEE expression result |
| Underflow | follows active environment | scaled Accurate method avoids avoidable square underflow | follows active environment |
| Subnormal values | supported subject to unchanged FTZ/DAZ state | same | same |
| Integer overflow | unchanged legacy semantics; no new checked-integer claim | not supported | not supported by v1.6 scientific APIs |

Input NaN payload preservation is not part of the bitwise guarantee. SmartParallel-generated invalid NaNs use `std::numeric_limits<T>::quiet_NaN()`.
