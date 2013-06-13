/* @@@LICENSE
*
*      Copyright (c) 2008-2013 LG Electronics, Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */

/*
* Novacom project
* $URL$
* $Rev$
* $Date$
* $Id$
*/
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <poll.h>
#include <termios.h>
#include <fcntl.h>

#include "socket.h"
#include "packet.h"
#include "packet_struct.h"
#include "tcprelay.h"
#include "base64.h"
#include "cksum.h"

//#define DEBUG_NOVACOM
/* nduid */
#define NOVACOM_NDUID_BYTELEN	(20)							///< nduid byte array representation
#define NOVACOM_NDUID_CHRLEN	(40)							///< nduid character array representation
#define NOVACOM_NDUID_STRLEN	(NOVACOM_NDUID_CHRLEN + 1)		///< nduid string representation
#define NOVACOM_SESSION_CHRLEN	(10)							///< session id::char
#define NOVACOM_SESSION_STRLEN	(NOVACOM_SESSION_CHRLEN + 1)	///< session_id::str

#define NOVACOM_CTRLPORT		(6971)

#define NOVACOMDMSG_REPLY_UNKCMD	"unrecognized command"
#define NOVACOMDMSG_AUTH_REQUEST	"req:auth"
#define NOVACOMDMSG_REPLY_OK		"ok"

#define NOVACOM_MAX_PACKETSIZE	(4096)

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

static const char *serverip = "localhost";
static int serverport = 6968;
static const char *device = "";
static const char *devicepass = NULL;
static const char *devcmd = NULL;
static char devnduid[NOVACOM_NDUID_STRLEN];
static char devsnid[NOVACOM_SESSION_STRLEN];
static char command[NOVACOM_MAX_PACKETSIZE];
static char response_buf[2048];
static int term_mode = 0;
static int listdev = 0;
static bool waitfordevice = false;
static bool dosignals = false;
static bool dosigwinch = false;
static int signalpipe[2];

//Port Forwarding Options
static int portforwardmode = 0;

/* last tty resize data stored here from SIGWINCH */
static struct winsize_s {
	/* resize data */
	uint16_t rows;
	uint16_t cols;
} winsize;

#define PIPE_READ 0
#define PIPE_WRITE 1

static struct termios oldstdin;
static struct termios oldstdout;
enum CONMODE_e {
	CONMODE_TERMSUPPORT,
	CONMODE_NOECHO,
	CONMODE_ECHO,
};

//proto
static int read_line(int fd, char *buf, size_t buflen);


void usage(int argc, char **argv, int version_only) {
#ifdef BUILDVERSION
	printf("version: %s\n", BUILDVERSION);
#endif
	if (version_only)
		exit(1);
	printf("usage: ");
	printf("%s [-a address] [-p port] [-t] [-l] [-d device] [-c cmd] [-r password] [-w] <command>\n", argv[0]);
	printf("%s [-V]\n", argv[0]);
	printf("%s [-a address] [-p port] -P[ [-f <localport:remoteport,...>] ]\n", argv[0]);
	printf("options:\n");
	printf("\t-a address: ip address of the novacomd server, default is 'localhost'\n");
	printf("\t-p port: port of the novacomd server's device list port, default is 6968\n");
	printf("\t-t: go into terminal mode, for interactive use\n");
	printf("\t-s: pass signals to remote process\n");
	printf("\t-l: list devices and then exit\n");
	printf("\t-r: device password\n");
	printf("\t-c: service command [login, add, remove, logout]\n");
	printf("\t\t  login:  opens new session\n");
	printf("\t\t  add:    adds device token to host\n");
	printf("\t\t  remove: remove device token from host\n");
	printf("\t\t  logout: closes active session\n");
	printf("\t-d device: connect to specific device instead of first.\n");
	printf("\t\t might be <nduid>, <connection type>, <device type>\n");
	printf("\t-w: wait for device to show up before running command\n");
	printf("\t-V: version information\n");
	printf("\t-P: Port Forwarding Enabled\n");
	printf("\t-f: ports to forward\n");
	exit(1);
}

/* restore attributes */
static void resetconsole(void) {
	tcsetattr(0, TCSANOW, &oldstdin);
	tcsetattr(1, TCSANOW, &oldstdout);
}

/* save current attributes */
static void saveconsole(void) {
	tcgetattr(0, &oldstdin);
	tcgetattr(1, &oldstdout);

	atexit(&resetconsole);
}

