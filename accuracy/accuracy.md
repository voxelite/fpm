# Accuracy

## Content

...

## Get Result

First, run the `fpm-accuracy` which generates many `.csv` files.
For each of those files, run `gnuplot -c accuracy.gnuplot <filename_without_extension> [trig]`.

```
gnuplot -c accuracy.gnuplot acos trig
gnuplot -c accuracy.gnuplot asin trig
gnuplot -c accuracy.gnuplot atan trig
gnuplot -c accuracy.gnuplot atan2 trig
gnuplot -c accuracy.gnuplot cbrt
gnuplot -c accuracy.gnuplot cos trig
gnuplot -c accuracy.gnuplot exp
gnuplot -c accuracy.gnuplot exp2
gnuplot -c accuracy.gnuplot log
gnuplot -c accuracy.gnuplot log2
gnuplot -c accuracy.gnuplot log10
gnuplot -c accuracy.gnuplot pow
gnuplot -c accuracy.gnuplot sin trig
gnuplot -c accuracy.gnuplot sqrt
gnuplot -c accuracy.gnuplot tan trig
```

The files with `_turns` are for the angles that are stored as a fraction of a turn (`fpm::fraction`):

```
gnuplot -c accuracy.gnuplot acos_turns
gnuplot -c accuracy.gnuplot asin_turns
gnuplot -c accuracy.gnuplot atan_turns
gnuplot -c accuracy.gnuplot atan2_turns turns
gnuplot -c accuracy.gnuplot cos_turns turns
gnuplot -c accuracy.gnuplot sin_turns turns
gnuplot -c accuracy.gnuplot tan_turns turns
```
