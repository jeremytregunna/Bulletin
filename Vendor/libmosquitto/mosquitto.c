/*
 Copyright (c) 2010-2012 Roger Light <roger@atchoo.org>
 All rights reserved.
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 
 1. Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.
 3. Neither the name of mosquitto nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#ifndef WIN32
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#else
#include <winsock2.h>
#include <windows.h>
typedef int ssize_t;
#endif

#include "mosquitto.h"
#include "mosquitto_internal.h"
#include "logging_mosq.h"
#include "messages_mosq.h"
#include "memory_mosq.h"
#include "mqtt3_protocol.h"
#include "net_mosq.h"
#include "read_handle.h"
#include "send_mosq.h"
#include "util_mosq.h"
#include "will_mosq.h"

#if !defined(WIN32) && defined(__SYMBIAN32__)
#define HAVE_PSELECT
#endif

void _mosquitto_destroy(struct mosquitto *mosq);

int mosquitto_lib_version(int *major, int *minor, int *revision)
{
	if(major) *major = LIBMOSQUITTO_MAJOR;
	if(minor) *minor = LIBMOSQUITTO_MINOR;
	if(revision) *revision = LIBMOSQUITTO_REVISION;
	return LIBMOSQUITTO_VERSION_NUMBER;
}

int mosquitto_lib_init(void)
{
#ifdef WIN32
	srand(GetTickCount());
#else
	struct timeval tv;
    
	gettimeofday(&tv, NULL);
	srand((unsigned int)(tv.tv_sec*1000 + tv.tv_usec/1000));
#endif
    
	_mosquitto_net_init();
    
	return MOSQ_ERR_SUCCESS;
}

int mosquitto_lib_cleanup(void)
{
	_mosquitto_net_cleanup();
    
	return MOSQ_ERR_SUCCESS;
}

struct mosquitto *mosquitto_new(const char *id, bool clean_session, void *userdata)
{
	struct mosquitto *mosq = NULL;
	int rc;
    
	if(clean_session == false && id == NULL){
		errno = EINVAL;
		return NULL;
	}
    
#ifndef WIN32
	signal(SIGPIPE, SIG_IGN);
#endif
    
	mosq = (struct mosquitto *)_mosquitto_calloc(1, sizeof(struct mosquitto));
	if(mosq){
		mosq->sock = INVALID_SOCKET;
#ifdef WITH_THREADING
		mosq->thread_id = pthread_self();
#endif
		rc = mosquitto_reinitialise(mosq, id, clean_session, userdata);
		if(rc){
			mosquitto_destroy(mosq);
			if(rc == MOSQ_ERR_INVAL){
				errno = EINVAL;
			}else if(rc == MOSQ_ERR_NOMEM){
				errno = ENOMEM;
			}
			return NULL;
		}
	}else{
		errno = ENOMEM;
	}
	return mosq;
}

int mosquitto_reinitialise(struct mosquitto *mosq, const char *id, bool clean_session, void *userdata)
{
	int i;
    
	if(!mosq) return MOSQ_ERR_INVAL;
    
	if(clean_session == false && id == NULL){
		return MOSQ_ERR_INVAL;
	}
    
	_mosquitto_destroy(mosq);
	memset(mosq, 0, sizeof(struct mosquitto));
    
	if(userdata){
		mosq->userdata = userdata;
	}else{
		mosq->userdata = mosq;
	}
	mosq->sock = INVALID_SOCKET;
	mosq->keepalive = 60;
	mosq->message_retry = 20;
	mosq->last_retry_check = 0;
	mosq->clean_session = clean_session;
	if(id){
		if(strlen(id) == 0){
			return MOSQ_ERR_INVAL;
		}
		mosq->id = _mosquitto_strdup(id);
	}else{
		mosq->id = (char *)_mosquitto_calloc(24, sizeof(char));
		if(!mosq->id){
			return MOSQ_ERR_NOMEM;
		}
		mosq->id[0] = 'm';
		mosq->id[1] = 'o';
		mosq->id[2] = 's';
		mosq->id[3] = 'q';
		mosq->id[4] = '/';
        
		for(i=5; i<23; i++){
			mosq->id[i] = (rand()%73)+48;
		}
	}
	mosq->in_packet.payload = NULL;
	_mosquitto_packet_cleanup(&mosq->in_packet);
	mosq->out_packet = NULL;
	mosq->current_out_packet = NULL;
	mosq->last_msg_in = time(NULL);
	mosq->last_msg_out = time(NULL);
	mosq->ping_t = 0;
	mosq->last_mid = 0;
	mosq->state = mosq_cs_new;
	mosq->messages = NULL;
	mosq->will = NULL;
	mosq->on_connect = NULL;
	mosq->on_publish = NULL;
	mosq->on_message = NULL;
	mosq->on_subscribe = NULL;
	mosq->on_unsubscribe = NULL;
	mosq->host = NULL;
	mosq->port = 1883;
	mosq->in_callback = false;
	mosq->queue_len = 0;
#ifdef WITH_TLS
	mosq->ssl = NULL;
	mosq->tls_cert_reqs = SSL_VERIFY_PEER;
#endif
#ifdef WITH_THREADING
	pthread_mutex_init(&mosq->callback_mutex, NULL);
	pthread_mutex_init(&mosq->log_callback_mutex, NULL);
	pthread_mutex_init(&mosq->state_mutex, NULL);
	pthread_mutex_init(&mosq->out_packet_mutex, NULL);
	pthread_mutex_init(&mosq->current_out_packet_mutex, NULL);
	pthread_mutex_init(&mosq->msgtime_mutex, NULL);
	mosq->thread_id = pthread_self();
#endif
    
	return MOSQ_ERR_SUCCESS;
}

int mosquitto_will_set(struct mosquitto *mosq, const char *topic, int payloadlen, const void *payload, int qos, bool retain)
{
	if(!mosq) return MOSQ_ERR_INVAL;
	return _mosquitto_will_set(mosq, topic, payloadlen, payload, qos, retain);
}

int mosquitto_will_clear(struct mosquitto *mosq)
{
	if(!mosq) return MOSQ_ERR_INVAL;
	return _mosquitto_will_clear(mosq);
}

int mosquitto_username_pw_set(struct mosquitto *mosq, const char *username, const char *password)
{
	if(!mosq) return MOSQ_ERR_INVAL;
    
	if(username){
		mosq->username = _mosquitto_strdup(username);
		if(!mosq->username) return MOSQ_ERR_NOMEM;
		if(mosq->password){
			_mosquitto_free(mosq->password);
			mosq->password = NULL;
		}
		if(password){
			mosq->password = _mosquitto_strdup(password);
			if(!mosq->password){
				_mosquitto_free(mosq->username);
				mosq->username = NULL;
				return MOSQ_ERR_NOMEM;
			}
		}
	}else{
		if(mosq->username){
			_mosquitto_free(mosq->username);
			mosq->username = NULL;
		}
		if(mosq->password){
			_mosquitto_free(mosq->password);
			mosq->password = NULL;
		}
	}
	return MOSQ_ERR_SUCCESS;
}


void _mosquitto_destroy(struct mosquitto *mosq)
{
	struct _mosquitto_packet *packet;
	if(!mosq) return;
    
#ifdef WITH_THREADING
	if(!pthread_equal(mosq->thread_id, pthread_self())){
		pthread_cancel(mosq->thread_id);
		pthread_join(mosq->thread_id, NULL);
	}
    
	if(mosq->id){
		/* If mosq->id is not NULL then the client has already been initialised
		 * and so the mutexes need destroying. If mosq->id is NULL, the mutexes
		 * haven't been initialised. */
		pthread_mutex_destroy(&mosq->callback_mutex);
		pthread_mutex_destroy(&mosq->log_callback_mutex);
		pthread_mutex_destroy(&mosq->state_mutex);
		pthread_mutex_destroy(&mosq->out_packet_mutex);
		pthread_mutex_destroy(&mosq->current_out_packet_mutex);
		pthread_mutex_destroy(&mosq->msgtime_mutex);
	}
