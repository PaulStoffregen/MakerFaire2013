#include "ledvideo.h"

// for permission to access /dev/video# devices:
// sudo usermod -G video paul
// sudo usermod -G dialout paul

int webcam_fd=-1;
int led_top_fd=-1;
int led_bottom_fd=-1;
int stomp_pads_fd=-1;

static int udev_monitor_fd=-1;
static unsigned char framebuffer[230400];




void new_device(struct udev_device *dev)
{
	const char *devname;
	int n;

	devname = udev_device_get_devnode(dev);
	//printf("new_device: %s\n", devname);
	if (!devname) return;
	if (sscanf(devname, "/dev/ttyACM%d", &n) == 1) {
		new_serial_device(devname);
		return;
	}
	if (sscanf(devname, "/dev/video%d", &n) == 1) {
		new_video_device(devname);
		return;
	}
}

int elapsed_microseconds(void)
{
	static struct timeval prev, now;
	static int initialized=0;
	int elapsed;

	if (!initialized) {
		gettimeofday(&prev, NULL);
		initialized = 1;
		return 0;
	}
	gettimeofday(&now, NULL);

	elapsed = (now.tv_sec - prev.tv_sec) * 1000000;
	elapsed += (now.tv_usec - prev.tv_usec);
	prev.tv_sec = now.tv_sec;
	prev.tv_usec = now.tv_usec;
	//printf("elapsed us=%d\n", elapsed);
	return elapsed;
}


