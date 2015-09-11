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
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <dispatch/dispatch.h>
#include <xpc/xpc.h>

#include "../xpc_internal.h"

#define SOCKET_DIR "/var/run/xpc"


struct unix_port
{
    	int socket;
	struct sockaddr_un sun;
};

static int
unix_lookup(const char *name, xpc_port_t *port)
{
	struct sockaddr_un addr;
	struct unix_port *ret;
	struct stat st;
	char *path;

	asprintf(&path, "%s/%s", SOCKET_DIR, name);

	if (stat(path, &st) != 0) {
		return (-1);
	}

	if (!(st.st_mode & S_IFSOCK)) {
		return (-1);
	}

	ret = malloc(sizeof(struct unix_port));
	ret->socket = -1;
	ret->sun.sun_family = AF_UNIX;
	ret->sun.sun_len = sizeof(struct sockaddr_un);
	strncpy(ret->sun.sun_path, path, sizeof(ret->sun.sun_path));

	*port = ret;
	return (0);
}

static int
unix_listen(const char *name, xpc_port_t *port)
{
	struct sockaddr_un addr;
	struct unix_port *ret;
	struct stat st;
	char *path;
	int nb = 1;

	if (name == NULL) {
		asprintf(&path, "%s/anonymous.XXXXXXXX", SOCKET_DIR);
		path = mktemp(path);
	} else
		asprintf(&path, "%s/%s", SOCKET_DIR, name);

	unlink(path);

	ret = malloc(sizeof(struct unix_port));
	ret->sun.sun_family = AF_UNIX;
	ret->sun.sun_len = sizeof(struct sockaddr_un);
	strncpy(ret->sun.sun_path, path, sizeof(ret->sun.sun_path));

	ret->socket = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (ret->socket == -1) {
		free(ret);
		return (-1);
	}

	if (bind(ret->socket, (struct sockaddr *)&ret->sun, sizeof(struct sockaddr_un)) != 0) {
		debugf("bind failed: %s", strerror(errno));
		free(ret);
		return (-1);
	}

	//if (setsockopt(ret->socket, 0, LOCAL_CREDS, &nb, sizeof(nb)) == -1) {
	//	debugf("setsockopt failed: %s", strerror(errno));
	//	free(ret);
	//	return (-1);
	//}

	*port = ret;
	return (0);
}

static int
unix_release(xpc_port_t port)
{
	struct unix_port *uport = (struct unix_port *)port;

	if (uport->socket != -1) {
		close(uport->socket);
		unlink(uport->sun.sun_path);
	}

	free(uport);
	return (0);
}

static char *
unix_port_to_string(xpc_port_t port)
{
	struct unix_port *uport = (struct unix_port *)port;
	char *ret;

	if (uport->socket != -1)
		asprintf(&ret, "<%s [%d]>", uport->sun.sun_path, uport->socket);
	else
		asprintf(&ret, "<%s>", uport->sun.sun_path);

	return (ret);
}

static int
unix_port_compare(xpc_port_t p1, xpc_port_t p2)
{
	struct unix_port *up1 = (struct unix_port *)p1;
	struct unix_port *up2 = (struct unix_port *)p2;

	return (strncmp(up1->sun.sun_path, up2->sun.sun_path, sizeof up1->sun.sun_path));
}

static dispatch_source_t
unix_create_source(xpc_port_t port, dispatch_queue_t tq)
{
	struct unix_port *uport = (struct unix_port *)port;
	dispatch_source_t source;

	return (dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, (uintptr_t)uport->socket, 0, tq));
}

static int
unix_send(xpc_port_t local, xpc_port_t remote, struct iovec *iov, int niov, struct xpc_resource *res, size_t nres)
{
	struct unix_port *local_port = (struct unix_port *)local;
	struct unix_port *remote_port = (struct unix_port *)remote;
	struct msghdr msg;
	union {
	    struct cmsghdr hdr;
	    struct cmsgcred cred;
	} cmsg;

	debugf("local=%s, remote=%s, msg=%p, size=%ld",
	    unix_port_to_string(local), unix_port_to_string(remote),
	    iov->iov_base, iov->iov_len);

	msg.msg_name = &remote_port->sun;
	msg.msg_namelen = remote_port->sun.sun_len;
	msg.msg_iov = iov;
	msg.msg_iovlen = niov;
	msg.msg_control = (caddr_t) &cmsg;
	msg.msg_controllen = CMSG_SPACE(sizeof(cmsg));
	cmsg.hdr.cmsg_type = SCM_CREDS;
	cmsg.hdr.cmsg_level = SOL_SOCKET;
	cmsg.hdr.cmsg_len = CMSG_LEN(sizeof(struct cmsgcred));

	if (sendmsg(local_port->socket, &msg, 0) < 0)
		return (-1);

	return (0);
}

static int
unix_recv(xpc_port_t local, xpc_port_t *remote, struct iovec *iov, int niov,
    struct xpc_resource **res, size_t *nres, struct xpc_credentials *creds)
{
	struct unix_port *port = (struct unix_port *)local;
	struct unix_port *remote_port;
	struct msghdr msg;
	struct cmsgcred *recv_creds;
	ssize_t recvd;
	union {
	    struct cmsghdr hdr;
	    struct cmsgcred cred;
	} cmsg;

	remote_port = malloc(sizeof(struct unix_port));
	remote_port->socket = -1;

	msg.msg_name = &remote_port->sun;
	msg.msg_namelen = sizeof(struct sockaddr_un);
	msg.msg_iov = iov;
	msg.msg_iovlen = niov;
	msg.msg_control = (caddr_t) &cmsg;
	msg.msg_controllen = CMSG_SPACE(sizeof(cmsg));
	memset(&cmsg, 0, sizeof(cmsg));

	recvd = recvmsg(port->socket, &msg, 0);
	if (recvd < 0)
		return (-1);

	recv_creds = (struct cmsgcred *)CMSG_DATA(&cmsg);
	creds->xc_remote_pid = recv_creds->cmcred_pid;
	creds->xc_remote_euid = recv_creds->cmcred_euid;
	creds->xc_remote_guid = recv_creds->cmcred_gid;

	*remote = (xpc_port_t *)remote_port;
	debugf("local=%s, remote=%s, msg=%p, len=%ld", unix_port_to_string(local), unix_port_to_string(*remote), iov->iov_base, recvd);
	debugf("remote pid=%d, uid=%d, gid=%d", recv_creds->cmcred_pid, recv_creds->cmcred_uid, recv_creds->cmcred_gid);

	return (0);
}

struct xpc_transport unix_transport = {
    	.xt_name = "unix",
	.xt_listen = unix_listen,
	.xt_lookup = unix_lookup,
	.xt_release = unix_release,
    	.xt_port_to_string = unix_port_to_string,
    	.xt_port_compare = unix_port_compare,
    	.xt_create_source = unix_create_source,
	.xt_send = unix_send,
	.xt_recv = unix_recv
};
