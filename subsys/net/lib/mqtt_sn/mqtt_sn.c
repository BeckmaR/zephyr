/*
 * Copyright (c) 2022 René Beckmann
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file mqtt_sn.c
 *
 * @brief MQTT-SN Client API Implementation.
 */

#include <net/mqtt_sn.h>

#include <net/net_ip.h>
#include <net/socket.h>
#include <fcntl.h>

#include "mqtt_sn_msg.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(net_mqtt_sn, CONFIG_MQTT_SN_LOG_LEVEL);

K_HEAP_DEFINE(mqtt_sn_msg_heap, CONFIG_MQTT_SN_LIB_MSG_HEAP_SIZE);

struct mqtt_sn_confirmable {
	int64_t last_attempt;
	uint16_t msg_id;
	uint8_t retries;
};

struct mqtt_sn_publish {
	struct mqtt_sn_confirmable con;
	sys_snode_t next;
	struct mqtt_sn_topic *topic;
	struct mqtt_sn_data data;
	enum mqtt_sn_qos qos;
	bool retain;
};

enum __attribute__((packed)) mqtt_sn_topic_state {
	MQTT_SN_TOPIC_STATE_REGISTERING,
	MQTT_SN_TOPIC_STATE_REGISTERED,
	MQTT_SN_TOPIC_STATE_SUBSCRIBING,
	MQTT_SN_TOPIC_STATE_SUBSCRIBED,
	MQTT_SN_TOPIC_STATE_UNSUBSCRIBING,
};

struct mqtt_sn_topic {
	struct mqtt_sn_confirmable con;
	sys_snode_t next;
	struct mqtt_sn_data name;
	uint16_t topic_id;
	enum mqtt_sn_qos qos;
	enum mqtt_sn_topic_type type;
	enum mqtt_sn_topic_state state;
};

enum mqtt_sn_client_state { CLIENT_DISCONNECTED, CLIENT_ACTIVE, CLIENT_ASLEEP, CLIENT_AWAKE };

static void set_state(struct mqtt_sn_client *client, enum mqtt_sn_client_state state)
{
	client->state = state;
}

#define T_RETRY_MSEC (CONFIG_MQTT_SN_LIB_T_RETRY * MSEC_PER_SEC)
#define N_RETRY (CONFIG_MQTT_SN_LIB_N_RETRY)

static uint16_t next_msg_id(void)
{
	static uint16_t msg_id = 0;
	return ++msg_id;
}

static int send_msg(struct mqtt_sn_client *client, struct mqtt_sn_msg *msg)
{
	LOG_HEXDUMP_DBG(msg->buf.data, msg->buf.len, "Send message");

	if (!client->callbacks->msg_send) {
		LOG_ERR("Can't send: no callback");
		return -ENOTSUP;
	}

	return client->callbacks->msg_send(msg, client->gateway);
}

static inline void *mqtt_sn_malloc(size_t sz)
{
	return k_heap_alloc(&mqtt_sn_msg_heap, sz, K_NO_WAIT);
}

static inline void mqtt_sn_free(void *mem)
{
	k_heap_free(&mqtt_sn_msg_heap, mem);
}

void mqtt_sn_msg_data(struct mqtt_sn_msg *msg, struct mqtt_sn_data *mem)
{
	mem->data = msg->buf.data;
	mem->size = msg->buf.len;
}

struct mqtt_sn_msg *mqtt_sn_msg_alloc(size_t sz)
{
	struct mqtt_sn_msg *msg;

	LOG_DBG("Allocating %zu bytes of data", sizeof(*msg) + sz);

	msg = mqtt_sn_malloc(sizeof(*msg) + sz);
	if (!msg) {
		return NULL;
	}

	msg->buf.__buf = (uint8_t *)msg + sizeof(*msg);
	msg->buf.size = sz;
	net_buf_simple_init(&msg->buf, 0);
	msg->refc = 1;

	return msg;
}

struct mqtt_sn_msg *mqtt_sn_msg_from_data(void *data, size_t sz)
{
	struct mqtt_sn_msg *msg;

	msg = mqtt_sn_malloc(sizeof(*msg));
	if (!msg) {
		return NULL;
	}

	net_buf_simple_init_with_data(&msg->buf, data, sz);
	msg->refc = 1;

	return msg;
}

struct mqtt_sn_msg *mqtt_sn_msg_ref(struct mqtt_sn_msg *msg)
{
	msg->refc++;

