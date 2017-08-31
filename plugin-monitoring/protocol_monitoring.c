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

#include <string.h>

struct per_vhost_data__monitoring {
	uv_timer_t timeout_watcher;
	struct lws_context *context;
	struct lws_vhost *vhost;
	const struct lws_protocols *protocol;
};

struct per_session_data__monitoring {
	int number;
};

/////////////////////////////////////////////////

static char* getStr(char * path, char out_buf[], int size) {
	FILE * f = fopen(path, "rt");
	if(f!=NULL) {
		fgets(out_buf, size-1, f);
		out_buf[size-1]='\0';
		fclose(f);
	}
	else
	{
		out_buf[0]='\0';
	}
	return out_buf;
}

#define INTERNAL_BUF_SIZE 50

// Return the length of written bytes
static int retrieveDynamicData(char* outputStr, int length) {
	char buf[4][INTERNAL_BUF_SIZE];
	return snprintf(outputStr, length,
		"{\"timestamp\": %ld,\n"
		"\"pressure\"  : %.2f,\n"
		"\"bmp280Temp\": %.3f,\n"
		"\"htu21Temp\" : %.3f,\n"
		"\"humidity\"  : %.2f}\n", 
		time(NULL),
		atof(getStr("/sys/bus/i2c/devices/i2c-1/1-0076/iio:device1/in_pressure_input"        , buf[0], INTERNAL_BUF_SIZE)) * 10,
		atof(getStr("/sys/bus/i2c/devices/i2c-1/1-0076/iio:device1/in_temp_input"            , buf[1], INTERNAL_BUF_SIZE))/1000,
		atof(getStr("/sys/bus/i2c/devices/i2c-1/1-0040/iio:device0/in_temp_input"            , buf[2], INTERNAL_BUF_SIZE))/1000,
		atof(getStr("/sys/bus/i2c/devices/i2c-1/1-0040/iio:device0/in_humidityrelative_input", buf[3], INTERNAL_BUF_SIZE))/1000
	);
}

/////////////////////////////////////////////////

static void
uv_timeout_cb_monitoring(uv_timer_t *w
#if UV_VERSION_MAJOR == 0
		, int status
#endif
)
{
//	printf("+++ monitoring-protocol : Callback mark 1\n");
	struct per_vhost_data__monitoring *vhd = lws_container_of(w,
			struct per_vhost_data__monitoring, timeout_watcher);
//	printf("+++ monitoring-protocol : Callback mark 2\n");
	lws_callback_on_writable_all_protocol_vhost(vhd->vhost, vhd->protocol);
//	printf("+++ monitoring-protocol : Callback mark 3\n");
}

static int
callback_monitoring(struct lws *wsi, enum lws_callback_reasons reason,
			void *user, void *in, size_t len)
{
	struct per_session_data__monitoring *pss =
			(struct per_session_data__monitoring *)user;
	struct per_vhost_data__monitoring *vhd =
			(struct per_vhost_data__monitoring *)
			lws_protocol_vh_priv_get(lws_get_vhost(wsi),
					lws_get_protocol(wsi));
	unsigned char buf[LWS_PRE + 512];
	unsigned char *p = &buf[LWS_PRE];
	int n, m;

	switch (reason) {
	case LWS_CALLBACK_PROTOCOL_INIT:
		vhd = lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi),
				lws_get_protocol(wsi),
				sizeof(struct per_vhost_data__monitoring));
		vhd->context = lws_get_context(wsi);
		vhd->protocol = lws_get_protocol(wsi);
		vhd->vhost = lws_get_vhost(wsi);
		uv_timer_init(lws_uv_getloop(vhd->context, 0),
			      &vhd->timeout_watcher);
		uv_timer_start(&vhd->timeout_watcher,
			       uv_timeout_cb_monitoring, 1* 1000, 1 * 1000);
		printf("+++ monitoring-protocol : LWS_CALLBACK_PROTOCOL INIT\n");
		break;

	case LWS_CALLBACK_PROTOCOL_DESTROY:
		if (!vhd)
			break;
		uv_timer_stop(&vhd->timeout_watcher);
		break;

	case LWS_CALLBACK_ESTABLISHED:
		pss->number = 0;
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:
//		printf("+++ monitoring-protocol : LWS_CALLBACK_SERVER_WRITEABLE BEGIN\n");
		//n = sprintf((char *)p, "%d", pss->number++);
		//printf("BEFORE retrieveDynamicData()\n");
		n = retrieveDynamicData((char *)p, 512);
		//printf("AFTER retrieveDynamicData()=%s\n", (char *)p);
		m = lws_write(wsi, p, n, LWS_WRITE_TEXT);
		if (m < n) {
			lwsl_err("ERROR %d writing to di socket\n", n);
			return -1;
		}
//		printf("+++ monitoring-protocol : LWS_CALLBACK_SERVER_WRITEABLE END\n");
		break;

	case LWS_CALLBACK_RECEIVE:
		printf("+++ monitoring-protocol : [%d] : %s\n", len, (const char *)in);
		break;

	default:
		break;
	}

	return 0;
}

static const struct lws_protocols protocols[] = {
	{
		"monitoring-protocol",
		callback_monitoring,
		sizeof(struct per_session_data__monitoring),
		512, /* rx buf size must be >= permessage-deflate rx size */
	},
};

LWS_VISIBLE int
init_protocol_monitoring(struct lws_context *context,
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
destroy_protocol_monitoring(struct lws_context *context)
{
	return 0;
}
