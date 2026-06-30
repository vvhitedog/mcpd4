# Regularization Exactness Proof

Last updated: 2026-06-29 19:57 PDT

This note records the exactness condition for the original low-scale
regularization scheme and reviews whether the `early_experiments` implementation
at commit `e5d48b2` enforces that condition.

## Claim

Let the original integer min-cut objective be scaled by an objective quantum
`M`. In the original experiments, `M = 10000`.

If a regularized dual-decomposition round returns local solutions that agree on
every duplicated boundary node, then the agreed primal solution is globally
optimal for the original unregularized problem when the total regularization
range for that round is strictly less than `M`.

The condition is:

```text
B < M
```

where `B` is the maximum total effect that regularization could have on the
summed decomposed objective in that solve.

For a one-sided nonnegative regularizer, this is:

```text
B = sum of all active regularization weights across all local subproblems
```

This is not the final regularization contribution paid by the returned solution.
It is the maximum possible regularization swing over the local solve. For an
integer implementation, the safe check is:

```text
global_regularization_budget <= objective_scale - 1
```

Equivalently:

```text
global_regularization_budget < objective_scale
```

If the budget equals `M`, the proof does not go through because the perturbation
could hide exactly one original objective unit.

## Proof

Let:

```text
F(z)       original scaled min-cut objective for a globally consistent labeling z
F*         optimal original scaled objective value
L_alpha(x) unregularized decomposed Lagrangian objective
g(alpha)   min_x L_alpha(x), the unregularized DD lower bound
R(x)       regularization perturbation used in the local subproblem solves
H_alpha(x) L_alpha(x) + R(x)
y          minimizer returned by the regularized local solves
```

Here `x` ranges over independent local copies. A globally consistent labeling
`z` is a special case where all copies of duplicated boundary nodes agree.

The standard dual-decomposition relaxation gives:

```text
g(alpha) <= F*
```

Assume the regularizer has bounded range:

```text
0 <= R(x) <= B
```

The regularized solver minimizes:

```text
H_alpha(x) = L_alpha(x) + R(x)
```

Let `x_g` be an unregularized minimizer, so `L_alpha(x_g) = g(alpha)`. Since
`y` minimizes `H_alpha`:

```text
L_alpha(y) + R(y) <= L_alpha(x_g) + R(x_g)
```

Using `L_alpha(x_g) = g(alpha)` and `R(x_g) <= B`:

```text
L_alpha(y) + R(y) <= g(alpha) + B
```

Since `R(y) >= 0`:

```text
L_alpha(y) <= g(alpha) + B
```

Rearrange:

```text
g(alpha) >= L_alpha(y) - B
```

If `y` has agreement on every duplicated boundary node, then all DD alpha terms
cancel globally:

```text
L_alpha(y) = F(y_global)
```

Therefore:

```text
F(y_global) - B <= g(alpha) <= F* <= F(y_global)
```

The final inequality holds because `y_global` is a feasible primal labeling.

If all original objectives are scaled by `M`, then both `F*` and
`F(y_global)` are multiples of `M`. When `B < M`:

```text
F(y_global) - M < F* <= F(y_global)
```

There is no distinct multiple of `M` in that interval. Therefore:

```text
F* = F(y_global)
```

The agreed solution is globally optimal for the original unregularized problem.

## What "Below M" Means

The phrase "regularization is below `M`" means the total objective range induced
by regularization in the entire summed decomposed solve is below `M`.

It does not mean:

```text
final_regularization_contribution < M
```

It also does not mean:

```text
each local partition budget < M
```

unless the sum over partitions is also below `M`.

For the original one-sided regularizer, each active local node gets an added
cost of `regularization_strength * x_i`, so each active local node contributes
`regularization_strength` to the budget. The correct global budget is the sum
over every active regularized local node participating in the local solves whose
lower bounds are being summed.

For a more general regularizer that can have negative or symmetric terms, the
safe budget is the total range:

```text
B = max_x R(x) - min_x R(x)
```

The exactness check remains:

```text
B < M
```

## Review Of The OG Implementation