	return msg;
}

void mqtt_sn_msg_unref(struct mqtt_sn_msg *msg)
{
	if (--msg->refc) {
		return;
	}

	mqtt_sn_free(msg);
}

static void mqtt_sn_con_init(struct mqtt_sn_confirmable *con)
{
	con->last_attempt = 0;
	con->retries = N_RETRY;
	con->msg_id = next_msg_id();
}

static void mqtt_sn_publish_destroy(struct mqtt_sn_client *client, struct mqtt_sn_publish *pub)
{
	sys_slist_find_and_remove(&client->publish, &pub->next);
	mqtt_sn_free(pub);
}

static struct mqtt_sn_publish *mqtt_sn_publish_create(struct mqtt_sn_data *data)
{
	struct mqtt_sn_publish *pub;
	uint8_t *data_cpy;

	pub = mqtt_sn_malloc(sizeof(*pub) + data->size);
	if (!pub) {
		return NULL;
	}

	memset(pub, 0, sizeof(*pub));

	data_cpy = (uint8_t *)pub + sizeof(*pub);
	memcpy(data_cpy, data->data, data->size);
	pub->data.data = data_cpy;
	pub->data.size = data->size;

	mqtt_sn_con_init(&pub->con);

	return pub;
}

static struct mqtt_sn_publish *mqtt_sn_publish_find_msg_id(struct mqtt_sn_client *client,
							   uint16_t msg_id)
{
	struct mqtt_sn_publish *pub;

	SYS_SLIST_FOR_EACH_CONTAINER (&client->publish, pub, next) {
		if (pub->con.msg_id == msg_id) {
			return pub;
		}
	}

	return NULL;
}

static struct mqtt_sn_publish *mqtt_sn_publish_find_topic(struct mqtt_sn_client *client,
							  struct mqtt_sn_topic *topic)
{
	struct mqtt_sn_publish *pub;

	SYS_SLIST_FOR_EACH_CONTAINER (&client->publish, pub, next) {
		if (pub->topic == topic) {
			return pub;
		}
	}

	return NULL;
}

static struct mqtt_sn_topic *mqtt_sn_topic_create(struct mqtt_sn_data *name)
{
	struct mqtt_sn_topic *topic;
	uint8_t *name_cpy;

	topic = mqtt_sn_malloc(sizeof(*topic) + name->size);
	if (!topic) {
		return NULL;
	}

	memset(topic, 0, sizeof(*topic));

	name_cpy = (uint8_t *)topic + sizeof(*topic);
	memcpy(name_cpy, name->data, name->size);
	topic->name.data = name_cpy;
	topic->name.size = name->size;

	mqtt_sn_con_init(&topic->con);

	return topic;
}

static struct mqtt_sn_topic *mqtt_sn_topic_find_name(struct mqtt_sn_client *client,
						     struct mqtt_sn_data *topic_name)
{
	struct mqtt_sn_topic *topic;

	SYS_SLIST_FOR_EACH_CONTAINER (&client->topic, topic, next) {
		if (topic->name.size == topic_name->size &&
		    memcmp(topic->name.data, topic_name->data, topic_name->size) == 0) {
			return topic;
		}
	}

	return NULL;
}

static struct mqtt_sn_topic *mqtt_sn_topic_find_msg_id(struct mqtt_sn_client *client,
						       uint16_t msg_id)
{
	struct mqtt_sn_topic *topic;

	SYS_SLIST_FOR_EACH_CONTAINER (&client->topic, topic, next) {
		if (topic->con.msg_id == msg_id) {
			return topic;
		}
	}

	return NULL;
}

static void mqtt_sn_topic_destroy(struct mqtt_sn_client *client, struct mqtt_sn_topic *topic)
{
	struct mqtt_sn_publish *pub;

	// Destroy all pubs referencing this topic
	while ((pub = mqtt_sn_publish_find_topic(client, topic)) != NULL) {
		LOG_WRN("Destroying publish msg_id %d", pub->con.msg_id);
		mqtt_sn_publish_destroy(client, pub);
	}

	sys_slist_find_and_remove(&client->topic, &topic->next);
	mqtt_sn_free(topic);
}

static void disconnect(struct mqtt_sn_client *client)
{
	set_state(client, CLIENT_DISCONNECTED);
}

