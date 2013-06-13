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

///
/// This program tunnels local sockets to sockets on the device by relaying them through
/// novacomd.  So, if you wanted to telnet to the device, for example, you would telnet to
/// localhost:10023 (this program's listen socket).  This would open a new socket to novacomd,
/// which would then punch the data through to the service on the device
///
/// @todo Add a way to specify services either in a configuration file or the command line instead
///   of hard-coding them
/// @todo Clean up error handling

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <unistd.h>

#include "socket.h"

#define DEVICE_LIST_SOCKET 6968
#define PACKET_MAGIC 0xdecafbad
#define PACKET_VERSION 1
#define PACKET_HEADER_TYPE_DATA 0
#define PACKET_HEADER_TYPE_OOB 2
#define REPORT_ERROR()  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, strerror(errno));
#define closesocket(_x_) close(_x_)
typedef pthread_t ServiceThreadId;

#define MIN(x, y) ((x) < (y) ? (x) : (y))

typedef struct Tunnel Tunnel;
typedef struct Service Service;
typedef struct PacketHeader PacketHeader;

struct Tunnel {
	int localSocketFd;
	int remoteSocketFd;
};

struct Service {
	int fd;
	int remotePort;
	int devport;
	const char * serverip;
};

struct PacketHeader {
	unsigned int magic;
	unsigned int version;
	unsigned int size;
	unsigned int type;
};

struct PortFowardAssociation {
	unsigned int localport;
	unsigned int remoteport;
};

static struct PortFowardAssociation* portforwardlist = NULL;
static unsigned int numportsforwarded = 0;
static const char* serverip = "";
static int devport = 0;

int parse_response(int fd);

int parsePortsToBeForwarded(const char* ports) {
	//fprintf(stdout, "Ports to forward: %s\n", ports);
	int wasparsed = 0;
	//memset(portforwardlist, 0, sizeof(struct PortFowardAssociation));

	char* port = strtok((char*)ports, ",:");
	do {
		struct PortFowardAssociation portpair;
		unsigned long decodedport = strtoul(port, NULL, 10);
		if (INT_MAX < decodedport) {
			wasparsed = 1;
		} else {
			portpair.localport = (unsigned int) decodedport;
			port = strtok(NULL, ",:");
			if (NULL != ports) {
				decodedport = strtoul(port, NULL, 10);
				if (INT_MAX < decodedport) {
					wasparsed = 1;
				} else {
					portpair.remoteport = (unsigned int) decodedport;
				}
			} else {
				wasparsed = 1; //failed to parse properly
			}
		}

		if (0 == wasparsed) {
			numportsforwarded += 1;
			portforwardlist = (struct PortFowardAssociation*)realloc(portforwardlist, (numportsforwarded) * sizeof(struct PortFowardAssociation));
			if (portforwardlist) {
				//fprintf(stdout, "Forwarding Port %i<->%i\n", portpair.localport, portpair.remoteport);
				memcpy(&(portforwardlist[numportsforwarded - 1]), &portpair, sizeof(struct PortFowardAssociation));
			} else {
				wasparsed = 1;
			}
		}
	} while ((0 == wasparsed) && (NULL != (port = strtok(NULL, ",:"))));

	if (0 != wasparsed && portforwardlist) {
		free(portforwardlist);
		portforwardlist = NULL;
	}

	return wasparsed;
}

ServiceThreadId startThread(void *(*startFn)(void*), void *param) {
	ServiceThreadId threadId = 0;
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&threadId, &attr, startFn, param);
	pthread_attr_destroy(&attr);
	return threadId;
}

int readComplete(int socketFd, char *buffer, int length) {
	int totalRead = 0;
	int got;

	while (totalRead < length) {
		got = recv(socketFd, buffer + totalRead, length - totalRead, 0);
		if (got <= 0)
			return got;

		totalRead += got;
	}

	return totalRead;
}

void *deviceToHostThread(void *_castToTunnel) {
	int got;
	PacketHeader header;
	Tunnel *tunnel = (Tunnel*) _castToTunnel;
	char buffer[0x2000];
	unsigned int totalRead;

	signal(SIGPIPE, SIG_IGN);

	printf("device to host thread started\n");
	for (;;) {
		// Read the packet header
		got = readComplete(tunnel->remoteSocketFd, (char*) &header, sizeof(header));
		if (got < 0) {
			REPORT_ERROR();
			break;
		} else if(0==got) {
			break;
		}

		// Transfer the header of the packet
		if (header.version != 1 || header.magic != PACKET_MAGIC) {
			printf("bad packet!\n");
			break;
		}

		// Transfer the body of the packet
		totalRead = 0;
		while (totalRead < header.size) {
			got = readComplete(tunnel->remoteSocketFd, buffer, MIN(sizeof(buffer), header.size - totalRead));
			if (got < 0) {
				REPORT_ERROR();
				goto done;
			}

			if (header.type == PACKET_HEADER_TYPE_DATA) {
				if (send(tunnel->localSocketFd, buffer, got, 0) != got) {
					REPORT_ERROR();
					goto done;
				}
			} else if (header.type == PACKET_HEADER_TYPE_OOB) {
				// This is usually a remote disconnect
				goto done;
			}

			totalRead += got;
		}
	}

done: closesocket(tunnel->localSocketFd);
	free(tunnel);
	printf("device to host thread exited\n");
	return NULL;
}

