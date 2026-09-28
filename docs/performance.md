---
layout: default
---
# Performance

The image below shows the approximate performance of:
* `fpm` (the `Q16.16` type)
* [libfixmath](https://github.com/PetteriAimonen/libfixmath) (`fix16`), an alternative C library (see [notes](#notes)).
* [Compositional Numeric Library](https://github.com/johnmcfarlane/cnl) (`CNL`), an experimental library that is being developed in C++'s [SG14](https://github.com/WG21-SG14/SG14).
* native FPU operations (`float` and `double`).

The graph below plots the average number of nanoseconds a single operation takes: lower is better. The data was collected with [Google Benchmark](https://github.com/google/benchmark) on an Intel Core i7-5820K, 3.3GHz.

![](http://mikelankampgithub.s3-website-eu-west-1.amazonaws.com/fpm/performance.png)

The results show the following:
* Compared to `libfixmath`, the performance of `fpm` is at least as good, except for `exp`, where it's considerably slower.
* Compared to CNL, `fpm` only matches the performance for `sqrt`. However, CNL does not support the majority of benchmarked functions.
* Compared to native single-precision floating-point operations, `fpm` is slower by up to an order of magnitude for most functions, except for the basic `add`, `sub` and several power or trigonometry functions, where it is faster.

## Notes

For a fair comparison, `libfixmath` was compiled with `FIXMATH_NO_CACHE`.
It should also have been compiled with `FIXMATH_NO_OVERFLOW` (since `fpm` does not detect overflow), but `libfixmath` failed to compile with that option.

## Choosing the number of fraction bits

The approximations (`sin`, `cos`, `log`, `atan`, ...) choose their polynomial at compile time, so that the result is precise
to within about 0.6 units in the last place (ulp) of the type (see [the notes on the mathematical functions](index.md#mathematical-functions)).
More fraction bits need a longer polynomial, which is slower and larger. Since every type is equally precise relative to
its own resolution, the fraction bits are a trade-off between resolution and speed:

| type    | `sin` | `log` | `atan` | `asin` | `exp` | `pow` | `tan` |
|---------|------:|------:|-------:|-------:|------:|------:|------:|
| `24.8`  |   4.0 |   4.2 |    6.3 |     27 |   5.5 |    20 |    15 |
| `20.12` |   4.5 |   5.1 |    6.8 |     33 |   5.5 |    20 |    15 |
| `18.14` |   4.4 |   5.3 |    7.1 |     35 |   5.5 |    20 |    15 |
| `16.16` |   4.4 |   6.0 |    7.2 |     37 |   5.5 |    20 |    15 |
| `12.20` |   5.3 |   6.2 |    8.3 |     42 |   5.5 |    20 |    15 |
| `8.24`  |   5.3 |   7.2 |    9.1 |     46 |   5.5 |    20 |    15 |
| `48.16` |    12 |    15 |     24 |     89 |    19 |    51 |    47 |
| `32.32` |    15 |    21 |     32 |    133 |    19 |    52 |    47 |
| `16.48` |    17 |    27 |     36 |    177 |    19 |    52 |    44 |
| `8.56`  |    19 |    31 |     39 |    197 |    19 |    52 |    45 |

Nanoseconds per call (throughput) on x86-64, Clang 22 with `-O2`; GCC 16 is within about 10-20%.

* A 32-bit base type is 2-4 times as fast as a 64-bit one: its products need 64 instead of 128 bits.
* Within a base type, fewer fraction bits make `sin`, `cos`, `log`, `log2`, `log10`, `atan`, `atan2`, `asin` and `acos` faster.
  The polynomials' lengths change in steps, so neighbouring types can be equally fast.
* `exp`, `exp2`, `pow` and `tan` can have results with all the bits of the type (not only the fraction), so they are always
  calculated with all the bits: their speed only depends on the base type.
* Where the precision of an intermediate result is not needed, converting it to a type with fewer fraction bits (or a
  32-bit base type) before calling a function trades the precision for speed.