#endif
	if(mosq->sock != INVALID_SOCKET){
		_mosquitto_socket_close(mosq);
	}
	_mosquitto_message_cleanup_all(mosq);
	_mosquitto_will_clear(mosq);
#ifdef WITH_TLS
	if(mosq->ssl){
		SSL_free(mosq->ssl);
	}
	if(mosq->ssl_ctx){
		SSL_CTX_free(mosq->ssl_ctx);
	}
	if(mosq->tls_cafile) _mosquitto_free(mosq->tls_cafile);
	if(mosq->tls_capath) _mosquitto_free(mosq->tls_capath);
	if(mosq->tls_certfile) _mosquitto_free(mosq->tls_certfile);
	if(mosq->tls_keyfile) _mosquitto_free(mosq->tls_keyfile);
	if(mosq->tls_pw_callback) mosq->tls_pw_callback = NULL;
	if(mosq->tls_version) _mosquitto_free(mosq->tls_version);
	if(mosq->tls_ciphers) _mosquitto_free(mosq->tls_ciphers);
	if(mosq->tls_psk) _mosquitto_free(mosq->tls_psk);
	if(mosq->tls_psk_identity) _mosquitto_free(mosq->tls_psk_identity);
#endif
    
	if(mosq->address) _mosquitto_free(mosq->address);
	if(mosq->id) _mosquitto_free(mosq->id);
	if(mosq->username) _mosquitto_free(mosq->username);
	if(mosq->password) _mosquitto_free(mosq->password);
	if(mosq->host) _mosquitto_free(mosq->host);
    
	/* Out packet cleanup */
	if(mosq->out_packet && !mosq->current_out_packet){
		mosq->current_out_packet = mosq->out_packet;
		mosq->out_packet = mosq->out_packet->next;
	}
	while(mosq->current_out_packet){
		packet = mosq->current_out_packet;
		/* Free data and reset values */
		mosq->current_out_packet = mosq->out_packet;
		if(mosq->out_packet){
			mosq->out_packet = mosq->out_packet->next;
		}
        
		_mosquitto_packet_cleanup(packet);
		_mosquitto_free(packet);
	}
    
	_mosquitto_packet_cleanup(&mosq->in_packet);
}