int sendNovacomPacket(int socket, const unsigned char *data, int length) {
	PacketHeader header;

	header.magic = PACKET_MAGIC;
	header.version = PACKET_VERSION;
	header.size = length;
	header.type = PACKET_HEADER_TYPE_DATA;

	if (send(socket, (const char*)&header, sizeof(header), 0) < (int) sizeof(header)) {
		REPORT_ERROR();
		return -1;
	}

	if (send(socket, (const char*)data, length, 0) < length) {
		REPORT_ERROR();
		return -1;
	}

	return length;
}

void *hostToDeviceThread(void *_castToTunnel) {
	unsigned char buffer[0x2000];
	int got;
	Tunnel *tunnel = (Tunnel*) _castToTunnel;

	signal(SIGPIPE, SIG_IGN);

	printf("host to device thread started\n");
	for (;;) {
		got = recv(tunnel->localSocketFd,(char*) buffer, sizeof(buffer), 0);
		if (got <= 0) {
			REPORT_ERROR();
			break;
		}

		if (sendNovacomPacket(tunnel->remoteSocketFd, buffer, got) < 0) {
			REPORT_ERROR();
			break;
		}
	}

	printf("host to device thread exited\n");
	closesocket(tunnel->remoteSocketFd);
	free(tunnel);
	return NULL;
}

void createTunnel(Service *service) {
	Tunnel *newTunnel;
	char cmd[128];
	char response[128];
	int got;
	int remoteSocket;
	int localSocket;

	localSocket = accept_socket(service->fd);
	if (localSocket < 0) {
		REPORT_ERROR();
		goto error1;
	}

	remoteSocket = connect_socket(service->serverip, service->devport);
	if (remoteSocket < 0)
		goto error2;

	snprintf(cmd, sizeof(cmd), "connect tcp-port://%d\n", service->remotePort);
	printf("%s\n", cmd);
	ssize_t amountSent = send(remoteSocket, cmd, strlen(cmd), 0);
	if ((0 > amountSent) || (strlen(cmd) > (size_t) amountSent)) {
		REPORT_ERROR();
		goto error3;
	}

	got = parse_response(remoteSocket);
	if(got < 0) {
		printf("error attempting to tunnel socket: got response %s\n", response);
		goto error3;
	}

	// kick off the transfer threads.  Each one gets their own copy of the control structure
	newTunnel = (Tunnel*) malloc(sizeof(Tunnel));
	newTunnel->remoteSocketFd = remoteSocket;
	newTunnel->localSocketFd = localSocket;
	startThread(deviceToHostThread, newTunnel);

	newTunnel = (Tunnel*) malloc(sizeof(Tunnel));
	newTunnel->remoteSocketFd = remoteSocket;
	newTunnel->localSocketFd = localSocket;
	startThread(hostToDeviceThread, newTunnel);
	return;

error3: closesocket(remoteSocket);
error2: closesocket(localSocket);
error1: return;
}

void *serviceThread(void *_castToService) {
	Service *service = (Service*) _castToService;

	printf("started service thread\n");
	for (;;)
		createTunnel(service);

	return NULL;
}

ServiceThreadId createService(const char *serverip, int devport, int localPort, int remotePort) {
	Service *service;
	ServiceThreadId threadId = 0;

	service = (Service*) malloc(sizeof(Service));
	if (service == NULL) {
		REPORT_ERROR();
	} else {

		service->fd = create_listen_socket(localPort);
		if (service->fd < 0) {
			REPORT_ERROR();
			free(service);
		} else {

			service->remotePort = remotePort;
			service->serverip = serverip;
			service->devport = devport;
			fprintf(stdout, "created service fd %d local: %d remote %d\n", service->fd, localPort, remotePort);

			threadId = startThread(serviceThread, service);
		}
	}
	return threadId;
}

void initTcpRelay(const char *serveripparam, int devportparam) {
	serverip = serveripparam;
	devport = devportparam;
}

void startTcpRelayService() {
	signal(SIGPIPE, SIG_IGN);

	unsigned int x = 0;
	for (; x < numportsforwarded; x++) {
		fprintf(stdout, "Forwarding Port %i<->%i\n", portforwardlist[x].localport, portforwardlist[x].remoteport);
		createService(serverip, devport, portforwardlist[x].localport, portforwardlist[x].remoteport);
	}

	//    createService(serverip, devport, 10022, 22); /// SSH
	//    createService(serverip, devport, 12345, 2345); /// For remote GDB
	//    createService(serverip, devport, 12346, 2346); /// For PVRTune
}

void startTcpRelay() {

	startTcpRelayService();

	for (;;) { sleep(100000); }
}