static void mqtt_sn_do_subscribe(struct mqtt_sn_client *client, struct mqtt_sn_topic *topic)
{
	struct mqtt_sn_param p = { .type = MQTT_SN_MSG_TYPE_SUBSCRIBE };
	struct mqtt_sn_msg *msg;

	if (!client || !topic) {
		return;
	}

	if (client->state != CLIENT_ACTIVE) {
		LOG_ERR("Cannot subscribe: not connected");
		return;
	}

	p.params.subscribe.msg_id = topic->con.msg_id;
	p.params.subscribe.qos = topic->qos;
	p.params.subscribe.topic_type = topic->type;
	switch (topic->type) {
	case MQTT_SN_TOPIC_TYPE_NORMAL:
		p.params.subscribe.topic.topic_name.data = topic->name.data;
		p.params.subscribe.topic.topic_name.size = topic->name.size;
		break;
	case MQTT_SN_TOPIC_TYPE_PREDEF:
	case MQTT_SN_TOPIC_TYPE_SHORT:
		p.params.subscribe.topic.topic_id = topic->topic_id;
		break;
	default:
		LOG_ERR("Unexpected topic type %d", topic->type);
		return;
	}

	if (topic->con.last_attempt) {
		p.params.subscribe.dup = true;
	}

	msg = mqtt_sn_encode_msg(&p);
	if (!msg) {
		return;
	}

	send_msg(client, msg);
	mqtt_sn_msg_unref(msg);
	return;
}

static void mqtt_sn_do_unsubscribe(struct mqtt_sn_client *client, struct mqtt_sn_topic *topic)
{
	struct mqtt_sn_param p = { .type = MQTT_SN_MSG_TYPE_UNSUBSCRIBE };
	struct mqtt_sn_msg *msg;

	if (!client || !topic) {
		return;
	}

	if (client->state != CLIENT_ACTIVE) {
		LOG_ERR("Cannot unsubscribe: not connected");
		return;
	}

	p.params.unsubscribe.msg_id = topic->con.msg_id;
	p.params.unsubscribe.topic_type = topic->type;
	switch (topic->type) {
	case MQTT_SN_TOPIC_TYPE_NORMAL:
		p.params.unsubscribe.topic.topic_name.data = topic->name.data;
		p.params.unsubscribe.topic.topic_name.size = topic->name.size;
		break;
	case MQTT_SN_TOPIC_TYPE_PREDEF:
	case MQTT_SN_TOPIC_TYPE_SHORT:
		p.params.unsubscribe.topic.topic_id = topic->topic_id;
		break;
	default:
		LOG_ERR("Unexpected topic type %d", topic->type);
		return;
	}

	msg = mqtt_sn_encode_msg(&p);
	if (!msg) {
		return;
	}

	send_msg(client, msg);
	mqtt_sn_msg_unref(msg);
	return;
}

static void mqtt_sn_do_register(struct mqtt_sn_client *client, struct mqtt_sn_topic *topic)
{
	struct mqtt_sn_param p = { .type = MQTT_SN_MSG_TYPE_REGISTER };
	struct mqtt_sn_msg *msg;

	if (!client || !topic) {
		return;
	}

	if (client->state != CLIENT_ACTIVE) {
		LOG_ERR("Cannot register: not connected");
		return;
	}

	p.params.reg.msg_id = topic->con.msg_id;
	switch (topic->type) {
	case MQTT_SN_TOPIC_TYPE_NORMAL:
		p.params.reg.topic.data = topic->name.data;
		p.params.reg.topic.size = topic->name.size;
		break;
	default:
		LOG_ERR("Unexpected topic type %d", topic->type);
		return;
	}

	msg = mqtt_sn_encode_msg(&p);
	if (!msg) {
		return;
	}

	send_msg(client, msg);
	mqtt_sn_msg_unref(msg);
	return;
}

static void mqtt_sn_do_publish(struct mqtt_sn_client *client, struct mqtt_sn_publish *pub)
{
	struct mqtt_sn_param p = { .type = MQTT_SN_MSG_TYPE_PUBLISH };
	struct mqtt_sn_msg *msg;

	if (!client || !pub) {
		return;
	}

	if (client->state != CLIENT_ACTIVE) {
		LOG_ERR("Cannot subscribe: not connected");
		return;
	}

	p.params.publish.data.data = pub->data.data;
	p.params.publish.data.size = pub->data.size;
	p.params.publish.msg_id = pub->con.msg_id;
	p.params.publish.retain = pub->retain;
	p.params.publish.topic_id = pub->topic->topic_id;
	p.params.publish.topic_type = pub->topic->type;
	p.params.publish.qos = pub->qos;

	if (pub->con.last_attempt) {
		p.params.publish.dup = true;
	}

	msg = mqtt_sn_encode_msg(&p);
	if (!msg) {
		return;
	}

	send_msg(client, msg);
	mqtt_sn_msg_unref(msg);

	return;
}

