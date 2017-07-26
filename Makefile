
OBJS = ledvideo.o video.o serial.o gif_lib.o gif_read.o
LIBS = -lv4l2 -ludev

CC = gcc
CFLAGS = -O2 -Wall -Iinc

ledvideo: $(OBJS)
	gcc -O2 -Wall -o ledvideo $(OBJS) $(LIBS)

clean:
	rm -f ledvideo *.o *.ppm
