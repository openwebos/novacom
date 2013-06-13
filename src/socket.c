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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include <termios.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>

#include "socket.h"

int create_listen_socket(int port)
{
	/* create a socket */
	int s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		return -1;
	//set reuse.
	int reuse_addr = 1;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse_addr, sizeof(reuse_addr)) < 0) {
		close(s);
		return -1;
	}

	/* bind it to a local address */
	struct sockaddr_in saddr;
	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(port);
	if (bind(s, (const struct sockaddr *)&saddr,  sizeof(saddr)) < 0) {
		close(s);
		return -1;
	}

	/* start the socket listening */
	if (listen(s, SOMAXCONN) < 0) {
		close(s);
		return -1;
	}

	return s;
}

int accept_socket(int socket)
{
	int s = socket;
	int new_s;

	struct sockaddr saddr;
	memset(&saddr, 0, sizeof(saddr));
	socklen_t len = sizeof(saddr);

	new_s = accept(s, &saddr, &len);
	if(new_s < 0)
		return new_s;

	return new_s;
}

int connect_socket(const char *address, int port)
{
	struct hostent *addr;

//	printf("connect_socket: address %s port %d\n", address, port);

	addr = gethostbyname(address);
	if (!addr)
		return -1;

//	printf("h_name %s\n", addr->h_name);
//	printf("h_addrtype %d\n", addr->h_addrtype);
//	printf("h_length %d\n", addr->h_length);

	if (addr->h_addrtype != AF_INET)
		return -1; // can't deal with ipv6 yet

	struct sockaddr_in saddr;
	memset(&saddr, 0, sizeof saddr);

	saddr.sin_family = AF_INET;
	memcpy(&saddr.sin_addr, addr->h_addr_list[0], addr->h_length);
	saddr.sin_port = htons(port);

	int new_s = socket(PF_INET, SOCK_STREAM, 0);
//	printf("got socket %d\n", new_s);
	if (new_s < 0)
		return new_s;

	int err = connect(new_s, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in));
//	printf("connect returns %d\n", err);
	if (err < 0) {
		close(new_s);
		return err;
	}

	return new_s;
}