/* set console attribute */
static void setconsole(int mode) {
	struct termios ti;
	struct termios to;

	/* clear */
	memset(&ti, 0, sizeof(ti));
	memset(&ti, 0, sizeof(to));

	/* grab current attributes */
	if ( 0 > tcgetattr(0, &ti) )
		return;
	if ( 0 > tcgetattr(1, &to) )
		return;

	/* set console to terminal mode */
	if (CONMODE_TERMSUPPORT == mode) {
		ti.c_lflag = ISIG; // no input processing
		// Don't interpret various control characters, pass them through instead
		ti.c_cc[VINTR] = ti.c_cc[VQUIT] = ti.c_cc[VSUSP] = '\0';

		to.c_lflag = ISIG; // no output processing
		// Don't interpret various control characters, pass them through instead
		to.c_cc[VINTR] = to.c_cc[VQUIT] = to.c_cc[VSUSP] = '\0';
	} else if (CONMODE_NOECHO == mode) {
		/*no echo mode */
		ti.c_lflag &= ~(ECHO);
		to.c_lflag &= ~(ECHO);
	} else if (CONMODE_ECHO == mode ) {
		ti.c_lflag &= ECHO;
		to.c_lflag &= ECHO;
	}

	/* set new attributes */
	tcsetattr(0, TCSANOW, &ti);
	tcsetattr(1, TCSANOW, &to);
}

static void handle_winsize_change(void) {
	struct winsize ws;
	if (ioctl(1, TIOCGWINSZ, &ws) == 0) {
		//fprintf(stderr, "ws_row %d ws_col %d\n", ws.ws_row, ws.ws_col);
		winsize.rows = ws.ws_row;
		winsize.cols = ws.ws_col;
	}
}

void signal_to_pipe(int signo) {
	sigset_t signals;
	sigemptyset(&signals);
	ssize_t bytesRead = read(signalpipe[PIPE_READ], &signals, sizeof(sigset_t));
	if (-1 == bytesRead && (EAGAIN != errno) && (EINTR != errno)) {
		fprintf(stderr, "\n\n%s: Error(%i) reading from signal pipe\n\n", __func__, errno);
	}
	sigaddset(&signals, signo);
	write(signalpipe[PIPE_WRITE], &signals, sizeof(sigset_t));

	if (signo == SIGWINCH) {
		// process winsize change
		handle_winsize_change();
	}
}


/*
 * @breif parse command line options
 */
int parse_opt(int argc, char *argv[]) {
	int rc = 0;
	const struct option long_options[] = { 
		{ "address",   1, 0, 'a' },
		{ "device",    1, 0, 'd' },
		{ "list",      0, 0, 'l' },
		{ "port",      1, 0, 'p' },
		{ "terminal",  0, 0, 't' },
		{ "signals",   0, 0, 's' },
		{ "wait",      0, 0, 'w' },
		{ "password",  1, 0, 'r' },
		{ "cmd",       1, 0, 'c' },
		{ 0, 0, 0, 0 },
	};

	for (;;) {
		int option_index = 0;
		int c;
	
		c = getopt_long(argc, argv, "a:d:f:lPp:tswVr:c:", long_options, &option_index);
		if (c == -1)
			break;
	
		switch (c) {
		case 'a':
			serverip = optarg;
			break;
		case 'd':
			device = optarg;
			break;
		case 'r':
			devicepass = optarg;
			break;
		case 'c':
			devcmd = optarg;
			break;
		case 'l':
			listdev = 1;
			break;
		case 'f':
			if (parsePortsToBeForwarded(optarg)) {
				fprintf(stderr, "Port Forwarding params were not correct\n");
				usage(argc, argv, 0);
				rc = 1;
			}
			break;
		case 'p':
			serverport = atoi(optarg);
			break;
		case 'P':
			portforwardmode = 1;
			break;
		case 't':
			term_mode = 1;
			dosigwinch = true;
			break;
		case 's':
			dosignals = true;
			dosigwinch = true;
			break;
		case 'w':
			waitfordevice = true;
			break;
		case 'V':
			usage(argc, argv, 1);
			break;
		default:
			fprintf(stderr, "?? getopt returned character code 0x%x ??\n", c);
			usage(argc, argv, 0);
			break;
		}
	}

	return rc;
}

