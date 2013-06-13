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

#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include "packet.h"
#include "packet_struct.h" /* protocol level definition */

#define MIN(x,y) ((x)<(y)?(x):(y))

void hexdump8(const void *ptr, size_t len)
{
	unsigned long address = (unsigned long)ptr;
	size_t count;
	size_t i;

	for (count = 0 ; count < len; count += 16) {
		fprintf(stderr, "0x%08lx: ", address);
		for (i=0; i < MIN(16, len - count); i++) {
			fprintf(stderr, "0x%02hhx ", *(const unsigned char *)(address + i));
		}
		fprintf(stderr, "\n");
		address += 16;
	}	
}

int prepare_packet_data(void * buf, size_t size)
{
	struct packet_header *h = (struct packet_header*)buf;

	h->magic = PACKET_HEADER_MAGIC;
	h->version = PACKET_HEADER_VERSION;
	h->size = size;
	h->type = PACKET_HEADER_TYPE_DATA;

	return size + sizeof(struct packet_header);
}

int prepare_packet_signal(void * buf, int signo)
{
	struct packet_header *h = (struct packet_header*)buf;
	struct packet_oob_msg *m = (struct packet_oob_msg*)((char*)buf + sizeof(struct packet_header));

	h->magic = PACKET_HEADER_MAGIC;
	h->version = PACKET_HEADER_VERSION;
	h->size = sizeof(struct packet_oob_msg);
	h->type = PACKET_HEADER_TYPE_OOB;

	m->message = PACKET_OOB_SIGNAL;
	m->data.signo = signo;

	return sizeof(struct packet_header) + sizeof(struct packet_oob_msg);
}

int prepare_packet_term_resize(void *buf, int rows, int cols)
{
	struct packet_header *h = (struct packet_header*)buf;
	struct packet_oob_msg *m = (struct packet_oob_msg*)((char*)buf + sizeof(struct packet_header));

	h->magic = PACKET_HEADER_MAGIC;
	h->version = PACKET_HEADER_VERSION;
	h->size = sizeof(struct packet_oob_msg);
	h->type = PACKET_HEADER_TYPE_OOB;

	m->message = PACKET_OOB_RESIZE;
	m->data.resize.rows = rows;
	m->data.resize.cols = cols;

	return sizeof(struct packet_header) + sizeof(struct packet_oob_msg);
}

int prepare_packet_eof(void * buf, int fileno)
{
	struct packet_header *h = (struct packet_header*)buf;
	struct packet_oob_msg *m = (struct packet_oob_msg*)((char*)buf + sizeof(struct packet_header));

	h->magic = PACKET_HEADER_MAGIC;
	h->version = PACKET_HEADER_VERSION;
	h->size = sizeof(struct packet_oob_msg);
	h->type = PACKET_HEADER_TYPE_OOB;

	m->message = PACKET_OOB_EOF;
	m->data.fileno = fileno;

	return sizeof(struct packet_header) + sizeof(struct packet_oob_msg);
}

enum packet_recv_code packet_recv_something(int fd, char **outbuf, size_t *outsize, int *return_code)
{
	static size_t bodycounter = 0, headercounter = 0;
	static struct packet_header h;
	static struct packet_oob_msg m;
	static char packet_buf[64*1024];
	char *ptr;
	int readsize;

	if (headercounter < sizeof(h)) {
		ptr = (char *)&h;
		readsize = recv(fd, &ptr[headercounter], sizeof(h) - headercounter,0);
		if (readsize <= 0) 
			goto conn_fail;

		headercounter += readsize;
		// Don't wait around for the header to fill in
		if (headercounter < sizeof(h)) {
			return RECV_RESULT_NODATA;
		}
		bodycounter = 0;
	}

