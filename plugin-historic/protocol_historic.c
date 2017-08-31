/*
 * ws protocol handler plugin for "monitoring"
 *
 * Copyright (C) 2010-2016 Andy Green <andy@warmcat.com>
 *
 * This file is made available under the Creative Commons CC0 1.0
 * Universal Public Domain Dedication.
 *
 * The person who associated a work with this deed has dedicated
 * the work to the public domain by waiving all of his or her rights
 * to the work worldwide under copyright law, including all related
 * and neighboring rights, to the extent allowed by law. You can copy,
 * modify, distribute and perform the work, even for commercial purposes,
 * all without asking permission.
 *
 * These test plugins are intended to be adapted for use in your code, which
 * may be proprietary.  So unlike the library itself, they are licensed
 * Public Domain.
 *
 * This is a copy of dumb_increment adapted slightly to serve as the
 * "example-standalone-protocol", to show how to build protocol plugins
 * outside the library easily.
 */

#define LWS_DLL
#define LWS_INTERNAL
#include <libwebsockets.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/un.h> 

#include <string.h>

struct per_vhost_data__historic {
	struct lws_context *context;
	struct lws_vhost *vhost;
	const struct lws_protocols *protocol;
};

struct per_session_data__historic {
	int fdSocket;
	bool continuation;
	bool final;
};

/////////////////////////////////////////////////

#define SOCKET_PATH "/run/sensorsd/socket"

// Open the socket
static void pss_init(struct per_session_data__historic *pss) {
	struct sockaddr_un addr = {.sun_family = AF_UNIX,	 .sun_path=SOCKET_PATH };
	pss->fdSocket = socket(AF_UNIX, SOCK_STREAM, 0);
	if(pss->fdSocket == -1) {
		perror("socket error");
		pss->fdSocket = 0;
	}
	else if (connect(pss->fdSocket, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		perror("connect error");
		pss->fdSocket = 0;
	}
	pss->continuation = false;
	pss->final = false;
}

/////////////////////////////////////////////////

static int
callback_historic(struct lws *wsi, enum lws_callback_reasons reason,
			void *user, void *in, size_t len)
{
	struct per_session_data__historic *pss =
			(struct per_session_data__historic *)user;
	struct per_vhost_data__historic *vhd =
			(struct per_vhost_data__historic *)
			lws_protocol_vh_priv_get(lws_get_vhost(wsi),
					lws_get_protocol(wsi));
	unsigned char buf[LWS_PRE + 512];
	unsigned char *p = &buf[LWS_PRE];
	int n, m, write_mode;

	switch (reason) {
	case LWS_CALLBACK_PROTOCOL_INIT:
		vhd = lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi),
				lws_get_protocol(wsi),
				sizeof(struct per_vhost_data__historic));
		vhd->context = lws_get_context(wsi);
		vhd->protocol = lws_get_protocol(wsi);
		vhd->vhost = lws_get_vhost(wsi);
		printf("+++ historic-protocol: Init\n");
		break;

	case LWS_CALLBACK_PROTOCOL_DESTROY:
		if (!vhd)
			break;
		lwsl_info("+++ historic-protocol: Destroy\n");
		break;

	case LWS_CALLBACK_ESTABLISHED:
		pss_init(pss);
		lws_callback_on_writable(wsi);
		printf("+++ historic-protocol: Established\n");
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:
//		printf("+++ historic-protocol: Server Writable BEGIN\n");
		if(!pss->final) {
			n = read(pss->fdSocket, (char *)p, 512);
			
			write_mode = LWS_WRITE_CONTINUATION;
			if (!pss->continuation) {
				// only text supported in my code
				write_mode = LWS_WRITE_TEXT;
				pss->continuation = true;
			}
			if(n <= 0) {
				pss->final = true;
				n = 0;
			}	
			if (!pss->final)
				write_mode |= LWS_WRITE_NO_FIN;
			printf("+++ historic-protocol: writing %d, with final %d\n", n, pss->final);	
			m = lws_write(wsi, p, n, write_mode);
			if (m < n) {
				lwsl_err("ERROR %d writing to di socket\n", n);
				return -1;
			}
			lws_callback_on_writable(wsi);
		}
		else {
			printf("+++ historic-protocol: Close Connection\n");
			lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
		}
//		printf("+++ historic-protocol: Server Writable END\n");
		break;

	case LWS_CALLBACK_RECEIVE:
		printf("+++ historic-protocol: Receive\n");
		break;

	default:
		printf("+++ historic-protocol: DEFAULT\n");
		break;
	}

	return 0;
}

static const struct lws_protocols protocols[] = {
	{
		"historic-protocol",
		callback_historic,
		sizeof(struct per_session_data__historic),
		20*1024*1024, /* rx buf size must be >= permessage-deflate rx size */
	},
};

LWS_VISIBLE int
init_protocol_historic(struct lws_context *context,
			     struct lws_plugin_capability *c)
{
	if (c->api_magic != LWS_PLUGIN_API_MAGIC) {
		lwsl_err("Plugin API %d, library API %d", LWS_PLUGIN_API_MAGIC,
			 c->api_magic);
		return 1;
	}

	c->protocols = protocols;
	c->count_protocols = ARRAY_SIZE(protocols);
	c->extensions = NULL;
	c->count_extensions = 0;

	return 0;
}

LWS_VISIBLE int
destroy_protocol_historic(struct lws_context *context)
{
	return 0;
}