int parse_response(int fd)
{
	int rc;

	/* wait for a response */
	rc = read_line(fd, response_buf, sizeof(response_buf));
	if (rc < 0) {
		fprintf(stderr, "failed to read response\n");
		return -1;
	}

	/* password required */
	if(0 == strncmp(response_buf, NOVACOMDMSG_AUTH_REQUEST, strlen(NOVACOMDMSG_AUTH_REQUEST))) {
		fprintf(stderr, "password required\n");
		return -1;
	}

	/* check reply */
	if(0 != strncmp(response_buf, NOVACOMDMSG_REPLY_OK, strlen(NOVACOMDMSG_REPLY_OK))) {
		if( 0 == strncmp(response_buf, NOVACOMDMSG_REPLY_UNKCMD, strlen(NOVACOMDMSG_REPLY_UNKCMD)) ) {
			fprintf(stderr, "unknown command\n");
		} else {
			fprintf(stderr, "bad or error response from other side: '%s'\n", response_buf);
		}
		return -1;
	}

	/* success */
	return 0;
}

int read_line(int fd, char *buf, size_t buflen) {
	size_t pos;

	for (pos = 0; pos < buflen - 1; pos++) {
		int err = recv(fd, &buf[pos], 1, 0);
		if (err <= 0)
			break;

		if (buf[pos] == '\n')
			break;
	}

	buf[pos] = 0;

	return (int) pos;
}

static void list_devices(int devlistfd) {
	char buf[256];
	int rc;

	for (;;) {
		int port;
		char dev[NOVACOM_NDUID_STRLEN];
		char contype[24];
		char devtype[32];
		char sessionid[12];

		rc = read_line(devlistfd, buf, sizeof(buf));
		if (rc <= 0)
			break;

		rc = sscanf(buf, "%d %40s %20s %30s %10s", &port, dev, contype, devtype, sessionid);
		if (rc < 4)
			continue; // bad line

		/* user passed -d && -l: simple filter */
		if(device && strlen(device)) {
			dev[NOVACOM_NDUID_CHRLEN] = 0;
			/* print string only for matching entries */
			if(   (strcasecmp(device, dev) == 0 )
				||(strcasecmp(device, contype) == 0 )
				||(strcasecmp(device, devtype) == 0 ) ) {
					puts(buf);
			} else {
				continue;
			}
		} else {
			puts(buf);
		}
	}
}

static int find_device(int devlistfd, const char *device) {
	char buf[256];
	int rc;

	for (;;) {
		int port = -1;
		int match = 0;
		char dev[NOVACOM_NDUID_STRLEN];
		char contype[24];
		char devtype[32];
		char sessionid[12];

		rc = read_line(devlistfd, buf, sizeof(buf));
		if (rc <= 0)
			break;

		dev[0] = 0;
		contype[0] = 0;
		devtype[0] = 0;
		sessionid[0] = 0;

		rc = sscanf(buf, "%d %40s %20s %30s %10s", &port, dev, contype, devtype, sessionid);
		if (rc < 4)
			continue; // bad line

		dev[NOVACOM_NDUID_CHRLEN] = 0;
		/* if null or zero device was passed in to match, return the first result */
		/* otherwise if the strings match, return it */
		if (device == NULL || strcmp(device, "") == 0 || strcasecmp(device, dev) == 0) {
			match = 1;
		} else if(device && strcasecmp(device, contype) == 0) { /* connection type */
			match = 1;
		} else if(device && strcasecmp(device, devtype) == 0) { /* device type */
			match = 1;
		}

		/* matches on of the filters ? */
		if (match) {
			strncpy(devnduid, dev, NOVACOM_NDUID_CHRLEN);
			devnduid[NOVACOM_NDUID_CHRLEN] = 0;
			if( strlen(sessionid) ) {
				strncpy(devsnid, sessionid, NOVACOM_SESSION_CHRLEN);
				devsnid[NOVACOM_SESSION_CHRLEN] = 0;
			}
			return port;
		}
	}

	return -1;
}

int init_sockets() {
	int retVal = 0;
	return retVal;
}

/*
 * get password
 */
static char *getpsw()
{
	char *pwd = (char *)calloc(65, 1);

	/* no memory */
	if (!pwd)
		return NULL;

	fprintf(stdout, "Please enter password: ");

	setconsole(CONMODE_NOECHO);
	scanf("%64s", pwd);
	setconsole(CONMODE_ECHO);
	//fprintf(stdout, "\npassword: %s\n", pwd);
	fprintf(stdout, "\n");
	return pwd;
}