void mosquitto_destroy(struct mosquitto *mosq)
{
	_mosquitto_destroy(mosq);
	_mosquitto_free(mosq);
}

int mosquitto_socket(struct mosquitto *mosq)
{
	if(!mosq) return MOSQ_ERR_INVAL;
	return mosq->sock;
}

int mosquitto_connect(struct mosquitto *mosq, const char *host, int port, int keepalive)
{
	int rc;
	rc = mosquitto_connect_async(mosq, host, port, keepalive);
	if(rc) return rc;
    
	return mosquitto_reconnect(mosq);
}

int mosquitto_connect_async(struct mosquitto *mosq, const char *host, int port, int keepalive)
{
	if(!mosq) return MOSQ_ERR_INVAL;
	if(!host || port <= 0) return MOSQ_ERR_INVAL;
    
	if(mosq->host) _mosquitto_free(mosq->host);
	mosq->host = _mosquitto_strdup(host);
	if(!mosq->host) return MOSQ_ERR_NOMEM;
	mosq->port = port;
    
	mosq->keepalive = keepalive;
	pthread_mutex_lock(&mosq->state_mutex);
	mosq->state = mosq_cs_connect_async;
	pthread_mutex_unlock(&mosq->state_mutex);
    
	return MOSQ_ERR_SUCCESS;
}

int mosquitto_reconnect(struct mosquitto *mosq)
{
	int rc;
	struct _mosquitto_packet *packet;
	if(!mosq) return MOSQ_ERR_INVAL;
	if(!mosq->host || mosq->port <= 0) return MOSQ_ERR_INVAL;
    
	pthread_mutex_lock(&mosq->state_mutex);
	mosq->state = mosq_cs_new;
	pthread_mutex_unlock(&mosq->state_mutex);
    
	pthread_mutex_lock(&mosq->msgtime_mutex);
	mosq->last_msg_in = time(NULL);
	mosq->last_msg_out = time(NULL);
	pthread_mutex_unlock(&mosq->msgtime_mutex);
    
	mosq->ping_t = 0;
    
	_mosquitto_packet_cleanup(&mosq->in_packet);
    
	pthread_mutex_lock(&mosq->current_out_packet_mutex);
	pthread_mutex_lock(&mosq->out_packet_mutex);
    
	if(mosq->out_packet && !mosq->current_out_packet){
		mosq->current_out_packet = mosq->out_packet;
		mosq->out_packet = mosq->out_packet->next;
	}
    
	while(mosq->current_out_packet){
		packet = mosq->current_out_packet;
		/* Free data and reset values */
		mosq->current_out_packet = mosq->out_packet;
		if(mosq->out_packet){
			mosq->out_packet = mosq->out_packet->next;
		}
        
		_mosquitto_packet_cleanup(packet);
		_mosquitto_free(packet);
	}
	pthread_mutex_unlock(&mosq->out_packet_mutex);
	pthread_mutex_unlock(&mosq->current_out_packet_mutex);
    
	_mosquitto_messages_reconnect_reset(mosq);
    
	rc = _mosquitto_socket_connect(mosq, mosq->host, mosq->port);
	if(rc){
		return rc;
	}
    
	return _mosquitto_send_connect(mosq, mosq->keepalive, mosq->clean_session);
}

