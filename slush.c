/* Simple Linux UART Shell
 *
 * Copyright (C) 2015 Richard Cochran <richardcochran@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define DEVICE	"/dev/ttyS0"
#define BPS	115200
#define BUFLEN	1024

static int annotate, debug, trace;

static int open_serial(char* name, tcflag_t baud, int icrnl, int hwfc)
{
	int fd;
	struct termios nterm;

	fd = open(name, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		fprintf(stderr,"Cannot open %s : %m", name);
		return fd;
	}
	memset(&nterm, 0, sizeof(nterm));

	/* Input Modes */
	nterm.c_iflag = IGNPAR; /* Ignore framing errors and parity errors */
	if (icrnl) {
		/* Translate carriage return to newline on input */
		nterm.c_iflag |= ICRNL;
	}

	/* Output Modes */
	nterm.c_oflag = 0;

	/* Control Modes */
	nterm.c_cflag = baud;
	nterm.c_cflag |= CS8;    /* Character size */
	nterm.c_cflag |= CLOCAL; /* Ignore modem control lines */
	nterm.c_cflag |= CREAD;  /* Enable receiver */
	if (hwfc) {
		/* Enable RTS/CTS (hardware) flow control */
		nterm.c_cflag |= CRTSCTS;
	}

	/* Local Modes */
	if (!debug && !trace) {
		nterm.c_lflag = ICANON; /* Enable canonical mode */
	}

	nterm.c_cc[VTIME] = 10;   /* timeout is 10 deciseconds */
	nterm.c_cc[VMIN] = 1;     /* blocking read until N chars received */
	tcflush(fd, TCIFLUSH);
	tcsetattr(fd, TCSANOW, &nterm);
	return fd;
}

static void debug_show_reply(char *buf, int len)
{
	int i;
	char c;

	for (i = 0; i < len; i++) {
		printf("%02hhx", buf[i]);
	}
	printf("  ");
	for (i = 0; i < len; i++) {
		c = buf[i];
		printf("%c", isprint(c) ? c : '.');
	}
	printf("\n");
}

static uint64_t timestamp(void)
{
	struct timespec ts;
	uint64_t val;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	val = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
	val /= 1000000ULL;

	return val;
}

static void trace_show_reply(char *buf, int len)
{
	static uint64_t lastts, total;
	uint64_t diff, now;
	char c;
	int i;

	now = timestamp();
	if (lastts) {
		diff = now - lastts;
		total += diff;
	} else {
		diff = 0;
	}
	lastts = now;

	printf("%" PRId64 " %" PRId64 " ", total, diff);

	for (i = 0; i < len; i++) {
		c = buf[i];
		printf("%c", isprint(c) ? c : '.');
	}
	printf("\n");
}

static int read_reply(int fd)
{
	char reply[BUFLEN] = {0};
	int cnt;

	cnt = read(fd, reply, sizeof(reply));
	if (cnt < 0) {
		perror("read");
		return -1;
	}

	if (debug) {
		debug_show_reply(reply, cnt);
	} else if (trace) {
		trace_show_reply(reply, cnt);
	} else if (annotate) {
		printf("read %2d bytes {%s}\n", cnt, reply);
	} else {
		printf("%s", reply);
	}
	fflush(stdout);

	return 0;
}

static void usage(char* progname)
{
	fprintf(stderr,
		"Usage: %s [OPTION]...\n"
		"  -a          annotated mode\n"
		"  -b [num]    baud rate in bits per second, default: %d\n"
		"  -c          map CR to NL on input\n"
		"  -d          debug mode\n"
		"  -f          enable hardware flow control\n"
		"  -h          prints this message\n"
		"  -o [0,1,2]  NL mapping on output, 0=none 1=CR 2=CRNL\n"
		"  -p [file]   serial port device to open, default: '%s'\n"
		"  -t          trace mode, with relative time stamps\n",
		progname, BPS, DEVICE);
}

int main(int argc,char *argv[])
{
	char buf[BUFLEN], *device, *progname;
	int bps = BPS, c, num, fd, icrnl = 0, nlmap = 0, hwfc = 0;
	tcflag_t baud = B9600;
	struct pollfd pfd[2];

	/* Handle the command line arguments. */
	progname = strrchr(argv[0],'/');
	progname = progname ? 1+progname : argv[0];
	while (EOF != (c = getopt(argc, argv, "ab:cdfho:p:t"))) {
		switch (c) {
		case 'a':
			annotate = 1;
			break;
		case 'b':
			bps = atoi(optarg);
			break;
		case 'c':
			icrnl = 1;
			break;
		case 'd':
			debug = 1;
			break;
		case 'f':
			hwfc = 1;
			break;
		case 'o':
			nlmap = atoi(optarg);
			break;
		case 'p':
			device = optarg;
			break;
		case 't':
			trace = 1;
			break;
		case 'h':
			usage(progname);
			return 0;
		case '?':
		default:
			usage(progname);
			return -1;
		}
	}
	switch (bps) {
	case 1200:
		baud = B1200;
		break;
	case 1800:
		baud = B1800;
		break;
	case 2400:
		baud = B2400;
		break;
	case 4800:
		baud = B4800;
		break;
	case 9600:
		baud = B9600;
		break;
	case 19200:
		baud = B19200;
		break;
	case 38400:
		baud = B38400;
		break;
	case 57600:
		baud = B57600;
		break;
	case 115200:
		baud = B115200;
		break;
	default:
		usage(progname);
		return -1;
	}

	fd = open_serial(device, baud, icrnl, hwfc);
	if (fd < 0) {
		return -1;
	}
	pfd[0].fd = STDIN_FILENO;
	pfd[0].events = POLLIN | POLLPRI;
	pfd[1].fd = fd;
	pfd[1].events = POLLIN | POLLPRI;

	while (1) {
		num = poll(pfd, 2, -1);
		if (num < 0) {
			perror("poll failed");
			return -1;
		}
		if (!num) {
			perror("unexpected time out");
			return -1;
		}
		if (pfd[0].revents & (POLLIN|POLLPRI)) {
			memset(buf, 0, sizeof(buf));
			if (!fgets(buf, sizeof(buf), stdin))
				break;
			switch (nlmap) {
			case 0:
				break;
			case 1:
				buf[strlen(buf)-1] = '\r';
				break;
			case 2:
				buf[strlen(buf)-1] = '\r';
				buf[strlen(buf)]   = '\n';
				break;
			}
			write(fd, buf, strlen(buf));
		}
		if (pfd[1].revents & POLLERR) {
			fprintf(stderr, "POLLERR\n");
			return -1;
		}
		if (pfd[1].revents & POLLHUP) {
			fprintf(stderr, "POLLHUP\n");
			return -1;
		}
		if (pfd[1].revents & (POLLIN|POLLPRI)) {
			read_reply(fd);
		}
	}

	return 0;
}