	if (h.magic != PACKET_HEADER_MAGIC) {
		fprintf(stderr, "Bad packet header magic\n");
		return RECV_ERR_BAD_DATA;
	}
	if (h.version != PACKET_HEADER_VERSION) {
		fprintf(stderr, "Unsupported packet version\n");
		return RECV_ERR_BAD_DATA;
	}

//	fprintf(stderr, "packet_recv: headercounter %u, bodycounter %u\n", headercounter, bodycounter);
//	hexdump8(&h, headercounter);

	// At this point we have a full header, so process it.
	switch (h.type) {
		case PACKET_HEADER_TYPE_DATA:
		case PACKET_HEADER_TYPE_ERR:
			// stdout or stderr, return the data
			readsize = recv(fd, packet_buf, MIN(h.size - bodycounter, sizeof(packet_buf)),0);
			if (readsize <= 0) 
				goto conn_fail;

			bodycounter += readsize;

			if (bodycounter == h.size) {
				// reset for next packet
				headercounter = 0;
			}

			*outbuf = packet_buf;
			*outsize = readsize;
			if (h.type == PACKET_HEADER_TYPE_DATA)
				return RECV_RESULT_STDOUT;
			else
				return RECV_RESULT_STDERR;
		break;
		case PACKET_HEADER_TYPE_OOB:
		// oob data, handle message
			readsize = MIN(h.size - bodycounter, sizeof(m) - bodycounter);

			if (readsize > 0) { // some amount of message left to read
				ptr = (char *)&m;
				readsize = recv(fd, &ptr[bodycounter], readsize,0);
				if (readsize <= 0) 
					goto conn_fail;

				bodycounter += readsize;

				if (bodycounter == h.size) {
					// reset for next packet
					headercounter = 0;
				}
			
				if (bodycounter == sizeof(m)) {
//					fprintf(stderr, "packet OOB message %u\n", m.message);
//					hexdump8(&m, bodycounter);
					switch (m.message) {
						case PACKET_OOB_EOF:
							if (m.data.fileno == STDOUT_FILENO) {
								return RECV_RESULT_CLOSE_STDOUT;
							} else {
								return RECV_RESULT_CLOSE_STDERR;
							}
							break;
						case PACKET_OOB_RETURN:
							*return_code = m.data.returncode;
							return RECV_RESULT_RETURN;
							break;
						case PACKET_OOB_SIGNAL:
							fprintf(stderr, "Got a signal %d from the other end. That ain't right!\n", m.data.signo);
							return RECV_RESULT_NODATA;
						case PACKET_OOB_RESIZE:
							fprintf(stderr, "Got a resize from the other end\n");
							return RECV_RESULT_NODATA;
						default:
							fprintf(stderr, "unknown OOB message: %d\n", m.message);
							return RECV_RESULT_NODATA;
					}
				}
				return RECV_RESULT_NODATA;
			} else { 
				// Stray data after the oob message
				fprintf(stderr, "oversized oob message. sizeof(m) = %zd, h.size = %d, current pos = %zd\n", sizeof(m), h.size, bodycounter);
				readsize = recv(fd, packet_buf, MIN(h.size - bodycounter, sizeof(packet_buf)),0);
				if (readsize <= 0) 
					goto conn_fail;

				bodycounter += readsize;

				if (bodycounter == h.size) {
					// reset for next packet
					headercounter = 0;
				}
				return RECV_RESULT_NODATA;
			}
		break;
		default:
		// unknown packet type, discard data
			fprintf(stderr, "unknown packet, discarding data\n");
			readsize = recv(fd, packet_buf, MIN(h.size - bodycounter, sizeof(packet_buf)),0);
				if (readsize <= 0) 
					goto conn_fail;

			bodycounter += readsize;

			if (bodycounter == h.size) {
				// reset for next packet
				headercounter = 0;
			}
			return RECV_RESULT_NODATA;
		break;
	}

	fprintf(stderr, "fell through the packet receive logic somehow\n");
	return RECV_ERR_BAD_DATA;

conn_fail:
	return RECV_ERR_CLOSED_SOCKET;
}