int mosquitto_disconnect(struct mosquitto *mosq)
{
	if(!mosq) return MOSQ_ERR_INVAL;
	if(mosq->sock == INVALID_SOCKET) return MOSQ_ERR_NO_CONN;
    
	pthread_mutex_lock(&mosq->state_mutex);
	mosq->state = mosq_cs_disconnecting;
	pthread_mutex_unlock(&mosq->state_mutex);
    
	return _mosquitto_send_disconnect(mosq);
}

int mosquitto_publish(struct mosquitto *mosq, int *mid, const char *topic, int payloadlen, const void *payload, int qos, bool retain)
{
	struct mosquitto_message_all *message;
	uint16_t local_mid;
    
	if(!mosq || !topic || qos<0 || qos>2) return MOSQ_ERR_INVAL;
	if(strlen(topic) == 0) return MOSQ_ERR_INVAL;
	if(payloadlen < 0 || payloadlen > MQTT_MAX_PAYLOAD) return MOSQ_ERR_PAYLOAD_SIZE;
    
	if(_mosquitto_topic_wildcard_len_check(topic) != MOSQ_ERR_SUCCESS){
		return MOSQ_ERR_INVAL;
	}
    
	local_mid = _mosquitto_mid_generate(mosq);
	if(mid){
		*mid = local_mid;
	}
    
	if(qos == 0){
		return _mosquitto_send_publish(mosq, local_mid, topic, payloadlen, payload, qos, retain, false);
	}else{
		message = _mosquitto_calloc(1, sizeof(struct mosquitto_message_all));
		if(!message) return MOSQ_ERR_NOMEM;
        
		message->next = NULL;
		message->timestamp = time(NULL);
		message->direction = mosq_md_out;
		if(qos == 1){
			message->state = mosq_ms_wait_puback;
		}else if(qos == 2){
			message->state = mosq_ms_wait_pubrec;
		}
		message->msg.mid = local_mid;
		message->msg.topic = _mosquitto_strdup(topic);
		if(!message->msg.topic){
			_mosquitto_message_cleanup(&message);
			return MOSQ_ERR_NOMEM;
		}
		if(payloadlen){
			message->msg.payloadlen = payloadlen;
			message->msg.payload = _mosquitto_malloc(payloadlen*sizeof(uint8_t));
			if(!message->msg.payload){
				_mosquitto_message_cleanup(&message);
				return MOSQ_ERR_NOMEM;
			}
			memcpy(message->msg.payload, payload, payloadlen*sizeof(uint8_t));
		}else{
			message->msg.payloadlen = 0;
			message->msg.payload = NULL;
		}
		message->msg.qos = qos;
		message->msg.retain = retain;
		message->dup = false;
        
		_mosquitto_message_queue(mosq, message);
		return _mosquitto_send_publish(mosq, message->msg.mid, message->msg.topic, message->msg.payloadlen, message->msg.payload, message->msg.qos, message->msg.retain, message->dup);
	}
}

int mosquitto_subscribe(struct mosquitto *mosq, int *mid, const char *sub, int qos)
{
	if(!mosq) return MOSQ_ERR_INVAL;
	if(mosq->sock == INVALID_SOCKET) return MOSQ_ERR_NO_CONN;
    
	return _mosquitto_send_subscribe(mosq, mid, false, sub, qos);
}

int mosquitto_unsubscribe(struct mosquitto *mosq, int *mid, const char *sub)
{
	if(!mosq) return MOSQ_ERR_INVAL;
	if(mosq->sock == INVALID_SOCKET) return MOSQ_ERR_NO_CONN;
    
	return _mosquitto_send_unsubscribe(mosq, mid, false, sub);
}

