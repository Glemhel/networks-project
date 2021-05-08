#!/bin/bash
# remove all previous log files
ls | grep -P "peer[0-9]{1}log.txt" | xargs -d"\n" rm
gcc -pthread -o app application.c
gnome-terminal -e "./app 0 1" &
gnome-terminal -e "./app 1 0" &
gnome-terminal -e "./app 2 0" &
gnome-terminal -e "./app 3 0" &
gnome-terminal -e "./app 4 0" &
gnome-terminal -e "./app 5 0" &
gnome-terminal -e "./app 6 0" &
gnome-terminal -e "./app 7 0" &
#for i in {1..1}
#do
#    gnome-terminal -e ./peer$i $i 0
#done