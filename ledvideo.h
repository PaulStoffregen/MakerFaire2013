#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <libv4lconvert.h>      // sudo apt-get install libv4l-dev
#include <libv4l1-videodev.h>
#include <libv4l2.h>
#include <libudev.h>            // sudo apt-get install libudev-dev

extern int webcam_fd;
extern int led_top_fd;
extern int led_bottom_fd;
extern int stomp_pads_fd;

typedef struct animation_struct {
	int width;
	int height;
	int delay;
	struct animation_struct *next;
	unsigned char data[];
} animation_frame;

void video_frame(const unsigned char *buf, int elapsed_usec);
void begin_animation(int num);
void set_animation(int num, animation_frame *anim);
void new_video_device(const char *name);

void new_serial_device(const char *devname);

animation_frame * gifload(const char *filename);
void free_anim_list(animation_frame *first);

void die(const char *format, ...) __attribute__ ((format (printf, 1, 2)));
void quit(int num);



