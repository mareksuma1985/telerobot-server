CFLAGS=`pkg-config --cflags gtk+-2.0 gstreamer-0.10 gstreamer-interfaces-0.10`
LIBS=`pkg-config --libs gtk+-2.0 gstreamer-0.10 gstreamer-interfaces-0.10`


all: telerobot_server

telerobot_server: telerobot_server.c
	gcc $(CFLAGS) $< -o $@ $(LIBS)

clean:
	rm -f *.o telerobot_server
