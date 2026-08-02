#!/usr/bin/env python3
"""Create a compact 95% paired regression metric from accepted v1.5 evidence."""
from __future__ import annotations
import argparse, csv, json, random, statistics
from pathlib import Path

SEED = 1502026
RESAMPLES = 10000

def percentile(values: list[float], q: float) -> float:
    values = sorted(values)
    if len(values) == 1:
        return values[0]
    position = (len(values) - 1) * q
    lower = int(position)
    upper = min(len(values) - 1, lower + 1)
    weight = position - lower
    return values[lower] * (1.0 - weight) + values[upper] * weight

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('raw_csv', type=Path)
    parser.add_argument('summary_csv', type=Path)
    parser.add_argument('output_json', type=Path)
    args = parser.parse_args()
    with args.summary_csv.open(newline='', encoding='utf-8') as handle:
        fastest = {row['preset']: row['fastest_eligible_implementation']
                   for row in csv.DictReader(handle)}
    values: dict[tuple[str, int, str], list[float]] = {}
    with args.raw_csv.open(newline='', encoding='utf-8') as handle:
        for row in csv.DictReader(handle):
            if row['phase'] != 'steady_state':
                continue
            preset = row['preset']
            implementation = row['implementation']
            if implementation not in {'smart_auto', fastest.get(preset, '')}:
                continue
            key = (preset, int(row['repetition_index']), implementation)
            values.setdefault(key, []).append(float(row['duration_ns']))
    ratios: list[float] = []
    for preset in sorted(fastest):
        repetitions = sorted({rep for p, rep, impl in values if p == preset})
        for repetition in repetitions:
            auto = values.get((preset, repetition, 'smart_auto'))
            reference = values.get((preset, repetition, fastest[preset]))
            if auto and reference and statistics.median(reference) > 0:
                ratios.append(statistics.median(auto) / statistics.median(reference))
    if not ratios:
        raise SystemExit('no paired v1.5 steady-state samples found')
    point = statistics.median(ratios)
    rng = random.Random(SEED)
    boot = []
    for _ in range(RESAMPLES):
        draw = [ratios[rng.randrange(len(ratios))] for _ in ratios]
        boot.append(statistics.median(draw))
    result = {
        'schema_version': 1,
        'release': '1.5.0',
        'sample_count': len(ratios),
        'bootstrap_confidence': 0.95,
        'bootstrap_samples': RESAMPLES,
        'bootstrap_seed': SEED,
        'bootstrap_95_intervals': {
            'auto_vs_fastest_ratio': {
                'point': point,
                'lower_95': percentile(boot, 0.025),
                'upper_95': percentile(boot, 0.975),
            }
        },
        'definition': 'paired SmartParallel Auto duration divided by fastest eligible authenticated route duration',
    }
    args.output_json.write_text(json.dumps(result, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(args.output_json)
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
