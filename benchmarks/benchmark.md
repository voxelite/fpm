# Benchmark

## Content

...

## Get Result

First, run the `fpm-benchmark` with arguments `--benchmark_out_format=json --benchmark_out=performance.json`, the standard-output data are not enough.

Then you need to convert it for `gnuplot` using `python3 benchmark.py performance.json performance.csv`.

To get the image, all you need to do is `gnuplot -c benchmark.gnuplot`.
If you want to change image dimensions, it is the 2nd row of the file (first after comment).
