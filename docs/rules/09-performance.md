# Performance Rules

## Frame Budget (Target: locked 60 FPS)
| Phase | Budget |
|---|---|
| Total frame | <16.6ms |
| ECS logic (systems) | <6ms |
| Physics | <4ms |
| Render | <6ms |

## Non-Negotiables
- Hot paths: ZERO allocations, ZERO unbounded iteration
- Cache-aware data structures: contiguous arrays over linked lists
- No hidden work per frame (lazy init in hot paths is a bug)

## Optimization Process
1. **Measure first** — always profile before changing code
2. **Identify the bottleneck** — frame time breakdown per system
3. **Fix the bottleneck** — one change at a time
4. **Verify** — profile again, confirm improvement
5. **No premature optimization** — write clear code first, optimize measured hot spots

## What to Track (debug builds)
- Frame time total + per-system breakdown
- Allocations per frame (count + bytes)
- Arena high-water mark
- Debug: frame time graph in terminal overlay

## What NOT to Do
- ❌ Batching unrelated optimizations (now you have two bugs)
- ❌ Optimizing before profiling (gut feeling is wrong)
- ❌ Hand-unrolling loops (Zig's optimizer is better than you)
- ❌ Using `@ptrCast` for "performance" (it's UB waiting to happen)