static void process_pubs(struct mqtt_sn_client *client, int64_t *next_cycle)
{
	struct mqtt_sn_publish *pub, *pubs;
	const int64_t now = k_uptime_get();
	int64_t next_attempt;
	
	SYS_SLIST_FOR_EACH_CONTAINER_SAFE (&client->publish, pub, pubs, next) {
		LOG_HEXDUMP_DBG(pub->topic->name.data, pub->topic->name.size,
				"Processing publish for topic");
		LOG_HEXDUMP_DBG(pub->data.data, pub->data.size, "Processing publish data");

		if (pub->con.last_attempt == 0) {
			next_attempt = 0;
		} else {
			next_attempt = pub->con.last_attempt + T_RETRY_MSEC;
		}

		if (next_attempt <= now) {
			switch (pub->topic->state) {
			case MQTT_SN_TOPIC_STATE_REGISTERING:
			case MQTT_SN_TOPIC_STATE_SUBSCRIBING:
			case MQTT_SN_TOPIC_STATE_UNSUBSCRIBING:
				LOG_WRN("Can't publish; topic is not ready");
				break;
			case MQTT_SN_TOPIC_STATE_REGISTERED:
			case MQTT_SN_TOPIC_STATE_SUBSCRIBED:
				if (!pub->con.retries--) {
					disconnect(client);
					return;
				}
				mqtt_sn_do_publish(client, pub);
				if (pub->qos == MQTT_SN_QOS_0 || pub->qos == MQTT_SN_QOS_M1) {
					// We are done, remove this
					mqtt_sn_publish_destroy(client, pub);
					continue;
				} else {
					// Wait for ack
					pub->con.last_attempt = now;
					next_attempt = now + T_RETRY_MSEC;
				}
				break;
			}
		}

		if (next_attempt > now && (*next_cycle == 0 || next_attempt < *next_cycle)) {
			*next_cycle = next_attempt;
		}
	}
}

static void process_topics(struct mqtt_sn_client *client, int64_t *next_cycle)
{
	struct mqtt_sn_topic *topic;
	const int64_t now = k_uptime_get();
	int64_t next_attempt;

	SYS_SLIST_FOR_EACH_CONTAINER (&client->topic, topic, next) {
		LOG_HEXDUMP_DBG(topic->name.data, topic->name.size, "Processing topic");

		if (topic->con.last_attempt == 0) {
			next_attempt = 0;
		} else {
			next_attempt = topic->con.last_attempt + T_RETRY_MSEC;
		}

		if (next_attempt <= now) {
			switch (topic->state) {
			case MQTT_SN_TOPIC_STATE_SUBSCRIBING:
				if (!topic->con.retries--) {
					disconnect(client);
					return;
				}
				mqtt_sn_do_subscribe(client, topic);
				topic->con.last_attempt = now;
				next_attempt = now + T_RETRY_MSEC;
				break;
			case MQTT_SN_TOPIC_STATE_REGISTERING:
				if (!topic->con.retries--) {
					disconnect(client);
					return;
				}
				mqtt_sn_do_register(client, topic);
				topic->con.last_attempt = now;
				next_attempt = now + T_RETRY_MSEC;
				break;
			case MQTT_SN_TOPIC_STATE_UNSUBSCRIBING:
				if (!topic->con.retries--) {
					disconnect(client);
					return;
				}
				mqtt_sn_do_subscribe(client, topic);
				topic->con.last_attempt = now;
				next_attempt = now + T_RETRY_MSEC;
				break;
			case MQTT_SN_TOPIC_STATE_REGISTERED:
			case MQTT_SN_TOPIC_STATE_SUBSCRIBED:
				break;
			}
		}

		if (next_attempt > now && (*next_cycle == 0 || next_attempt < *next_cycle)) {
			*next_cycle = next_attempt;
		}
	}
}

