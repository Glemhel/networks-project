#!/bin/bash
gcc -pthread -o app application.c
gnome-terminal -e "./app 0 1" &
gnome-terminal -e "./app 1 0" &
gnome-terminal -e "./app 2 0" &
#for i in {1..1}
#do
#    gnome-terminal -e ./peer$i $i 0
#done