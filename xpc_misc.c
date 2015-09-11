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

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/sbuf.h>
#include <machine/atomic.h>
#include <assert.h>
#include <syslog.h>
#include <stdarg.h>
#include "xpc/xpc.h"
#include "xpc_internal.h"

static void xpc_copy_description_level(xpc_object_t obj, struct sbuf *sbuf,
    int level);

extern struct xpc_transport unix_transport;
static struct xpc_transport *selected_transport = &unix_transport;

void
fail_log(const char *exp)
{
	syslog(LOG_ERR, "%s", exp);
	//sleep(1);
	printf("%s", exp);
	//abort();
}

struct xpc_transport *
xpc_get_transport()
{
	return (selected_transport);
}

static void
xpc_dictionary_destroy(struct xpc_object *dict)
{
	struct xpc_dict_head *head;
	struct xpc_dict_pair *p, *ptmp;

	head = &dict->xo_dict;

	TAILQ_FOREACH_SAFE(p, head, xo_link, ptmp) {
		TAILQ_REMOVE(head, p, xo_link);
		xpc_object_destroy(p->value);
		free(p);
	}
}

static void
xpc_array_destroy(struct xpc_object *dict)
{
	struct xpc_object *p, *ptmp;
	struct xpc_array_head *head;

	head = &dict->xo_array;

	TAILQ_FOREACH_SAFE(p, head, xo_link, ptmp) {
		TAILQ_REMOVE(head, p, xo_link);
		xpc_object_destroy(p);
	}
}

static int
xpc_pack(struct xpc_object *xo, void **buf, size_t *size)
{
	mpack_writer_t writer;

	mpack_writer_init_growable(&writer, (char **)buf, size);
	xpc2mpack(&writer, xo);

	if (mpack_writer_destroy(&writer) != mpack_ok)
		return (-1);

	return (0);
}

static struct xpc_object *
xpc_unpack(void *buf, size_t size)
{
	mpack_tree_t tree;
	struct xpc_object *xo;

	mpack_tree_init(&tree, (const char *)buf, size);
	if (mpack_tree_error(&tree) != mpack_ok) {
		debugf("unpack failed: %d", mpack_tree_error(&tree))
		return (NULL);
	}

	xo = mpack2xpc(mpack_tree_root(&tree));
	return (xo);
}

void
xpc_object_destroy(struct xpc_object *xo)
{
	if (xo->xo_xpc_type == _XPC_TYPE_DICTIONARY)
		xpc_dictionary_destroy(xo);

	if (xo->xo_xpc_type == _XPC_TYPE_ARRAY)
		xpc_array_destroy(xo);

	free(xo);
}

xpc_object_t
xpc_retain(xpc_object_t obj)
{
	struct xpc_object *xo;

	xo = obj;
	atomic_add_int(&xo->xo_refcnt, 1);
	return (obj);
}

void
xpc_release(xpc_object_t obj)
{
	struct xpc_object *xo;

	xo = obj;
	if (atomic_fetchadd_int(&xo->xo_refcnt, -1) > 1)
		return;

	xpc_object_destroy(xo);
}

static const char *xpc_errors[] = {
	"No Error Found",
	"No Memory",
	"Invalid Argument",
	"No Such Process"
};

#if 0
const char *
xpc_strerror(int error)
{

	if (error > EXMAX || error < 0)
		return "BAD ERROR";
	return (xpc_errors[error]);
}
#endif

char *
xpc_copy_description(xpc_object_t obj)
{
	char *result;
	struct sbuf *sbuf;

	sbuf = sbuf_new_auto();
	xpc_copy_description_level(obj, sbuf, 0);
	sbuf_finish(sbuf);
	result = strdup(sbuf_data(sbuf));
	sbuf_delete(sbuf);

	return (result);
}

static void
xpc_copy_description_level(xpc_object_t obj, struct sbuf *sbuf, int level)
{
	struct xpc_object *xo = obj;
	struct uuid *id;
	char *uuid_str;
	uint32_t uuid_status;

	if (obj == NULL) {
		sbuf_printf(sbuf, "<null value>\n");
		return;
	}

	sbuf_printf(sbuf, "(%s) ", _xpc_get_type_name(obj));

	switch (xo->xo_xpc_type) {
	case _XPC_TYPE_DICTIONARY:
		sbuf_printf(sbuf, "\n");
		xpc_dictionary_apply(xo, ^(const char *k, xpc_object_t v) {
			sbuf_printf(sbuf, "%*s\"%s\": ", level * 4, " ", k);
			xpc_copy_description_level(v, sbuf, level + 1);
			return ((bool)true);
		});
		break;

	case _XPC_TYPE_ARRAY:
		sbuf_printf(sbuf, "\n");
		xpc_array_apply(xo, ^(size_t idx, xpc_object_t v) {
			sbuf_printf(sbuf, "%*s%ld: ", level * 4, " ", idx);
			xpc_copy_description_level(v, sbuf, level + 1);
			return ((bool)true);
		});
		break;

	case _XPC_TYPE_BOOL:
		sbuf_printf(sbuf, "%s\n",
		    xpc_bool_get_value(obj) ? "true" : "false");
		break;

	case _XPC_TYPE_STRING:
		sbuf_printf(sbuf, "\"%s\"\n",
		    xpc_string_get_string_ptr(obj));
		break;

	case _XPC_TYPE_INT64:
		sbuf_printf(sbuf, "%ld\n",
		    xpc_int64_get_value(obj));
		break;

	case _XPC_TYPE_UINT64:
		sbuf_printf(sbuf, "%lx\n",
		    xpc_uint64_get_value(obj));
		break;

	case _XPC_TYPE_DATE:
		sbuf_printf(sbuf, "%lu\n",
		    xpc_date_get_value(obj));
		break;	

	case _XPC_TYPE_UUID:
		id = (struct uuid *)xpc_uuid_get_bytes(obj);
		uuid_to_string(id, &uuid_str, &uuid_status);
		sbuf_printf(sbuf, "%s\n", uuid_str);
		free(uuid_str);
		break;

	case _XPC_TYPE_ENDPOINT:
		sbuf_printf(sbuf, "<%ld>\n", xo->xo_int);
		break;

	case _XPC_TYPE_NULL:
		sbuf_printf(sbuf, "<null>\n");
		break;
	}
}