/*
 * prepare command
 */
static int prepare_cmd(const char *cmd, size_t cmdsize)
{
	int rc = 1;
	char *pwd = NULL;

	/* enter password from console if required */
	if (strlen(devsnid) && !devicepass) {
		pwd = getpsw();
		if (pwd) {
			devicepass = pwd;
		}
	}

	do {
		/* session + password */
		if (strlen(devsnid) && devicepass && strlen(devicepass) ) {
			int rc;
			SHA1Context ctx;
			int len;
			char hash[sizeof(ctx.Message_Digest_Str) + 1];
			memset(hash, 0, sizeof(hash));
	
			/* calc SHA1(nduid, password)*/
			memset(hash, 0, sizeof(hash));
			len = snprintf(command, sizeof(command), "%s%s", devnduid, devicepass);
			memset((void *)devicepass, 0, strlen(devicepass));
			//fprintf(stderr, "1:command %s\n", command);
			if(len < 0) {
				fprintf(stderr, "error(%d)\n", errno);
				break;
			}
			SHA1Reset(&ctx);
			SHA1Input(&ctx, (const unsigned char *)command, len);
			rc = SHA1Result(&ctx);
			/* calc SHA1(nduid,password,session */
			len = snprintf(command, sizeof(command), "%.*s%s",
					sizeof(ctx.Message_Digest_Str), ctx.Message_Digest_Str, devsnid);
			//fprintf(stderr, "2:command %s\n", command);
			if(len < 0) {
				fprintf(stderr, "error(%d)\n", errno);
				break;
			}
			SHA1Reset(&ctx);
			SHA1Input(&ctx, (const unsigned char *)command, len);
			rc = SHA1Result(&ctx);
			/* results */
			memcpy(hash, ctx.Message_Digest_Str, sizeof(ctx.Message_Digest_Str));
			/* command */
			snprintf(command, sizeof(command), "%.*s dev://%s %s", cmdsize, cmd, devnduid, hash);
		} else if ( strlen(devsnid) ){
			fprintf(stderr, "Please specify password\n");
			break;
		} else {
			fprintf(stderr, "Device does not require authentication\n");
			break;
		}
		/* no errors */
		rc = 0;
	} while(0);

	/*  */
	if (pwd) {
		free(pwd);
	}

	return rc;
}


