#!/bin/bash
set -e

echo "=== Phase 1: Quick smoke test (small) ==="
make -s clean && make -s
./benchmark -N 2 -S 32 -T 10 --verify

echo ""
echo "=== Phase 2: SIZE=32, TSTEPS=100 scale N ==="
for N in 1 2 4 8 16 32; do
    ./benchmark -N $N -S 32 -T 100
    echo ""
done

echo "=== Phase 3: SIZE=120, TSTEPS=500 scale N ==="
for N in 1 2 4 8 16 32; do
    ./benchmark -N $N -S 120 -T 500
    echo ""
done

echo "=== Phase 4: Correctness verification at scale ==="
./benchmark -N 4 -S 120 -T 500 --verify