static void timeout_work(struct k_work *wrk)
{
	struct mqtt_sn_client *client;
	struct k_work_delayable *dwork;
	int64_t next_cycle = 0;

	dwork = k_work_delayable_from_work(wrk);
	client = CONTAINER_OF(dwork, struct mqtt_sn_client, wrk);

	LOG_DBG("Executing work of client %p", client);

	process_topics(client, &next_cycle);
	process_pubs(client, &next_cycle);

	if (next_cycle != 0) {
		k_work_schedule(dwork, K_MSEC(next_cycle - k_uptime_get()));
	}
}

int mqtt_sn_client_init(struct mqtt_sn_client *client, const struct mqtt_sn_data *client_id,
			const struct mqtt_sn_callbacks *callbacks)
{
	if (!client || !client_id || !callbacks) {
		return -EINVAL;
	}

	memset(client, 0, sizeof(*client));

	client->client_id.data = client_id->data;
	client->client_id.size = client_id->size;
	client->callbacks = callbacks;

	k_work_init_delayable(&client->wrk, timeout_work);

	return 0;
}

int mqtt_sn_connect(struct mqtt_sn_client *client, const void *gateway, bool will,
		    bool clean_session)
{
	int err;
	struct mqtt_sn_param p = { .type = MQTT_SN_MSG_TYPE_CONNECT };
	struct mqtt_sn_msg *msg;

	if (!client || !gateway) {
		return -EINVAL;
	}

	if (will && (!client->will_msg.data || !client->will_topic.data)) {
		LOG_ERR("will set to true, but no will data in client");
		return -EINVAL;
	}

	client->gateway = gateway;

	p.params.connect.clean_session = clean_session;
	p.params.connect.will = will;
	p.params.connect.duration = CONFIG_MQTT_SN_KEEPALIVE;
	p.params.connect.client_id.data = client->client_id.data;
	p.params.connect.client_id.size = client->client_id.size;

	msg = mqtt_sn_encode_msg(&p);
	if (!msg) {
		return -ENOMEM;
	}

	err = send_msg(client, msg);
	mqtt_sn_msg_unref(msg);

	return err;
}

int mqtt_sn_subscribe(struct mqtt_sn_client *client, enum mqtt_sn_qos qos,
		      struct mqtt_sn_data *topic_name)
{
	struct mqtt_sn_topic *topic;
	int err;

	if (!client || !topic_name) {
		return -EINVAL;
	}

	if (client->state != CLIENT_ACTIVE) {
		LOG_ERR("Cannot subscribe: not connected");
		return -ENOTCONN;
	}

	topic = mqtt_sn_topic_find_name(client, topic_name);
	if (!topic) {
		topic = mqtt_sn_topic_create(topic_name);
		if (!topic) {
			return -ENOMEM;
		}

		topic->qos = qos;
		topic->state = MQTT_SN_TOPIC_STATE_SUBSCRIBING;
		sys_slist_append(&client->topic, &topic->next);
	}

	err = k_work_reschedule(&client->wrk, K_NO_WAIT);
	if (err < 0) {
		return err;
	}

	return 0;
}

int mqtt_sn_unsubscribe(struct mqtt_sn_client *client, enum mqtt_sn_qos qos,
			struct mqtt_sn_data *topic_name)
{
	struct mqtt_sn_topic *topic;
	int err;

	if (!client || !topic_name) {
		return -EINVAL;
	}

	if (client->state != CLIENT_ACTIVE) {
		LOG_ERR("Cannot unsubscribe: not connected");
		return -ENOTCONN;
	}

	topic = mqtt_sn_topic_find_name(client, topic_name);
	if (!topic) {
		//LOG_ERR("Topic not found: %s", log_strdup(topic_name));
		return -ENOENT;
	}
	topic->state = MQTT_SN_TOPIC_STATE_UNSUBSCRIBING;
	mqtt_sn_con_init(&topic->con);

	err = k_work_reschedule(&client->wrk, K_NO_WAIT);
	if (err < 0) {
		return err;
	}

	return 0;
}

int mqtt_sn_publish(struct mqtt_sn_client *client, enum mqtt_sn_qos qos,
		    struct mqtt_sn_data *topic_name, bool retain, struct mqtt_sn_data *data)
{
	struct mqtt_sn_publish *pub;
	struct mqtt_sn_topic *topic;
	int err;

	if (!client || !topic_name) {
		return -EINVAL;
	}