/*
*  data transfer loop
*  @ret -1 abnormal termination, 0 - no errors
*/
int data_xfer( int fd ) {

	int rc = -1;
	/* Set this nonblock so we don't wedge up on writing */
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	fd_set rfds, wfds;
	bool listen_stdio = true;
	bool stdout_closed = false;
	bool writeblock = false;

	char *xwbuf = (char*) malloc(NOVACOM_MAX_PACKETSIZE + sizeof(struct packet_header));
	char *wbuf = xwbuf + sizeof(struct packet_header);
	size_t written, wsize;

	assert(xwbuf);

	if (dosigwinch) {
		handle_winsize_change();
		wsize = prepare_packet_term_resize(xwbuf, winsize.rows, winsize.cols);
		written = 0;
		writeblock = true;
	}

	while (1) {
		int maxfd = 0;
		struct timeval timeout;
		memset(&timeout, 0, sizeof(struct timeval));

		FD_ZERO(&rfds);
		FD_ZERO(&wfds);

		if (writeblock) {
			FD_SET(fd, &wfds);
			maxfd = MAX(maxfd, fd);
		} else if (listen_stdio) {
			FD_SET(STDIN_FILENO, &rfds);
			maxfd = MAX(maxfd, STDIN_FILENO);
			timeout.tv_sec = 0;
			timeout.tv_usec = 5000;
		}
		FD_SET(fd, &rfds);
		maxfd = MAX(maxfd, fd);

		if ((dosignals || dosigwinch) && !writeblock) {
			FD_SET(signalpipe[PIPE_READ], &rfds);
			maxfd = MAX(maxfd, signalpipe[PIPE_READ]);
		}

		int err = select(maxfd + 1, &rfds, &wfds, NULL, &timeout);
		if ((-1 == err) && ((EAGAIN == errno) || (EINTR == errno))) {
			continue;
		}

		if (FD_ISSET(fd, &rfds)) {
			char *rbuf;
			size_t rsize;
			int retcode;

			enum packet_recv_code result = packet_recv_something(fd, &rbuf, &rsize, &retcode);
			//fprintf(stderr, "after recv: res %d, rbuf %p, rsize %u, retcode %u\n", result, rbuf, rsize, retcode);
			switch (result) {
			case RECV_ERR_BAD_DATA:
				fprintf(stderr, "\nbad data from other end\n");
				goto out;
			case RECV_ERR_CLOSED_SOCKET:
				fprintf(stderr, "\nnovacomd socket was closed prematurely\n");
				goto out;
			case RECV_RESULT_NODATA:
				break;
			case RECV_RESULT_STDOUT:
				if (stdout_closed) {
					fprintf(stderr, "\nReceived data for stdout after closing stdout\n");
				} else {
					write(STDOUT_FILENO, rbuf, rsize);
				}
				break;
			case RECV_RESULT_STDERR:
				write(STDERR_FILENO, rbuf, rsize);
				break;
			case RECV_RESULT_CLOSE_STDOUT:
				close(STDOUT_FILENO);
				stdout_closed = true;
				break;
			case RECV_RESULT_CLOSE_STDERR:
				//fprintf(stderr, "closing stderr\n");
				//close(STDERR_FILENO);
				break;
			case RECV_RESULT_RETURN:
				//fprintf(stderr, "returning with err code %d\n", retcode);
				rc = retcode;
				goto out;
				break;
			}
		}
		if (FD_ISSET(fd, &wfds)) {
			// Partial write to the novacom socket, try to complete
			err = send(fd, xwbuf + written, wsize - written, 0);
			if ((err == 0) || ((err == -1) && (errno != EAGAIN))) {
				// Don't bail, drop through in case we can read
				writeblock = false;
				listen_stdio = false;
			}
			if (err > 0) {
				written += err;
			}
			if (written == wsize) {
				writeblock = false;
				wsize = 0;
				written = 0;
			}
		}

		if (writeblock)
			continue;

		if ((dosignals || dosigwinch) && FD_ISSET(signalpipe[PIPE_READ], &rfds)) {
			int i;
			sigset_t signals;
			sigemptyset(&signals);

			ssize_t bytesRead = read(signalpipe[PIPE_READ], &signals, sizeof(sigset_t));

			if (-1 != bytesRead && (EINTR == errno)) {
				for (i = 1; i < NSIG; i++) {
					if (sigismember(&signals, i)) {
						if (i == SIGWINCH) { // SIGWINCHes go through as a special resize packet
							wsize = prepare_packet_term_resize(xwbuf, winsize.rows, winsize.cols);
						} else {
							wsize = prepare_packet_signal(xwbuf, i);
						}
						written = 0;
						writeblock = true;

						// write it back to the pipe in case there's more signals to handle
						sigdelset(&signals, i);
						write(signalpipe[PIPE_WRITE], &signals, sizeof(sigset_t));

						break;
					}
				}
				if (writeblock)
					continue;
			} else {
				fprintf(stderr, "%s: Error reading from signal pipe: %i\n", __func__, errno);
			}
		}
		if (FD_ISSET(STDIN_FILENO, &rfds))
		{
			//printf("stdin ready\n");
			err = read(STDIN_FILENO, wbuf, NOVACOM_MAX_PACKETSIZE);
			if (err <= 0) {
				// stdin went empty, stop listening on it
				listen_stdio = false;
				wsize = prepare_packet_eof(xwbuf, STDIN_FILENO);
				written = 0;
				writeblock = true;
				close(STDIN_FILENO);
				continue;
			} else {
				wsize = prepare_packet_data(xwbuf, err);
				written = 0;
				writeblock = true;
				continue;
			}
		}
	}

out:
	free(xwbuf);

	return rc;
}


