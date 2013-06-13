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

#ifndef __PACKET_H
#define __PACKET_H

int prepare_packet_data(void * buf, size_t size);
int prepare_packet_eof(void * buf, int fileno);
int prepare_packet_signal(void * buf, int signo);
int prepare_packet_term_resize(void *buf, int rows, int cols);

/* called to process incoming data
 * returns:
 *  0  = no output data
 *  >0 = result code below, length or return code returned in argument
 *  <0 = parse error
 */
enum packet_recv_code {
	RECV_ERR_BAD_DATA = -2,
	RECV_ERR_CLOSED_SOCKET = -1,
	RECV_RESULT_NODATA = 0,
	RECV_RESULT_STDOUT = 1,
	RECV_RESULT_STDERR = 2,
	RECV_RESULT_CLOSE_STDOUT = 3,
	RECV_RESULT_CLOSE_STDERR = 4,
	RECV_RESULT_RETURN       = 5
};

enum packet_recv_code packet_recv_something(int fd, char **outbuf, size_t *outsize, int *return_code);

#endif

