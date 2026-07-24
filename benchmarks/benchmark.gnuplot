#!/usr/bin/gnuplot
set terminal png size 1720,900 font "Arial,8"
set datafile separator ","
set datafile missing "-"

set style data histogram
set style fill solid border linecolor black
set style histogram clustered gap 2
set boxwidth 1

set key left top noenhanced

set xtics scale 0
set grid ytics
set ylabel "avg. time (ns)"

DATA_FILE="performance.csv"

set output "performance.png"
plot for [COL=1:20] DATA_FILE using COL:xtic(1) title columnheader
set output "performance_64.png"
plot for [COL=1:13] DATA_FILE using COL:xtic(1) title columnheader
set output "performance_32.png"
plot DATA_FILE using 2:xtic(1) title columnheader(2), \
     DATA_FILE using 3:xtic(1) title columnheader(3), \
     DATA_FILE using 4:xtic(1) title columnheader(4), \
     DATA_FILE using 5:xtic(1) title columnheader(5), \
     DATA_FILE using 15:xtic(1) title columnheader(15), \
     DATA_FILE using 16:xtic(1) title columnheader(16), \
     DATA_FILE using 17:xtic(1) title columnheader(17), \
     DATA_FILE using 18:xtic(1) title columnheader(18), \
     DATA_FILE using 19:xtic(1) title columnheader(19)
set output "performance_basic.png"
plot DATA_FILE using 2:xtic(1) title columnheader(2), \
     DATA_FILE using 3:xtic(1) title columnheader(3), \
     DATA_FILE using 15:xtic(1) title columnheader(15), \
     DATA_FILE using 4:xtic(1) title columnheader(4), \
     DATA_FILE using 5:xtic(1) title columnheader(5)
set output "performance_16.png"
plot DATA_FILE using 2:xtic(1) title columnheader(2), \
     DATA_FILE using 3:xtic(1) title columnheader(3), \
     DATA_FILE using 4:xtic(1) title columnheader(4), \
     DATA_FILE using 5:xtic(1) title columnheader(5), \
     DATA_FILE using 15:xtic(1) title columnheader(15), \
     DATA_FILE using 20:xtic(1) title columnheader(20)
