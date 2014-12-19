#!/bin/bash
touch ./telerobot_server.c
gcc `pkg-config --cflags gtk+-2.0 gstreamer-0.10 gstreamer-interfaces-0.10` telerobot_server.c -o telerobot_server `pkg-config --libs gtk+-2.0 gstreamer-0.10 gstreamer-interfaces-0.10`
read x
