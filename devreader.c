/*
    devreader reads everything from device, then save to a file
    Copyright (C) 2016  Wei Shuai <cpuwolf@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>


#define TRACE_PORT_NAME "ttyACM1"
#define BAUDRATE	B115200

#define DEBUG 1
#define BUF_LEN 4096
#define TRUE 1
#define FALSE 0

unsigned int is_exit = FALSE;

int wait_for_device(const char *filename)
{
	int fd, wd;
	char buf[BUF_LEN];
	ssize_t len, i = 0;

	if(is_exit) return -1;

	fd = inotify_init();
	if (fd == -1) {
		perror("inotify_init");
		exit(-1);

	}

	wd = inotify_add_watch(fd, "/dev", IN_CREATE);
	if (fd == -1) {
		perror("inotify_add_watch");
		exit(-1);
	}

	while(1) {
		len = read(fd, buf, BUF_LEN);
		while (i < len) {
			struct inotify_event *event = (struct inotify_event *)&buf[i];

			if ((event->mask & IN_CREATE) && (strcmp(event->name, filename) == 0)) {
				printf("%s is created\n", filename);
				inotify_rm_watch(fd, wd);
				close(fd);
				return 1;
			}

			/* update the index to the start of the next event */
			i += sizeof (struct inotify_event) + event->len;
		}
		i = 0;
	}
}

static int open_device(const char *devname)
{
	int h, retval;
	char buf[32];
	struct termios t;

	strcpy(buf, "/dev/");
	strcat(buf, devname);

	/* open device */
	if ((h = open(buf, O_RDWR|O_NONBLOCK)) == -1) {
		perror("open");
		return -1;
	}
	
#if 0
	if ((retval = tcgetattr(h, &t)) < 0) {
		perror("tcgetattr");
		return -1;
	}
	
	cfsetispeed(&t, (speed_t)BAUDRATE);
	cfsetospeed(&t, (speed_t)BAUDRATE);

	t.c_cflag |= (CLOCAL | CREAD | CS8 | HUPCL);
	t.c_cflag &= ~(PARENB | CSIZE | CSTOPB);
	t.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
	t.c_iflag &= ~(INLCR | ICRNL | IGNCR | IXON | IXOFF);
	t.c_iflag |= IGNPAR;
	t.c_oflag &= ~(OPOST | ONLRET | ONOCR | OCRNL | ONLCR);

	if ((retval = tcsetattr(h, TCSANOW, &t)) < 0) {
		perror("tcsetattr");
		return -1;
	}
#endif
	return h;			
}

static int open_log_file(const char *filename)
{
	int retval;

	if ((retval = open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0644)) == -1) {
		perror("open tracelog");
	}

	return retval;	
}

void sig_handler(int sig)
{
	if(sig == SIGINT) {
		is_exit = TRUE;
	}
}

void create_logfile_name(char * fname)
{
	time_t tt;
	struct tm *t;
	char timestr[100];

	tt = time(NULL);
	t = localtime(&tt);
	sprintf(timestr, "devreader_%04d_%02d_%02d_%02d%02d%02d.bin", t->tm_year+1900, t->tm_mon, t->tm_mday, 
		t->tm_hour, t->tm_min, t->tm_sec);
	strcpy(fname, timestr);
}

int process_port(void)
{
	unsigned int total_len;
	int h, fd, ret;
	char buf[BUF_LEN];
	char filename[64];
	struct pollfd event;

#if 1
	printf("wait for /dev/%s\n", TRACE_PORT_NAME);
	wait_for_device(TRACE_PORT_NAME);
	/* wait device become stable */
	sleep(1);
#endif

	h = open_device(TRACE_PORT_NAME);
	if (h < 0) {
		printf("open tracelog device failed\n");
		goto exit_3;
	}

	create_logfile_name(filename);
	printf("trace file name: %s\n", filename);
	fd = open_log_file(filename);
	if (fd < 0) {
		printf("open tracelog failed\n");
		goto exit_2;
	}
	event.fd = h;
	event.events = POLLIN;

	total_len = 0;
	while (!is_exit) {
		ret = poll(&event, 1, 5000);
		if(ret < 0) {
			printf("poll device failed\n");
			break;
		}
		if(event.revents & POLLERR) {
			printf("\npoll device error\n");
			break;
		}
		if(!(event.revents & POLLIN)) {
			//printf("poll timeout\n");
			continue;
		}
		ret = read(h, buf, BUF_LEN);
		if (ret == -1) {
			if(errno == EAGAIN) {
				continue;
			}
			printf("read tracelog device failed\n");
			break;
		}
		if (ret == 0) {
			printf("no trace data available, reading again\n");
			continue;
		}
		ret = write(fd, buf, ret);
		if (ret == -1) {
			printf("write tracelog file failed\n");
			break;
		}
		total_len += ret;
		if(ret > 0)
			printf("written: %d bytes to %s\r", total_len, filename);
	}

	close(fd);
exit_2:
	close(h);
exit_3:
	return -1;
}


int main(void)
{
	signal(SIGINT, sig_handler);
	while(!is_exit) {
		process_port();
	}
	return 0;
}