int mosquitto_tls_set(struct mosquitto *mosq, const char *cafile, const char *capath, const char *certfile, const char *keyfile, int (*pw_callback)(char *buf, int size, int rwflag, void *userdata))
{
#ifdef WITH_TLS
	FILE *fptr;
    
	if(!mosq || (!cafile && !capath) || (certfile && !keyfile) || (!certfile && keyfile)) return MOSQ_ERR_INVAL;
    
	if(cafile){
		fptr = fopen(cafile, "rt");
		if(fptr){
			fclose(fptr);
		}else{
			return MOSQ_ERR_INVAL;
		}
		mosq->tls_cafile = _mosquitto_strdup(cafile);
        
		if(!mosq->tls_cafile){
			return MOSQ_ERR_NOMEM;
		}
	}else if(mosq->tls_cafile){
		_mosquitto_free(mosq->tls_cafile);
		mosq->tls_cafile = NULL;
	}
    
	if(capath){
		mosq->tls_capath = _mosquitto_strdup(capath);
		if(!mosq->tls_capath){
			return MOSQ_ERR_NOMEM;
		}
	}else if(mosq->tls_capath){
		_mosquitto_free(mosq->tls_capath);
		mosq->tls_capath = NULL;
	}
    
	if(certfile){
		fptr = fopen(certfile, "rt");
		if(fptr){
			fclose(fptr);
		}else{
			if(mosq->tls_cafile){
				_mosquitto_free(mosq->tls_cafile);
				mosq->tls_cafile = NULL;
			}
			if(mosq->tls_capath){
				_mosquitto_free(mosq->tls_capath);
				mosq->tls_capath = NULL;
			}
			return MOSQ_ERR_INVAL;
		}
		mosq->tls_certfile = _mosquitto_strdup(certfile);
		if(!mosq->tls_certfile){
			return MOSQ_ERR_NOMEM;
		}
	}else{
		if(mosq->tls_certfile) _mosquitto_free(mosq->tls_certfile);
		mosq->tls_certfile = NULL;
	}
    
	if(keyfile){
		fptr = fopen(keyfile, "rt");
		if(fptr){
			fclose(fptr);
		}else{
			if(mosq->tls_cafile){
				_mosquitto_free(mosq->tls_cafile);
				mosq->tls_cafile = NULL;
			}
			if(mosq->tls_capath){
				_mosquitto_free(mosq->tls_capath);
				mosq->tls_capath = NULL;
			}
			if(mosq->tls_certfile){
				_mosquitto_free(mosq->tls_certfile);
				mosq->tls_capath = NULL;
			}
			return MOSQ_ERR_INVAL;
		}
		mosq->tls_keyfile = _mosquitto_strdup(keyfile);
		if(!mosq->tls_keyfile){
			return MOSQ_ERR_NOMEM;
		}
	}else{
		if(mosq->tls_keyfile) _mosquitto_free(mosq->tls_keyfile);
		mosq->tls_keyfile = NULL;
	}
    
	mosq->tls_pw_callback = pw_callback;
    
    
	return MOSQ_ERR_SUCCESS;
#else
	return MOSQ_ERR_NOT_SUPPORTED;
    
#endif
}

int mosquitto_tls_opts_set(struct mosquitto *mosq, int cert_reqs, const char *tls_version, const char *ciphers)
{
#ifdef WITH_TLS
	if(!mosq) return MOSQ_ERR_INVAL;
    
	mosq->tls_cert_reqs = cert_reqs;
	if(tls_version){
		if(!strcasecmp(tls_version, "tlsv1")){
			mosq->tls_version = _mosquitto_strdup(tls_version);
			if(!mosq->tls_version) return MOSQ_ERR_NOMEM;
		}else{
			return MOSQ_ERR_INVAL;
		}
	}else{
		mosq->tls_version = _mosquitto_strdup("tlsv1");
		if(!mosq->tls_version) return MOSQ_ERR_NOMEM;
	}
	if(ciphers){
		mosq->tls_ciphers = _mosquitto_strdup(ciphers);
		if(!mosq->tls_ciphers) return MOSQ_ERR_NOMEM;
	}else{
		mosq->tls_ciphers = NULL;
	}
    
    
	return MOSQ_ERR_SUCCESS;
#else
	return MOSQ_ERR_NOT_SUPPORTED;
    
#endif
}


int mosquitto_tls_psk_set(struct mosquitto *mosq, const char *psk, const char *identity, const char *ciphers)
{
#if defined(WITH_TLS) && defined(WITH_TLS_PSK)
	if(!mosq || !psk || !identity) return MOSQ_ERR_INVAL;
    
	/* Check for hex only digits */
	if(strspn(psk, "0123456789abcdefABCDEF") < strlen(psk)){
		return MOSQ_ERR_INVAL;
	}
	mosq->tls_psk = _mosquitto_strdup(psk);
	if(!mosq->tls_psk) return MOSQ_ERR_NOMEM;
    
	mosq->tls_psk_identity = _mosquitto_strdup(identity);
	if(!mosq->tls_psk_identity){
		_mosquitto_free(mosq->tls_psk);
		return MOSQ_ERR_NOMEM;
	}
	if(ciphers){
		mosq->tls_ciphers = _mosquitto_strdup(ciphers);
		if(!mosq->tls_ciphers) return MOSQ_ERR_NOMEM;
	}else{
		mosq->tls_ciphers = NULL;
	}
    
	return MOSQ_ERR_SUCCESS;
#else
	return MOSQ_ERR_NOT_SUPPORTED;
#endif
}