#ifdef MACH
struct _launch_data {
	uint64_t type;
	union {
		struct {
			union {
				launch_data_t *_array;
				char *string;
				void *opaque;
				int64_t __junk;
			};
			union {
				uint64_t _array_cnt;
				uint64_t string_len;
				uint64_t opaque_size;
			};
		};
		int64_t fd;
		uint64_t  mp;
		uint64_t err;
		int64_t number;
		uint64_t boolean; /* We'd use 'bool' but this struct needs to be used under Rosetta, and sizeof(bool) is different between PowerPC and Intel */
		double float_num;
	};
};

static uint8_t ld_to_xpc_type[] = {
	_XPC_TYPE_INVALID,
	_XPC_TYPE_DICTIONARY,
	_XPC_TYPE_ARRAY,
	_XPC_TYPE_FD,
	_XPC_TYPE_UINT64,
	_XPC_TYPE_DOUBLE,
	_XPC_TYPE_BOOL,
	_XPC_TYPE_STRING,
	_XPC_TYPE_DATA,
	_XPC_TYPE_ERROR,
	_XPC_TYPE_ENDPOINT
};
	
xpc_object_t
ld2xpc(launch_data_t ld)
{
	struct xpc_object *xo;
	xpc_u val;


	if (ld->type > LAUNCH_DATA_MACHPORT)
		return (NULL);
	if (ld->type == LAUNCH_DATA_STRING || ld->type == LAUNCH_DATA_OPAQUE) {
		val.str = malloc(ld->string_len);
		memcpy(__DECONST(void *, val.str), ld->string, ld->string_len);
		xo = _xpc_prim_create(ld_to_xpc_type[ld->type], val, ld->string_len);
	} else if (ld->type == LAUNCH_DATA_BOOL) {
		val.b = (bool)ld->boolean;
		xo = _xpc_prim_create(ld_to_xpc_type[ld->type], val, 0);
	} else if (ld->type == LAUNCH_DATA_ARRAY) {
		xo = xpc_array_create(NULL, 0);
		for (uint64_t i = 0; i < ld->_array_cnt; i++)
			xpc_array_append_value(xo, ld2xpc(ld->_array[i]));
	} else {
		val.ui = ld->mp;
		xo = _xpc_prim_create(ld_to_xpc_type[ld->type], val, ld->string_len);	
	}
	return (xo);
}
#endif

#if 0
xpc_object_t
xpc_copy_entitlement_for_token(const char *key __unused, audit_token_t *token __unused)
{
	xpc_u val;

	val.b = true;
	return (_xpc_prim_create(_XPC_TYPE_BOOL, val,0));
}
#endif

int
xpc_pipe_send(xpc_object_t xobj, uint64_t id, xpc_port_t local, xpc_port_t remote)
{
	struct xpc_transport *transport = xpc_get_transport();
	struct iovec iov[2];

	assert(xpc_get_type(xobj) == &_xpc_type_dictionary);

	iov[0].iov_base = (void *)&id;
	iov[0].iov_len = sizeof(uint64_t);

	if (xpc_pack(xobj, &iov[1].iov_base, &iov[1].iov_len) != 0) {
		debugf("pack failed");
		return (-1);
	}

	if (transport->xt_send(local, remote, (struct iovec *)&iov, 2, NULL, 0) != 0) {
		debugf("transport send function failed: %s", strerror(errno));
		return (-1);
	}

	return (0);
}

int
xpc_pipe_receive(xpc_port_t local, xpc_port_t *remote, xpc_object_t *result,
    uint64_t *id, struct xpc_credentials *creds)
{
	struct xpc_transport *transport = xpc_get_transport();
	struct xpc_resource *resources;
	struct iovec iov;
	size_t nresources;

	iov.iov_base = malloc(65535);
	iov.iov_len = 65535;

	if (transport->xt_recv(local, remote, &iov, 1, &resources, &nresources, creds) != 0) {
		debugf("transport receive function failed: %s", strerror(errno));
		return (-1);
	}

	*id = *(uint64_t *)iov.iov_base;
	*result = xpc_unpack(iov.iov_base + sizeof(uint64_t), iov.iov_len - sizeof(uint64_t));

	if (*result == NULL)
		return (-1);

	return (0);
}