int main()
{
	struct udev *udev=NULL;
	struct udev_monitor *mon=NULL;
	struct udev_enumerate *enumerate;
	struct udev_list_entry *devices, *dev_list_entry;
	struct udev_device *dev;
	struct sigaction sa;
	const char *path, *action;
	struct timeval tv;
	fd_set fds;
	int n, maxfd;
	int elapsed=0;
	char c;

	//set_animation(5, gifload("../star1.gif"));
	//exit(0);

	set_animation(1, gifload("../flare2.gif"));
	set_animation(2, gifload("../dorkbot1.gif"));
	set_animation(3, gifload("../dorkbot2.gif"));
	set_animation(4, gifload("../fill_in_dots_upper_left_red.gif"));
	set_animation(5, gifload("../star1.gif"));
	set_animation(6, gifload("../test.gif"));
	set_animation(7, gifload("../ironingman2.gif"));

	//exit(0);

	// Open the udev monitoring device.  All actual opening of video and
	// serial devices will occur when udev detects them.
	udev = udev_new();
	if (!udev) die("unable to access udev");
	mon = udev_monitor_new_from_netlink(udev, "udev");
	udev_monitor_filter_add_match_subsystem_devtype(mon, "tty", NULL);
	udev_monitor_filter_add_match_subsystem_devtype(mon, "video4linux", NULL);
	udev_monitor_enable_receiving(mon);
	udev_monitor_fd = udev_monitor_get_fd(mon);
	if (udev_monitor_fd < 0) die("unable to init udev monitoring");
	printf("scan devices\n");
	enumerate = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enumerate, "tty");
	udev_enumerate_add_match_subsystem(enumerate, "video4linux");
	udev_enumerate_scan_devices(enumerate);
	devices = udev_enumerate_get_list_entry(enumerate);
	udev_list_entry_foreach(dev_list_entry, devices) {
		path = udev_list_entry_get_name(dev_list_entry);
		if (!path) continue;
		//printf("path: %s\n", path);
		dev = udev_device_new_from_syspath(udev, path);
		if (dev) {
			new_device(dev);
			udev_device_unref(dev);
		}
	}
	udev_enumerate_unref(enumerate);

	// install the signal handler to turn off all LEDs on quit
	sa.sa_handler = &quit;
	sa.sa_flags = SA_RESTART;
	sigfillset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	while (1) {
		FD_ZERO(&fds);
		FD_SET(udev_monitor_fd, &fds);
		if (webcam_fd >= 0) FD_SET(webcam_fd, &fds);
		if (led_top_fd >= 0) FD_SET(led_top_fd, &fds);
		if (led_bottom_fd >= 0) FD_SET(led_bottom_fd, &fds);
		if (stomp_pads_fd >= 0) FD_SET(stomp_pads_fd, &fds);
		if (webcam_fd >= 0) {
			tv.tv_sec = 0;
			tv.tv_usec = 500000;
		} else {
			tv.tv_sec = 0;
			tv.tv_usec = 5000;
		}
		maxfd = udev_monitor_fd;
		if (webcam_fd > maxfd) maxfd = webcam_fd;
		if (led_top_fd > maxfd) maxfd = led_top_fd;
		if (led_bottom_fd > maxfd) maxfd = led_bottom_fd;
		if (stomp_pads_fd > maxfd) maxfd = stomp_pads_fd;
		n = select(maxfd + 1, &fds, NULL, NULL, &tv);
		elapsed += elapsed_microseconds();
		if (n < 0) {
			printf("select error, n=%d\n", n);
			continue;
		}
		if (n == 0) {
			//printf("select timeout, n=%d\n", n);
		}
		if (webcam_fd >= 0) {
			if (FD_ISSET(webcam_fd, &fds)) {
				n = v4l2_read(webcam_fd, framebuffer, 230400);
				if (n == 230400) {
					video_frame(framebuffer, elapsed);
					elapsed = 0;
				} else if (n < 0 && errno != EINTR && errno != EAGAIN) {
					printf("video read error, closing video stream, n=%d\n", n);
					v4l2_close(webcam_fd);
					webcam_fd = -1;
				} else {
					printf("video read, unexpected length, n=%d\n", n);
				}
			}
		} else {
			if (elapsed > 33300) {
				video_frame(NULL, elapsed);
				elapsed = 0;
			}
		}
		if (stomp_pads_fd >= 0 && FD_ISSET(stomp_pads_fd, &fds)) {
			n = read(stomp_pads_fd, &c, 1);
			if (n > 0) {
				printf("stomp: %c\n", c);
				if (c >= 'a' && c <= 'z') {
					begin_animation(c - 'a');
				}
			} else if (n < 0 && errno != EINTR && errno != EAGAIN) {
				printf("error reading stomp pads, n=%d\n", n);
				close(stomp_pads_fd);
				stomp_pads_fd = -1;
			}
		}
		if (led_top_fd >= 0 && FD_ISSET(led_top_fd, &fds)) {
			n = read(led_top_fd, &c, 1);
			if (n > 0) {
				printf("unexpected data received from top half LEDs\n");
			} else if (n < 0 && errno != EINTR && errno != EAGAIN) {
				printf("error with top half LEDs, n=%d\n", n);
				close(led_top_fd);
				led_top_fd = -1;
			}
		}

		if (FD_ISSET(udev_monitor_fd, &fds)) {
			dev = udev_monitor_receive_device(mon);
			if (dev) {
				action = udev_device_get_action(dev);
				printf("udev action: %s\n", action);
				if (strcmp(action, "add") == 0) {
					new_device(dev);
				}
				udev_device_unref(dev);
			}
		}
	}

	return 0;
}

void quit(int num)
{
	unsigned char data[60*16*3 + 3];
	int n;

	printf("\n");
	//printf("\nSignal %d\n", num);
	memset(data, 0, sizeof(data));
	data[0] = '*';
	data[2] = 60;
	if (led_top_fd >= 0) {
		printf("turning off top half LEDs\n");
		n = write(led_top_fd, data, sizeof(data));
		if (n != sizeof(data)) printf("error clearing top LEDs\n");
		tcdrain(led_top_fd);
		close(led_top_fd);
	}
	if (led_bottom_fd >= 0) {
		printf("turning off bottom half LEDs\n");
		n = write(led_bottom_fd, data, sizeof(data));
		if (n != sizeof(data)) printf("error clearing bottom LEDs\n");
		tcdrain(led_bottom_fd);
		close(led_bottom_fd);
	}
	if (stomp_pads_fd >= 0) {
		close(stomp_pads_fd);
	}
	if (webcam_fd >= 0) {
		v4l2_close(webcam_fd);
	}
	exit(0);
}

void die(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	fprintf(stderr, "\n");
	quit(0);
}

