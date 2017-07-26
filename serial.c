#include "ledvideo.h"


int receive_bytes(int port, char *buf, int len, double timeout)
{
        int r;
        int retry=0;
        long timeout_sec = (long)timeout;
        long timeout_usec = (long)((timeout - (double)timeout_sec) * 1000000.0);

        if (len < 1) return -2;
        // non-blocking read mode
        fcntl(port, F_SETFL, fcntl(port, F_GETFL) | O_NONBLOCK);
        while (1) {
                r = read(port, buf, len);
                //printf("read, r = %d\n", r);
                if (r < 0 && errno != EAGAIN && errno != EINTR) return -1;
                else if (r > 0) return r;
                else {
                        // no data available right now, must wait
                        fd_set fds;
                        struct timeval t;
                        FD_ZERO(&fds);
                        FD_SET(port, &fds);
                        t.tv_sec = timeout_sec;
                        t.tv_usec = timeout_usec;
                        //t.tv_sec = 60;
                        //t.tv_usec = 0;
                        r = select(port+1, &fds, NULL, NULL, &t);
                        //printf("select, r = %d\n", r);
                        if (r < 0) return -1;  // error
                        if (r == 0) return 0; // timeout
                }
                retry++;
                if (retry > 1000) return -100; // no input
        }
        fcntl(port, F_SETFL, fcntl(port, F_GETFL) & ~O_NONBLOCK);
}

const char *receive_line(int port, double timeout)
{
        static char buf[8192];
        static int buflen=0;
        static char *end=NULL;
        int n;

        if (end) {
                n = buflen - (int)(end - buf + 1);
                if (n > 0) {
                        //printf("memmove, buflen=%d, n=%d\n", buflen, n);
                        memmove(buf, end + 1, n);
                        buflen = n;
                } else {
                        //printf("memmove, buflen=%d\n", buflen);
                        buflen = 0;
                }
        }
        while (1) {
                if (buflen >= sizeof(buf)) {
                        end = buf + sizeof(buf) - 1;
                        buflen = sizeof(buf);
                } else {
                        //end = memchr(buf, 0, buflen);
                        //if (!end) end = strchr(buf, '\n');
                        end = strchr(buf, '\n');
                }
                if (end) {
                        //printf("string %d bytes, buflen=%d\n", (int)(end - buf), buflen);
                        *end = 0;
                        if (end > buf && *(end-1) == '\r') *(end-1) = 0;
                        return buf;
                }
                //printf("buf:");
                //for (n=0; n<buflen; n++) {
                        //printf("%d,", buf[n]);
                //}
                //printf("\n");
                n = receive_bytes(port, buf + buflen, sizeof(buf) - buflen, timeout);
                if (n < 0) return NULL; // die("error while receiving data, n=%d\n", n);
                if (n == 0) return NULL; // die("timeout receiving data\n");
                buflen += n;
        }
}


void new_serial_device(const char *devname)
{
	char c;
	int n, fd=-1;
	int led_width, led_height, led_layout, unused1, unused2;
	int video_xoffset, video_yoffset, video_width, video_height;
	int unused3, unused4, unused5;
	struct termios term;
	const char *str;

	printf("new_serial_device: %s\n", devname);
	fd = open(devname, O_RDWR);
	if (fd < 0) return;
	if (tcgetattr(fd, &term) < 0) {
		printf("unable to get terminal settings");
		goto fail;
	}
	cfmakeraw(&term);
	if (tcsetattr(fd, TCSANOW, &term) < 0) {
		printf("unable to set terminal settings");
		goto fail;
	}

	c = '?';
	n = write(fd, &c, 1);
	if (n != 1) goto fail;
	printf("send query\n");
	str = receive_line(fd, 0.5);
	if (!str) goto fail;
	printf("query response: \"%s\"\n", str);
	if (strstr(str, "Stomp Pads")) {
		if (stomp_pads_fd >= 0) close(stomp_pads_fd);
		printf("Stomp Pads\n");
		stomp_pads_fd = fd;
		return;
	}
	if (sscanf(str, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
	  &led_width, &led_height, &led_layout, &unused1, &unused2,
	  &video_xoffset, &video_yoffset, &video_width, &video_height,
	  &unused3, &unused4, &unused5) != 12) {
		goto fail;
	}
	if (led_width == 60 && led_height == 16 && led_layout == 0 &&
	  video_xoffset == 0 && video_yoffset == 0 &&
	  video_width == 100 && video_height == 50) {
		if (led_top_fd >= 0) close(led_top_fd);
		printf("LEDs top half\n");
		led_top_fd = fd;
		return;
	}
	if (led_width == 60 && led_height == 16 && led_layout == 0 &&
	  video_xoffset == 0 && video_yoffset == 50 &&
	  video_width == 100 && video_height == 50) {
		if (led_bottom_fd >= 0) close(led_bottom_fd);
		printf("LEDs bottom half\n");
		led_bottom_fd = fd;
		return;
	}
fail:
	printf("not recognized\n");
	if (fd > 0) close(fd);
	return;
}

