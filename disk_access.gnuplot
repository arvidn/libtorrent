set term png size 1200,700
set output "disk_access.png"
set xrange [0:*]
set xlabel "time (ms)"
set ylabel "disk offset"
set key box
plot  "disk_access.log" using 1:3 title "disk access locations" with steps