	if (client->state != CLIENT_ACTIVE) {
		LOG_ERR("Cannot publish: not connected");
		return -ENOTCONN;
	}

	topic = mqtt_sn_topic_find_name(client, topic_name);
	if (!topic) {
		topic = mqtt_sn_topic_create(topic_name);
		if (!topic) {
			return -ENOMEM;
		}

		topic->qos = qos;
		topic->state = MQTT_SN_TOPIC_STATE_REGISTERING;
		sys_slist_append(&client->topic, &topic->next);
	}

	pub = mqtt_sn_publish_create(data);
	if (!pub) {
		return -ENOMEM;
	}

	pub->qos = qos;
	pub->retain = retain;
	pub->topic = topic;

	sys_slist_append(&client->publish, &pub->next);

	err = k_work_reschedule(&client->wrk, K_NO_WAIT);
	if (err < 0) {
		return err;
	}

	return 0;
}

static void handle_connack(struct mqtt_sn_client *client, struct mqtt_sn_param_connack *p)
{
	if (p->ret_code == MQTT_SN_CODE_ACCEPTED) {
		LOG_INF("MQTT_SN client connected");
		switch (client->state) {
		case CLIENT_DISCONNECTED:
		case CLIENT_ASLEEP:
		case CLIENT_AWAKE:
			set_state(client, CLIENT_ACTIVE);
			break;
		default:
			LOG_ERR("Client received CONNACK but was in state %d", client->state);
			return;
		}
	} else {
		LOG_WRN("CONNACK ret code %d", p->ret_code);
		disconnect(client);
	}
}

static void handle_willtopicreq(struct mqtt_sn_client *client)
{
	struct mqtt_sn_msg *msg;
	struct mqtt_sn_param response = { .type = MQTT_SN_MSG_TYPE_WILLTOPIC };

	response.params.willtopic.qos = client->will_qos;
	response.params.willtopic.retain = client->will_retain;
	response.params.willtopic.topic.data = client->will_topic.data;
	response.params.willtopic.topic.size = client->will_topic.size;

	msg = mqtt_sn_encode_msg(&response);
	if (!msg) {
		return;
	}

	send_msg(client, msg);
	mqtt_sn_msg_unref(msg);
}

static void handle_willmsgreq(struct mqtt_sn_client *client)
{
	struct mqtt_sn_msg *msg;
	struct mqtt_sn_param response = { .type = MQTT_SN_MSG_TYPE_WILLMSG };

	response.params.willmsg.msg.data = client->will_msg.data;
	response.params.willmsg.msg.size = client->will_msg.size;

	msg = mqtt_sn_encode_msg(&response);
	if (!msg) {
		return;
	}

	send_msg(client, msg);
	mqtt_sn_msg_unref(msg);
}

static void handle_gwinfo(struct mqtt_sn_client *client, struct mqtt_sn_param_gwinfo *p)
{
	if (client->callbacks->gwinfo) {
		client->callbacks->gwinfo(p->gw_id, &p->gw_add);
	}
}

static void handle_register(struct mqtt_sn_client *client, struct mqtt_sn_param_register *p)
{
	struct mqtt_sn_msg *msg;
	struct mqtt_sn_param response = { .type = MQTT_SN_MSG_TYPE_REGACK };
	struct mqtt_sn_topic *topic;
	int err;

	topic = mqtt_sn_topic_create(&p->topic);
	if (!topic) {
		return;
	}

	topic->state = MQTT_SN_TOPIC_STATE_REGISTERED;
	topic->topic_id = p->topic_id;
	topic->type = MQTT_SN_TOPIC_TYPE_NORMAL;

	response.params.regack.ret_code = MQTT_SN_CODE_ACCEPTED;
	response.params.regack.topic_id = p->topic_id;
	response.params.regack.msg_id = p->msg_id;

	msg = mqtt_sn_encode_msg(&response);
	if (!msg) {
		return;
	}

	send_msg(client, msg);
	mqtt_sn_msg_unref(msg);
}

static void handle_regack(struct mqtt_sn_client *client, struct mqtt_sn_param_regack *p)
{
	int err;
	struct mqtt_sn_topic *topic = mqtt_sn_topic_find_msg_id(client, p->msg_id);

	if (!topic) {
		LOG_ERR("Can't REGACK, no topic found");
		return;
	}

	if (p->ret_code == MQTT_SN_CODE_ACCEPTED) {
		topic->state = MQTT_SN_TOPIC_STATE_REGISTERED;
	} else {
		LOG_WRN("Gateway could not register topic ID %u, code %d", p->topic_id,
			p->ret_code);
	}
}