int main(int argc, char **argv) {
	int rc = 0;
	int devport = -1;

	/* */
	memset(devnduid, 0, sizeof(devnduid));
	memset(devsnid, 0, sizeof(devsnid));

	/* options */
	rc = parse_opt(argc, argv);
	if (rc) {
		return rc;
	}

	/* save current attributes */
	saveconsole();

	/* sockets */
	rc = init_sockets();
	if (rc) {
		return 1;
	}

	do {
		/* get the device list */
		int devlistfd = connect_socket(serverip, serverport);
		if (devlistfd < 0) {
			fprintf(stderr, "failed to connect to server\n");
			return 1;
		}

		/* dump the list if we're in device list mode */
		if (listdev) {
			list_devices(devlistfd);
			return 0;
		}

		/* find our device (or the default one) and return the port to connect to */
		devport = find_device(devlistfd, device);

		/* we do not need the device list port anymore */
		close(devlistfd);

		if (devport < 0) {
			if (waitfordevice) {
				/* wait a bit and retry */
				usleep(500000);
				continue;
			} else {
				fprintf(stderr, "unable to find device\n");
				return 1;
			}
		}

		/* found device, abort the loop */
		break;
	} while (1);

	//Port Forwarding Mode Begins
	if (portforwardmode) {
		initTcpRelay(serverip, devport);

		startTcpRelay();

		return 0;
	}

	/* if we're in device list mode, dont bother extracting the command */
	if (!listdev && !devcmd) {
		if (argc - optind < 1) {
			usage(argc, argv, 0);
		}

		/* construct the command from the rest of the arguments */
		int i;
		command[0] = 0;
		for (i = optind; i < argc; i++) {
			if (i != optind)
				strcat(command, " ");
			strcat(command, argv[i]);
		}
		strcat(command, "\n");
#ifdef DEBUG_NOVACOM
		printf("command is %s\n", command);
#endif
	} else if (devcmd) {
		int rc;
		/*host control cmd */
		if( !strncmp(devcmd, "list", 4)) {
			snprintf(command, sizeof(command), "list host://");
			devport = NOVACOM_CTRLPORT;
		} else if ( !strncmp(devcmd, "login", 5)) { /*device control cmd */
			rc = prepare_cmd(devcmd, 5);
			if (!rc) {
				devport = NOVACOM_CTRLPORT;
			} else {
				return rc;
			}
		} else if (!strncmp(devcmd, "logout", 6)) { /*device control cmd */
			/* command */
			snprintf(command, sizeof(command), "logout dev://%s\n", devnduid);
			devport = NOVACOM_CTRLPORT;
		} else if (!strncmp(devcmd, "add", 3)) { /*device control cmd */
			rc = prepare_cmd(devcmd, 3);
			if (!rc) {
				devport = NOVACOM_CTRLPORT;
			} else {
				return rc;
			}
		} else if (!strncmp(devcmd, "remove", 6)) { /*device control cmd */
			rc = prepare_cmd(devcmd, 6);
			if (!rc) {
				devport = NOVACOM_CTRLPORT;
			} else {
				return rc;
			}
		} else {
			fprintf(stderr, "unsupported command(%s)\n", devcmd);
			return 1;
		}
	}

	/* connect to the device port */
	int fd = connect_socket(serverip, devport);
	if (fd < 0) {
		fprintf(stderr, "failed to connect to server\n");
		return 1;
	}

	/* put the tty into interactive mode */
	if (term_mode) {
		setconsole(CONMODE_TERMSUPPORT);
	}

	/* signals */
	if (dosignals || dosigwinch) {
		struct sigaction sa;
		int retVal = pipe(signalpipe);
		if (-1 != retVal) {
			fcntl(signalpipe[PIPE_READ], F_SETFL, fcntl(signalpipe[PIPE_READ], F_GETFL) | O_NONBLOCK);

			memset(&sa, 0, sizeof(sa));
			sa.sa_handler = &signal_to_pipe;
			sa.sa_flags = SA_RESTART;

			// install signal handlers
			if (dosigwinch)
				sigaction(SIGWINCH, &sa, NULL);

			if (dosignals) {
				sigaction(SIGINT, &sa, NULL);
				sigaction(SIGHUP, &sa, NULL);
				sigaction(SIGQUIT, &sa, NULL);
				sigaction(SIGTERM, &sa, NULL);
			}
		} else {
			fprintf(stderr, "failed to establish pipe \n");
			close(fd);
			return 1;
		}
	}

	/* send the command */
	if ( send(fd, command, strlen(command), 0) < 0) {
		fprintf(stderr, "novacom: unable to send command to server\n");
	}

	/* parse it */
	if (parse_response(fd) < 0) {
		close(fd);
		return 1;
	} else if(devcmd) {
		/* command executed, just exit */
		close(fd);
		return 0;
	}

	rc = data_xfer(fd);
	if(rc != 0) {
		fprintf(stderr, "novacom: unexpected EOF from server\n");
	}
	close(fd);
	return rc;
}