int mosquitto_loop(struct mosquitto *mosq, int timeout, int max_packets)
{
#ifdef HAVE_PSELECT
	struct timespec local_timeout;
#else
	struct timeval local_timeout;
#endif
	fd_set readfds, writefds;
	int fdcount;
	int rc;
    
	if(!mosq || max_packets < 1) return MOSQ_ERR_INVAL;
	if(mosq->sock == INVALID_SOCKET) return MOSQ_ERR_NO_CONN;
    
	FD_ZERO(&readfds);
	FD_SET(mosq->sock, &readfds);
	FD_ZERO(&writefds);
	pthread_mutex_lock(&mosq->out_packet_mutex);
	if(mosq->out_packet || mosq->current_out_packet){
		FD_SET(mosq->sock, &writefds);
#ifdef WITH_TLS
	}else if(mosq->ssl && mosq->want_write){
		FD_SET(mosq->sock, &writefds);
#endif
	}
	pthread_mutex_unlock(&mosq->out_packet_mutex);
	if(timeout >= 0){
		local_timeout.tv_sec = timeout/1000;
#ifdef HAVE_PSELECT
		local_timeout.tv_nsec = (timeout-local_timeout.tv_sec*1000)*1e6;
#else
		local_timeout.tv_usec = (int)(timeout-local_timeout.tv_sec*1000)*1000;
#endif
	}else{
		local_timeout.tv_sec = 1;
#ifdef HAVE_PSELECT
		local_timeout.tv_nsec = 0;
#else
		local_timeout.tv_usec = 0;
#endif
	}
    
#ifdef HAVE_PSELECT
	fdcount = pselect(mosq->sock+1, &readfds, &writefds, NULL, &local_timeout, NULL);
#else
	fdcount = select(mosq->sock+1, &readfds, &writefds, NULL, &local_timeout);
#endif
	if(fdcount == -1){
#ifdef WIN32
		errno = WSAGetLastError();
#endif
		if(errno == EINTR){
			return MOSQ_ERR_SUCCESS;
		}else{
			return MOSQ_ERR_ERRNO;
		}
	}else{
		if(FD_ISSET(mosq->sock, &readfds)){
			rc = mosquitto_loop_read(mosq, max_packets);
			if(rc || mosq->sock == INVALID_SOCKET){
				return rc;
			}
		}
		if(FD_ISSET(mosq->sock, &writefds)){
			rc = mosquitto_loop_write(mosq, max_packets);
			if(rc || mosq->sock == INVALID_SOCKET){
				return rc;
			}
		}
	}
	return mosquitto_loop_misc(mosq);
}

int mosquitto_loop_forever(struct mosquitto *mosq, int timeout, int max_packets)
{
	int run = 1;
	int rc;
    
	if(!mosq) return MOSQ_ERR_INVAL;
    
	if(mosq->state == mosq_cs_connect_async){
		mosquitto_reconnect(mosq);
	}
    
	while(run){
		do{
			rc = mosquitto_loop(mosq, timeout, max_packets);
		}while(rc == MOSQ_ERR_SUCCESS);
		if(errno == EPROTO){
			return rc;
		}
		if(mosq->state == mosq_cs_disconnecting){
			run = 0;
		}else{
#ifdef WIN32
			Sleep(1000);
#else
			sleep(1);
#endif
			mosquitto_reconnect(mosq);
		}
	}
	return rc;
}

int mosquitto_loop_misc(struct mosquitto *mosq)
{
	time_t now = time(NULL);
	int rc;
    
	if(!mosq) return MOSQ_ERR_INVAL;
	if(mosq->sock == INVALID_SOCKET) return MOSQ_ERR_NO_CONN;
    
	_mosquitto_check_keepalive(mosq);
	if(mosq->last_retry_check+1 < now){
		_mosquitto_message_retry_check(mosq);
		mosq->last_retry_check = now;
	}
	if(mosq->ping_t && now - mosq->ping_t >= mosq->keepalive){
		/* mosq->ping_t != 0 means we are waiting for a pingresp.
		 * This hasn't happened in the keepalive time so we should disconnect.
		 */
		_mosquitto_socket_close(mosq);
		pthread_mutex_lock(&mosq->state_mutex);
		if(mosq->state == mosq_cs_disconnecting){
			rc = MOSQ_ERR_SUCCESS;
		}else{
			rc = 1;
		}
		pthread_mutex_unlock(&mosq->state_mutex);
		pthread_mutex_lock(&mosq->callback_mutex);
		if(mosq->on_disconnect){
			mosq->in_callback = true;
			mosq->on_disconnect(mosq, mosq->userdata, rc);
			mosq->in_callback = false;
		}
		pthread_mutex_unlock(&mosq->callback_mutex);
		return MOSQ_ERR_CONN_LOST;
	}
	return MOSQ_ERR_SUCCESS;
}

