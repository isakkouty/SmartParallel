# Decision engine

The production decision path ranks candidate execution plans from a `DecisionContext` and plan features. Utility scores are ordinal decision values, not runtime estimates.

Runtime-prediction fields may appear in validation CSVs solely to compare against the frozen legacy baseline. They are forbidden as utility-model inputs and cannot promote or override a production decision.

A newly trained utility model starts in shadow mode. Promotion requires lower holdout mean regret than the frozen baseline and no increase in decisions with more than 20% regret.
