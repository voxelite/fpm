# Benchmark

## Content

...

## Get Result

First, run the `fpm-benchmark` with arguments `--benchmark_out_format=json --benchmark_out=performance.json`, the standard-output data are not enough.

Then you need to convert it for `gnuplot` using `python3 benchmark.py performance.json performance.csv`.

To get the image, all you need to do is `gnuplot -c benchmark.gnuplot`.
If you want to change image dimensions, it is the 2nd row of the file (first after comment).

The results for `fpm::fraction` are in the same files: the types `fpm::fraction_8` to `fpm::fraction_64` for its arithmetic, and
the types like `fpm::turns_16_for_16_16` for the trigonometry with angles that are stored as a fraction of a turn
(a 16-bit angle, for numbers of the type `fpm::fixed_16_16`). Those are in `performance_fraction.png`.