static void handle_publish(struct mqtt_sn_client *client, struct mqtt_sn_param_publish *p)
{
	struct mqtt_sn_msg *msg;
	struct mqtt_sn_param response;

	if (p->qos == MQTT_SN_QOS_1) {
		response.type = MQTT_SN_MSG_TYPE_PUBACK;
		response.params.puback.topic_id = p->topic_id;
		response.params.puback.msg_id = p->msg_id;
		response.params.puback.ret_code = MQTT_SN_CODE_ACCEPTED;

		msg = mqtt_sn_encode_msg(&response);
		if (!msg) {
			return;
		}

		send_msg(client, msg);
		mqtt_sn_msg_unref(msg);
	} else if (p->qos == MQTT_SN_QOS_2) {
		response.type = MQTT_SN_MSG_TYPE_PUBREC;
		response.params.pubrec.msg_id = p->msg_id;

		msg = mqtt_sn_encode_msg(&response);
		if (!msg) {
			return;
		}

		send_msg(client, msg);
		mqtt_sn_msg_unref(msg);
	}

	if (client->callbacks->publish) {
		client->callbacks->publish(&p->data, p->topic_type, p->topic_id);
	}
}

static void handle_puback(struct mqtt_sn_client *client, struct mqtt_sn_param_puback *p)
{
	struct mqtt_sn_publish *pub = mqtt_sn_publish_find_msg_id(client, p->msg_id);

	if (!pub) {
		LOG_ERR("No matching PUBLISH found for msg id %u", p->msg_id);
		return;
	}

	mqtt_sn_publish_destroy(client, pub);
}

static void handle_pubrec(struct mqtt_sn_client *client, struct mqtt_sn_param_pubrec *p)
{
	struct mqtt_sn_msg *msg;
	struct mqtt_sn_param response = { .type = MQTT_SN_MSG_TYPE_PUBREL };
	struct mqtt_sn_publish *pub = mqtt_sn_publish_find_msg_id(client, p->msg_id);

	if (!pub) {
		LOG_ERR("No matching PUBLISH found for msg id %u", p->msg_id);
		return;
	}

	pub->con.last_attempt = k_uptime_get();
	pub->con.retries = N_RETRY;

	response.params.pubrel.msg_id = p->msg_id;
	msg = mqtt_sn_encode_msg(&response);
	if (!msg) {
		return;
	}

	send_msg(client, msg);
	mqtt_sn_msg_unref(msg);
}

static void handle_pubrel(struct mqtt_sn_client *client, struct mqtt_sn_param_pubrel *p)
{
	struct mqtt_sn_msg *msg;
	struct mqtt_sn_param response = { .type = MQTT_SN_MSG_TYPE_PUBCOMP };
	int err;

	response.params.pubcomp.msg_id = p->msg_id;
	msg = mqtt_sn_encode_msg(&response);
	if (!msg) {
		return;
	}

	send_msg(client, msg);
	mqtt_sn_msg_unref(msg);
}

static void handle_pubcomp(struct mqtt_sn_client *client, struct mqtt_sn_param_pubcomp *p)
{
	struct mqtt_sn_publish *pub = mqtt_sn_publish_find_msg_id(client, p->msg_id);

	if (!pub) {
		LOG_ERR("No matching PUBLISH found for msg id %u", p->msg_id);
		return;
	}

	mqtt_sn_publish_destroy(client, pub);
}

static void handle_suback(struct mqtt_sn_client *client, struct mqtt_sn_param_suback *p)
{
	struct mqtt_sn_topic *topic = mqtt_sn_topic_find_msg_id(client, p->msg_id);

	if (!topic) {
		LOG_ERR("No matching SUBSCRIBE found for msg id %u", p->msg_id);
		return;
	}

	if (p->ret_code == MQTT_SN_CODE_ACCEPTED) {
		topic->state = MQTT_SN_TOPIC_STATE_SUBSCRIBED;
		topic->topic_id = p->topic_id;
		topic->qos = p->qos;
	} else {
		LOG_WRN("SUBACK with ret code %d", p->ret_code);
	}
}

