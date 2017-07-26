#include "ledvideo.h"


// All the animations we might play.  The pointers are to the
// first frame of each animation sequence

#define MAX_ANIMATIONS  21
static animation_frame *animations[MAX_ANIMATIONS];

// The animations we are currently playing.  The pointers are to
// the frame that is currently playing.

#define MAX_PLAYING     8
static animation_frame *playing[MAX_PLAYING];
static int playing_microsec[MAX_PLAYING];


void begin_animation(int num)
{
	int i, count;
	animation_frame *frame;
	animation_frame *newplaying[MAX_PLAYING];
	int new_microsec[MAX_PLAYING];

	if (animations[num] == NULL) return;
	// first, check if this animation is already playing
	for (i=0; i < MAX_PLAYING; i++) {
		for (frame = animations[num]; frame; frame = frame->next) {
			if (playing[i] == frame) {
				printf("restart animation %d on slot %d\n", num, i);
				playing[i] = animations[num];
				playing_microsec[i] = 0;
				return;
			}
		}
	}
	printf("begin_animation %d\n", num);
	// place the animation at the beginning of the currently playing list
	newplaying[0] = animations[num];
	new_microsec[0] = 0;
	count = 1;
	// copy all the others still current playing
	for (i=0; i < MAX_PLAYING && count < MAX_PLAYING; i++) {
		if (playing[i]) {
			newplaying[count] = playing[i];
			new_microsec[count] = playing_microsec[count];
			count++;
		}
	}
	for (; count < MAX_PLAYING; count++) {
		newplaying[count] = NULL;
		playing_microsec[count] = 0;
	}
	// update the actual lists
	memcpy(playing, newplaying, sizeof(animation_frame *) * MAX_PLAYING);
	memcpy(playing_microsec, new_microsec, sizeof(int) * MAX_PLAYING);
}

void set_animation(int num, animation_frame *anim)
{
	animation_frame *old;
	int i;

	if (num >= MAX_ANIMATIONS) return;
	printf("set_animation %d\n", num);
	// if this slot had an animation assigned, check all of its
	// frames against the currently playing list.  We don't want
	// future video updates to access deleted memory
	if (animations[num]) {
		for (old = animations[num]; old; old = old->next) {
			for (i=0; i < MAX_PLAYING; i++) {
				if (playing[i] == old) playing[i] = NULL;
			}
		}
		free_anim_list(animations[num]);
	}
	animations[num] = anim;
	// TODO: show visual indication on LEDs somehow...
}




// Convert RGB image data to OctoWS2811 raw data format
//   img points to 2880 bytes, RGB image size 60x16
//   data points to raw output for OctoWS2811 VideoDisplay
void image2data(const unsigned char *img, unsigned char *data)
{
	int i, x, y, xbegin, xinc, xend;
	uint32_t pixel[8], mask;
	const unsigned char *p;
	unsigned char b;
	const int linesPerPin=2, layout=0, width=60;

	for (y=0; y < linesPerPin; y++) {
		if ((y & 1) == (layout ? 0 : 1)) {
			xbegin = 0;
			xend = width;
			xinc = 1;
		} else {
			xbegin = width-1;
			xend = -1;
			xinc = -1;
		}
		for (x = xbegin; x != xend; x += xinc) {
			for (i=0; i < 8; i++) {
				// fetch 8 pixels from the image, 1 for each pin
				p = img + ((x * 3) + (y + i * 2) * 60 * 3);
				pixel[i] = (p[0] << 8) | (p[1] << 16) | (p[2] << 0);
			}
			// convert 8 pixels to 24 bytes
			for (mask = 0x800000; mask != 0; mask >>= 1) {
				b = 0;
				for (i=0; i < 8; i++) {
					if ((pixel[i] & mask) != 0) b |= (1 << i);
				}
				*data++ = b;
			}
		}
	}
}




#define MIN(x, y) (((x) < (y)) ? (x) : (y))


void animation_overlay(unsigned char *img, const animation_frame *anim)
{
	int x, y, xend, yend;
	int red, green, blue, alpha;
	const unsigned char *src;
	unsigned char *dest;

	xend = MIN(anim->width, 60);
	yend = MIN(anim->height, 32);

	for (y=0; y < yend; y++) {
		src = &(anim->data[y * anim->width * 4]);
		//dest = &(img[y * 60 * 3]);
		dest = &(img[y * 60 * 3 + 59 * 3]);
		for (x=0; x < xend; x++) {
			red = *src++;
			green = *src++;
			blue = *src++;
			alpha = *src++;
			if (alpha > 0) {
				*dest++ = red;
				*dest++ = green;
				*dest++ = blue;
			} else {
				dest += 3;
			}
			dest -= 6;
		}
	}
}




