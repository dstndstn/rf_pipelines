### Higher-priority loose ends:

- In the polynomial detrender, the checking for a poorly conditioned Cholesky factorization
  doesn't seem to be working perfectly.  One symptom is that the detrender tests in test-kernels
  sometimes fail for polynomial degree > 8.

  One thing that should be fairly easy to implement: double-precision polynomial fitting
  (with a double_precision flag added to the polyomial_detrender).  This could also be extended
  to add a "monitor" flag to the single-precision polynomial_detrender, which compares the
  result to the double-precision detrender on-the-fly, and increments a counter if it
  detects an inconsistency.

  Maybe the real solution is to change the criterion by which the Cholesky factorization
  detects numerical instability?  Currently, we check each value of L_{ii} / A_{ii}^{1/2}
  independently.  Maybe it would be help to also include a check on prod_i (L_{ii} / A_{ii}^{1/2})?
  What does lapack do?

- Remove hardcoded max downsampling, by writing kernels for the "large-Df" and "large-Dt" cases.
  (Related to "R-kernels" below.)

- I would like to look at the compiler-generated assembly for a few representative kernels,
  just to see whether the compiler is doing something reasonable, and to see if there's scope
  for more optimization.

### Lower-priority loose ends:

- Many kernels could be improved by having a boolean 'Aligned' compile-time argument, which
  selects whether the loads/stores can be aligned.  (This is trivial in principle, but the 
  amount of boilerplate code required to propagate this flag into the transform objects and
  standalone apply_*() functions is a little obnoxious.)

- Related: it would be interesting to see whether streaming writes can help speed up code
  which masks the weights array.  Since streaming writes require aligned pointers, implementing
  'Aligned' flags is a prequisite!

- Detrending kernels: these are pretty well optimized, but there are a few experiments which may
  improve performance a little.  

  For polydeg >= 5ish, the AXIS_TIME kernel will contain register spills, which might be eliminated
  by horizontally summing (or partially summing) on-the fly.

  It would be interesting to see whether the AXIS_FREQ kernel could be improved by doing two
  8-element columns at once, so that I/O takes place on full cache lines.  My prediciton is that
  this helps a little for very low polydeg (say 0 or 1) but otherwise doesn't help and may make
  things slower.

- R-kernels: some kernels could be generalized by having a compile-time argument R, the number
  of rows read at once.  A toy program suggested that R=4 might help speed up the AXIS_TIME clippers.

- test-kernels.cpp: The unit testing here feels a little incomplete, e.g. std_dev_clipper kernels aren't
  tested, and clipper corner cases aren't testsed.   I think this is OK in the short term, since 
  test-cpp-python-equivalence.py is such a complete set of tests.  Nevertheless for completeness 
  I'd like to improve this eventually.

  test-kernels also takes a long time to compile.  This could probably be fixed by moving the unit
  tests to the source files containing kernel tables (polynomial_detrender, intensity_clipper, etc.)
  and getting function pointers out of the kernel tables, so that the templates are only instantiated once.

  Finally, test-kernels could use a lot of cleanup (e.g. reduce use of vectorize() now that simd_helpers is more complete).

- intensity_clipper and std_dev_clipper transforms with AXIS_FREQ could might benefit from
  processing two simd_t's per kernel (rather than one), so that I/O takes places on full
  cache lines.

- Kernels could include the masked fraction (or maybe this could be a boolean flag?)

- Something I may revisit later: I experimented with arrays of precomputed P_l's, rather than computing on-the-fly.
  Preliminary timings suggest that this may improve performance by ~10% for large polynomial degree, but worsen performance for samll degree.
  This may eventually be worth implementing!

- Cleanup: try to share code between AXIS_TIME and AXIS_FREQ detrender kernels?

- Cleanup: could permute ordering of template arguments, so that arguments which the compiler can guess are
  at the end.  (E.g. <T,S> is always first even though it can almost always be guessed.)