static int _mosquitto_loop_rc_handle(struct mosquitto *mosq, int rc)
{
	if(rc){
		_mosquitto_socket_close(mosq);
		pthread_mutex_lock(&mosq->state_mutex);
		if(mosq->state == mosq_cs_disconnecting){
			rc = MOSQ_ERR_SUCCESS;
		}
		pthread_mutex_unlock(&mosq->state_mutex);
		pthread_mutex_lock(&mosq->callback_mutex);
		if(mosq->on_disconnect){
			mosq->in_callback = true;
			mosq->on_disconnect(mosq, mosq->userdata, rc);
			mosq->in_callback = false;
		}
		pthread_mutex_unlock(&mosq->callback_mutex);
		return rc;
	}
	return rc;
}

int mosquitto_loop_read(struct mosquitto *mosq, int max_packets)
{
	int rc;
	int i;
	if(max_packets < 1) return MOSQ_ERR_INVAL;
    
	max_packets = mosq->queue_len;
	if(max_packets < 1) max_packets = 1;
	/* Queue len here tells us how many messages are awaiting processing and
	 * have QoS > 0. We should try to deal with that many in this loop in order
	 * to keep up. */
	for(i=0; i<max_packets; i++){
		rc = _mosquitto_packet_read(mosq);
		if(rc || errno == EAGAIN || errno == COMPAT_EWOULDBLOCK){
			return _mosquitto_loop_rc_handle(mosq, rc);
		}
	}
	return rc;
}

int mosquitto_loop_write(struct mosquitto *mosq, int max_packets)
{
	int rc;
	int i;
	if(max_packets < 1) return MOSQ_ERR_INVAL;
    
	max_packets = mosq->queue_len;
	if(max_packets < 1) max_packets = 1;
	/* Queue len here tells us how many messages are awaiting processing and
	 * have QoS > 0. We should try to deal with that many in this loop in order
	 * to keep up. */
	for(i=0; i<max_packets; i++){
		rc = _mosquitto_packet_write(mosq);
		if(rc || errno == EAGAIN || errno == COMPAT_EWOULDBLOCK){
			return _mosquitto_loop_rc_handle(mosq, rc);
		}
	}
	return rc;
}

bool mosquitto_want_write(struct mosquitto *mosq)
{
	if(mosq->out_packet){
		return true;
	}else{
		return false;
	}
}

void mosquitto_connect_callback_set(struct mosquitto *mosq, void (*on_connect)(struct mosquitto *, void *, int))
{
	pthread_mutex_lock(&mosq->callback_mutex);
	mosq->on_connect = on_connect;
	pthread_mutex_unlock(&mosq->callback_mutex);
}

void mosquitto_disconnect_callback_set(struct mosquitto *mosq, void (*on_disconnect)(struct mosquitto *, void *, int))
{
	pthread_mutex_lock(&mosq->callback_mutex);
	mosq->on_disconnect = on_disconnect;
	pthread_mutex_unlock(&mosq->callback_mutex);
}

void mosquitto_publish_callback_set(struct mosquitto *mosq, void (*on_publish)(struct mosquitto *, void *, int))
{
	pthread_mutex_lock(&mosq->callback_mutex);
	mosq->on_publish = on_publish;
	pthread_mutex_unlock(&mosq->callback_mutex);
}

void mosquitto_message_callback_set(struct mosquitto *mosq, void (*on_message)(struct mosquitto *, void *, const struct mosquitto_message *))
{
	pthread_mutex_lock(&mosq->callback_mutex);
	mosq->on_message = on_message;
	pthread_mutex_unlock(&mosq->callback_mutex);
}

void mosquitto_subscribe_callback_set(struct mosquitto *mosq, void (*on_subscribe)(struct mosquitto *, void *, int, int, const int *))
{
	pthread_mutex_lock(&mosq->callback_mutex);
	mosq->on_subscribe = on_subscribe;
	pthread_mutex_unlock(&mosq->callback_mutex);
}

void mosquitto_unsubscribe_callback_set(struct mosquitto *mosq, void (*on_unsubscribe)(struct mosquitto *, void *, int))
{
	pthread_mutex_lock(&mosq->callback_mutex);
	mosq->on_unsubscribe = on_unsubscribe;
	pthread_mutex_unlock(&mosq->callback_mutex);
}