// Called for each new video frame to display
//   buf points to a 320x240 RGB frame buffer (230400 bytes)
void video_frame(const unsigned char *buf, int elapsed_usec)
{
	unsigned char *p;
	unsigned char scaled[5760];
	unsigned char data_top[60*16*3 + 3];
	unsigned char data_bottom[60*16*3 + 3];
	int x, y, i, j, r, g, b, n;
	const int usec = 18000;
	animation_frame *anim;
	static int firstscale=1;

	//printf("video_frame\n");
	printf(".");
	fflush(stdout);
	if (buf) {
		// first, scale a 300x160 section from the center to 60x32 (exactly 5X reduction)
		// TODO: this could be a better scaling algorithm and maybe even apply sharpening?
		p = scaled;
		for (y=0; y<32; y++) {
			for (x=0; x<60; x++) {
				r = g = b = 0;
				i = (y * 5 + 40) * 960 + (x * 5 + 10) * 3;
				for (j=0; j<5; j++) {
					r += buf[i+0] + buf[i+3] + buf[i+6] + buf[i+9] + buf[i+12];
					g += buf[i+1] + buf[i+4] + buf[i+7] + buf[i+10] + buf[i+13];
					b += buf[i+2] + buf[i+6] + buf[i+8] + buf[i+11] + buf[i+14];
					i += 960;
				}
				*p++ = r / 25;
				*p++ = g / 25;
				*p++ = b / 25;
			}
		}
	} else {
		// webcam is offline.. keep updating with blank data
		memset(scaled, 0, sizeof(scaled));
	}

	// add any animations on top of the scaled image
	for (i=MAX_PLAYING-1; i >= 0; i--) {
		anim = playing[i];
		if (anim == NULL) continue;
		printf("do animation slot %d\n", i);
		int usec = playing_microsec[i];
		if (usec == 0) {
			printf("  initial show\n");
			animation_overlay(scaled, anim);
			playing_microsec[i] = 1;
		} else {
			usec += elapsed_usec;
			printf("  shown for %d us\n", usec);
			while (anim && usec > anim->delay) {
				printf("  usec > %d (this frame)\n", anim->delay);
				usec -= anim->delay;
				anim = anim->next;
			}
			playing[i] = anim;
			if (usec < 1) usec = 1;
			playing_microsec[i] = usec;
			if (anim) animation_overlay(scaled, anim);
		}
	}

	// convert the 60x32 RGB data to OctoWS2811 format
	if (led_top_fd >= 0 && led_bottom_fd >= 0) {
		// if both boards are online, top is master, bottom is slave
		data_top[0] = '*';
		data_top[1] = usec & 255;
		data_top[2] = (usec >> 8) & 255;
		image2data(scaled, data_top + 3);
		data_bottom[0] = '%';
		data_bottom[1] = 0;
		data_bottom[2] = 0;
		image2data(scaled + 2880, data_bottom + 3);
	} else if (led_top_fd >= 0) {
		// only top is online, it's the master
		data_top[0] = '*';
		data_top[1] = usec & 255;
		data_top[2] = (usec >> 8) & 255;
		image2data(scaled, data_top + 3);
	} else if (led_bottom_fd >= 0) {
		// only bottom is online, it's the master
		data_bottom[0] = '*';
		data_bottom[1] = usec & 255;
		data_bottom[2] = (usec >> 8) & 255;
		image2data(scaled + 2880, data_bottom + 3);
	}

	// actually transmit the data
	if (led_top_fd >= 0) {
		n = write(led_top_fd, data_top, sizeof(data_top));
		if (n != sizeof(data_top)) {
			// TODO: handle error...
		}
	}
	if (led_bottom_fd >= 0) {
		n = write(led_bottom_fd, data_bottom, sizeof(data_bottom));
		if (n != sizeof(data_bottom)) {
			// TODO: handle error...
		}
	}

	if (firstscale) {
#if 0
		FILE *f;
		f = fopen("out.ppm", "wb");
		if (f) {
			fprintf(f, "P6\n320 240 255\n");
			fwrite(buf, 230400, 1, f);
			fclose(f);
		}
		f = fopen("scale.ppm", "wb");
		if (f) {
			fprintf(f, "P6\n60 32 255\n");
			fwrite(scaled, 5760, 1, f);
			fclose(f);
		}
#endif
		firstscale = 0;
	}
}





static void xioctl(int fh, int request, void *arg)
{
	int r;

	do {
		r = v4l2_ioctl(fh, request, arg);
	} while (r == -1 && ((errno == EINTR) || (errno == EAGAIN)));
	if (r == -1) die("error %d, %s\n", errno, strerror(errno));
}

// Called when udev detects the webcam.
//   name is the "/dev/video#" device file name to open
void new_video_device(const char *name)
{
	struct v4l2_capability capability;
	struct v4l2_format format;
	int fd;

	printf("new_video_device: %s\n", name);

	// open the video device using v4l2 wrapper library
	//fd = v4l2_open(name, O_RDWR | O_NONBLOCK, 0);
	fd = v4l2_open(name, O_RDWR, 0);
	if (fd < 0) {
		printf("unable to open %s", name);
		return;
	}

	// make sure it has the capabilities we need
	xioctl(fd, VIDIOC_QUERYCAP, &capability);
	if (!(capability.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		printf("device %s does not support video capture", name);
		v4l2_close(fd);
		return;
	}
	if (!(capability.capabilities & V4L2_CAP_READWRITE)) {
		printf("device %s does not support read/write API", name);
		v4l2_close(fd);
		return;
	}

	// fetch the default video capture settings
	memset(&format, 0, sizeof(format));
	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	xioctl(fd, VIDIOC_G_FMT, &format);

	// request these settings
	format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
	format.fmt.pix.field = 1;
	format.fmt.pix.width = 320;
	format.fmt.pix.height = 240;
	xioctl(fd, VIDIOC_S_FMT, &format);
	if (format.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB24) {
		printf("unable to capture in RGB24 format");
		v4l2_close(fd);
		return;
	}
	if (format.fmt.pix.field != 1) {
		printf("unable to capture in progressive fields");
		v4l2_close(fd);
		return;
	}
	if (format.fmt.pix.width != 320 || format.fmt.pix.height != 240) {
		printf("unable to capture at 320x240 size\n");
		v4l2_close(fd);
		return;
	}
	printf("image size      %dx%d\n", format.fmt.pix.width, format.fmt.pix.height);
	printf("bytes per line  %d\n", format.fmt.pix.bytesperline);
	printf("bytes per frame %d\n", format.fmt.pix.sizeimage);

	// everything is good, so use this video device
	if (webcam_fd >= 0) v4l2_close(webcam_fd);
	webcam_fd = fd;
}


