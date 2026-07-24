#!/usr/bin/gnuplot
set terminal png size 860,600 font "Arial,8"
set datafile separator ","
set datafile missing "-"
set key noenhanced
set key autotitle columnhead
set colors classic
set format y "%g%%"

SERIES=ARG1
DATA_FILE=SERIES.".csv"

if (ARG2 eq "trig") {
    # Trig functions range from -pi to pi
    set xtics pi
    set format x '%.0Pπ'
    set xrange [-pi:pi]
}

err(x,real) = (real != 0) ? abs((x - real)/real) * 100 : (x != 0) ? "-" : 0;

set output "accuracy-".SERIES.".png"
set title 'Δ '.SERIES

#plot for [COL=3:10] DATA_FILE using 1:(err(column(COL),$2)) with linespoints
plot for [COL=4:9] DATA_FILE using 1:(err(column(COL),$2)) with linespoints