Reviewed target:

```text
vvhitedog/mcpd3 early_experiments
commit e5d48b2 Introduce regularization on constrainied nodes
```

The OG code has the right mathematical shape:

- The example multiplies every arc and terminal capacity by `10000` before
  constructing the dual decomposition. This creates the objective lattice
  (`example/dimacs_dual_decomp.cpp`, lines 44-49 in `e5d48b2`).
- Regularization is disabled at high DD step sizes and enabled only at low
  scales: strength `10` at step size `10`, strength `1` at step size `1`
  (`decomp/dualdecomp.h`, lines 181-183).
- The regularizer is nonnegative and one-sided. In the incremental local solve,
  if a constrained local node had previous label `1`, the code adds
  `regularization_strength` to that node's terminal term, penalizing the current
  label `1` (`primaldual/mcpd3.h`, lines 455-468).
- The reported local min-cut value excludes the regularization term. It is
  recomputed and incrementally updated using original local capacities and DD
  alpha terms only (`primaldual/mcpd3.h`, lines 313-345 and 499-572).
- The DD lower-bound sum uses `solver->getMinCutValue()`, so it is summing
  those unregularized local values (`decomp/dualdecomp.h`, lines 187-193).
- If all duplicated boundary labels agree, the source-side and target-side alpha
  terms cancel in the summed Lagrangian, so the reported summed value is the
  original primal objective for the agreed labeling.

Those points match the proof structure.

However, the OG implementation does not fully enforce the proof obligation:

- The budget check is local to each `PrimalDualMinCutSolver`; it is not summed
  globally across all partitions.
- The check is warning-only. It prints a message but still accepts the result as
  if it were certifying.
- The check uses `regularization_budget > 10000`. The proof requires strict
  `< 10000`, so the implementation should reject `>= 10000`.
- The threshold is hardcoded as `10000` rather than tied to the actual objective
  scale used by the caller.
- The local `regularization_budget` variable only counts regularization terms
  set during that incremental update. It does not export a durable effective
  budget for the solve.
- Because the local maxflow graph is updated incrementally, a regularized
  terminal value can persist when a constrained node is skipped due to unchanged
  alpha. That means the actual perturbation used by maxflow can differ from the
  budget counted in the current update.
- Changing from step size `10` to step size `1` does not force a full local
  terminal recomputation in the OG code. A stale `+10` perturbation can remain
  unless that node is refreshed by an alpha change.
- The exactness argument only applies to runs that use the scaled objective
  lattice. The OG example does this by premultiplying capacities by `10000`, but
  the solver class itself does not enforce or even know that invariant.

## Conclusion

The OG scheme is exact in principle under the bounded-range lattice proof:

```text
agreement + global_regularization_budget < objective_scale => optimal
```

But the `early_experiments` implementation only approximates that guard. It has
the intended ingredients, but it does not provide a reliable certificate because
it does not track and enforce the global effective regularization budget for the
actual local solves being summed.

The `babyface.n6c10` OG run reached:

```text
lower_bound = 194480000
num_disagreeing = 0
```

and no per-local warning appeared in the captured log. That confirms the old
scheme converged on this case and that no single logged local update exceeded
the hardcoded threshold. It does not, by itself, prove that the implementation
enforced the global `B < 10000` condition.

## Hardening Requirements

To turn the OG idea into a certifying implementation:

1. Make the objective quantum explicit, for example `objective_scale`.
2. Track the effective regularization term applied to every regularized local
   node for the current solve.
3. Compute each local range contribution before or during the solve.
4. Sum local contributions at the coordinator/DD level.
5. Require:

   ```text
   global_regularization_budget < objective_scale
   ```

6. Treat `>= objective_scale` as non-certifying, not as a warning.
7. Force a full terminal recomputation whenever regularization strength changes,
   or clear all regularization state before changing strength.
8. Keep lower-bound reporting on the unregularized objective.
9. Only declare optimality from a regularized round when both conditions hold:

   ```text
   agreement == true
   global_regularization_budget < objective_scale
   ```