static void handle_unsuback(struct mqtt_sn_client *client, struct mqtt_sn_param_unsuback *p)
{
	struct mqtt_sn_topic *topic = mqtt_sn_topic_find_msg_id(client, p->msg_id);

	if (!topic || topic->state != MQTT_SN_TOPIC_STATE_UNSUBSCRIBING) {
		LOG_ERR("No matching UNSUBSCRIBE found for msg id %u", p->msg_id);
		return;
	}

	mqtt_sn_topic_destroy(client, topic);
}

static void handle_pingreq(struct mqtt_sn_client *client)
{
	struct mqtt_sn_msg *msg;
	struct mqtt_sn_param response = { .type = MQTT_SN_MSG_TYPE_PINGRESP };

	msg = mqtt_sn_encode_msg(&response);
	if (!msg) {
		return;
	}

	send_msg(client, msg);
	mqtt_sn_msg_unref(msg);
}

static void handle_pingresp(struct mqtt_sn_client *client)
{
}

static void handle_disconnect(struct mqtt_sn_client *client, struct mqtt_sn_param_disconnect *p)
{
	LOG_INF("Received DISCONNECT");
	disconnect(client);
}

static void handle_willtopicresp(struct mqtt_sn_client *client,
				 struct mqtt_sn_param_willtopicresp *p)
{
}

static void handle_willmsgresp(struct mqtt_sn_client *client, struct mqtt_sn_param_willmsgresp *p)
{
}

void mqtt_sn_recv(struct mqtt_sn_client *client, void *data, size_t sz)
{
	int err;
	struct mqtt_sn_msg *msg;
	struct mqtt_sn_param p;

	if (!client) {
		return;
	}

	msg = mqtt_sn_msg_from_data(data, sz);
	if (!msg) {
		LOG_ERR("Could not allocate message");
		return;
	}

	err = mqtt_sn_decode_msg(msg, &p);
	if (err) {
		goto end;
	}

	switch (p.type) {
	case MQTT_SN_MSG_TYPE_GWINFO:
		handle_gwinfo(client, &p.params.gwinfo);
		break;
	case MQTT_SN_MSG_TYPE_CONNACK:
		handle_connack(client, &p.params.connack);
		break;
	case MQTT_SN_MSG_TYPE_WILLTOPICREQ:
		handle_willtopicreq(client);
		break;
	case MQTT_SN_MSG_TYPE_WILLMSGREQ:
		handle_willmsgreq(client);
		break;
	case MQTT_SN_MSG_TYPE_REGISTER:
		handle_register(client, &p.params.reg);
		break;
	case MQTT_SN_MSG_TYPE_REGACK:
		handle_regack(client, &p.params.regack);
		break;
	case MQTT_SN_MSG_TYPE_PUBLISH:
		handle_publish(client, &p.params.publish);
		break;
	case MQTT_SN_MSG_TYPE_PUBACK:
		handle_puback(client, &p.params.puback);
		break;
	case MQTT_SN_MSG_TYPE_PUBREC:
		handle_pubrec(client, &p.params.pubrec);
		break;
	case MQTT_SN_MSG_TYPE_PUBREL:
		handle_pubrel(client, &p.params.pubrel);
		break;
	case MQTT_SN_MSG_TYPE_PUBCOMP:
		handle_pubcomp(client, &p.params.pubcomp);
		break;
	case MQTT_SN_MSG_TYPE_SUBACK:
		handle_suback(client, &p.params.suback);
		break;
	case MQTT_SN_MSG_TYPE_UNSUBACK:
		handle_unsuback(client, &p.params.unsuback);
		break;
	case MQTT_SN_MSG_TYPE_PINGREQ:
		handle_pingreq(client);
		break;
	case MQTT_SN_MSG_TYPE_PINGRESP:
		handle_pingresp(client);
		break;
	case MQTT_SN_MSG_TYPE_DISCONNECT:
		handle_disconnect(client, &p.params.disconnect);
		break;
	case MQTT_SN_MSG_TYPE_WILLTOPICRESP:
		handle_willtopicresp(client, &p.params.willtopicresp);
		break;
	case MQTT_SN_MSG_TYPE_WILLMSGRESP:
		handle_willmsgresp(client, &p.params.willmsgresp);
		break;
	default:
		LOG_ERR("Unexpected message type %d", p.type);
		break;
	}

	k_work_reschedule(&client->wrk, K_NO_WAIT);
end:
	mqtt_sn_msg_unref(msg);
}