void mosquitto_log_callback_set(struct mosquitto *mosq, void (*on_log)(struct mosquitto *, void *, int, const char *))
{
	pthread_mutex_lock(&mosq->log_callback_mutex);
	mosq->on_log = on_log;
	pthread_mutex_unlock(&mosq->log_callback_mutex);
}

void mosquitto_user_data_set(struct mosquitto *mosq, void *userdata)
{
	if(mosq){
		mosq->userdata = userdata;
	}
}

const char *mosquitto_strerror(int mosq_errno)
{
	switch(mosq_errno){
		case MOSQ_ERR_SUCCESS:
			return "No error.";
		case MOSQ_ERR_NOMEM:
			return "Out of memory.";
		case MOSQ_ERR_PROTOCOL:
			return "A network protocol error occurred when communicating with the broker.";
		case MOSQ_ERR_INVAL:
			return "Invalid function arguments provided.";
		case MOSQ_ERR_NO_CONN:
			return "The client is not currently connected.";
		case MOSQ_ERR_CONN_REFUSED:
			return "The connection was refused.";
		case MOSQ_ERR_NOT_FOUND:
			return "Message not found (internal error).";
		case MOSQ_ERR_CONN_LOST:
			return "The connection was lost.";
		case MOSQ_ERR_TLS:
			return "A TLS error occurred.";
		case MOSQ_ERR_PAYLOAD_SIZE:
			return "Payload too large.";
		case MOSQ_ERR_NOT_SUPPORTED:
			return "This feature is not supported.";
		case MOSQ_ERR_AUTH:
			return "Authorisation failed.";
		case MOSQ_ERR_ACL_DENIED:
			return "Access denied by ACL.";
		case MOSQ_ERR_UNKNOWN:
			return "Unknown error.";
		case MOSQ_ERR_ERRNO:
			return "Error defined by errno.";
		default:
			return "Unknown error.";
	}
}

const char *mosquitto_connack_string(int connack_code)
{
	switch(connack_code){
		case 0:
			return "Connection Accepted.";
		case 1:
			return "Connection Refused: unacceptable protocol version.";
		case 2:
			return "Connection Refused: identifier rejected.";
		case 3:
			return "Connection Refused: broker unavailable.";
		case 4:
			return "Connection Refused: bad user name or password.";
		case 5:
			return "Connection Refused: not authorised.";
		default:
			return "Connection Refused: unknown reason.";
	}
}

int mosquitto_sub_topic_tokenise(const char *subtopic, char ***topics, int *count)
{
	size_t len;
	int hier_count = 1;
	int start, stop;
	int hier;
	int tlen;
	int i, j;
    
	if(!subtopic || !topics || !count) return MOSQ_ERR_INVAL;
    
	len = strlen(subtopic);
    
	for(i=0; i<len; i++){
		if(subtopic[i] == '/'){
			while(i<len && subtopic[i] == '/'){
				/* Ignore duplicate separators. */
				i++;
			}
			if(i >= len-1){
				/* Separator at end of line */
			}else{
				hier_count++;
			}
		}
	}
    
	(*topics) = _mosquitto_calloc(hier_count, sizeof(char *));
	if(!(*topics)) return MOSQ_ERR_NOMEM;
    
	start = 0;
	stop = 0;
	hier = 0;
    
	for(i=0; i<len+1; i++){
		if(subtopic[i] == '/' || subtopic[i] == '\0'){
			if(i>0 && subtopic[i] == '/' && subtopic[i-1] == '/'){
				start = i+1;
				continue;
			}
			stop = i;
			if(start != stop){
				tlen = stop-start + 1;
				(*topics)[hier] = _mosquitto_calloc(tlen, sizeof(char));
				if(!(*topics)[hier]){
					for(i=0; i<hier_count; i++){
						if((*topics)[hier]){
							_mosquitto_free((*topics)[hier]);
						}
					}
					_mosquitto_free((*topics));
					return MOSQ_ERR_NOMEM;
				}
				for(j=start; j<stop; j++){
					(*topics)[hier][j-start] = subtopic[j];
				}
			}
			start = i+1;
			hier++;
		}
	}
    
	*count = hier_count;
    
	return MOSQ_ERR_SUCCESS;
}

int mosquitto_sub_topic_tokens_free(char ***topics, int count)
{
	int i;
    
	if(!topics || !(*topics) || count<1) return MOSQ_ERR_INVAL;
    
	for(i=0; i<count; i++){
		if((*topics)[i]) _mosquitto_free((*topics)[i]);
	}
	_mosquitto_free(*topics);
    
	return MOSQ_ERR_SUCCESS;
}

