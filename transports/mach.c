/*
 * Copyright 2014-2015 iXsystems, Inc.
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdbool.h>

#include "../xpc_internal.h"

#define SOCKET_DIR "/var/run/xpc"


struct unix_pipe
{
    char *		up_path;
    int			up_socket;
    bool		up_listener;
};

static int
mach_lookup(const char *name, xpc_pipe_t *pipe)
{
	if (flags & XPC_CONNECTION_MACH_SERVICE_LISTENER) {
		kr = bootstrap_check_in(bootstrap_port, name,
					&conn->xc_local_port);
		if (kr != KERN_SUCCESS) {
			errno = EBUSY;
			free(conn);
			return (NULL);
		}

		return (conn);
	}

static int
mach_listen(const char *name, xpc_pipe_t *pipe)
{
	kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
				&conn->xc_local_port);
	if (kr != KERN_SUCCESS) {
		errno = EPERM;
		return (NULL);
	}

	kr = mach_port_insert_right(mach_task_self(), conn->xc_local_port,
				    conn->xc_local_port, MACH_MSG_TYPE_MAKE_SEND);
	if (kr != KERN_SUCCESS) {
		errno = EPERM;
		return (NULL);
	}
}

static int
mach_send(xpc_pipe_t pipe, struct iovec *iov, struct xpc_resource *res, size_t nres)
{

}

static int
mach_recv(xpc_pipe_t pipe, struct iovec *iov, struct xpc_resource **res, size_t *nres)
{
	struct xpc_recv_message message;
	mach_msg_header_t *request;
	kern_return_t kr;
	mig_reply_error_t response;
	mach_msg_trailer_t *tr;
	int data_size;
	struct xpc_object *xo;
	audit_token_t *auditp;
	xpc_u val;

	request = &message.header;
	/* should be size - but what about arbitrary XPC data? */
	request->msgh_size = MAX_RECV;
	request->msgh_local_port = local;
	kr = mach_msg(request, MACH_RCV_MSG |
			       MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0) |
			       MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT),
		      0, request->msgh_size, request->msgh_local_port,
		      MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);

	if (kr != 0)
		LOG("mach_msg_receive returned %d\n", kr);
	*remote = request->msgh_remote_port;
	*id = message.id;
	data_size = message.size;

	tr = (mach_msg_trailer_t *)(((char *)&message) + request->msgh_size);
	auditp = &((mach_msg_audit_trailer_t *)tr)->msgh_audit;

	xo->xo_audit_token = malloc(sizeof(*auditp));
	memcpy(xo->xo_audit_token, auditp, sizeof(*auditp));

	xpc_dictionary_set_mach_send(xo, XPC_RPORT, request->msgh_remote_port);
	xpc_dictionary_set_uint64(xo, XPC_SEQID, message.id);
	*result = xo;
	return (0);
}

static struct xpc_transport unix_transport = {
	.listen = mach_listen,
	.lookup = mach_lookup,
	.get_credentials = mach_get_credentials,
	.send = mach_send,
	.recv = mach_recv
};
