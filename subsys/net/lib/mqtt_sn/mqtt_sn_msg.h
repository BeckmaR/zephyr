/*
 * Copyright (c) 2022 René Beckmann
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file mqtt_sn_msg.h
 *
 * @brief Function and data structures internal to MQTT-SN module.
 */

#ifndef MQTT_SN_MSG_H_
#define MQTT_SN_MSG_H_

#include <net/mqtt_sn.h>
#include <net/buf.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MQTT_SN_LENGTH_FIELD_EXTENDED_PREFIX 0x01
#define MQTT_SN_PROTOCOL_ID 0x01

struct mqtt_sn_flags {
	bool dup;
	enum mqtt_sn_qos qos;
	bool retain;
	bool will;
	bool clean_session;
	enum mqtt_sn_topic_type topic_type;
};

/**@brief MQTT-SN Flags-field bitmasks */
#define MQTT_SN_FLAGS_DUP BIT(7)
#define MQTT_SN_FLAGS_QOS_0 0
#define MQTT_SN_FLAGS_QOS_1 BIT(5)
#define MQTT_SN_FLAGS_QOS_2 BIT(6)
#define MQTT_SN_FLAGS_QOS_M1 BIT(5) | BIT(6)
#define MQTT_SN_FLAGS_MASK_QOS (BIT(5) | BIT(6))
#define MQTT_SN_FLAGS_SHIFT_QOS 5
#define MQTT_SN_FLAGS_RETAIN BIT(4)
#define MQTT_SN_FLAGS_WILL BIT(3)
#define MQTT_SN_FLAGS_CLEANSESSION BIT(2)
#define MQTT_SN_FLAGS_TOPICID_TYPE_NORMAL 0
#define MQTT_SN_FLAGS_TOPICID_TYPE_PREDEF BIT(0)
#define MQTT_SN_FLAGS_TOPICID_TYPE_SHORT BIT(1)
#define MQTT_SN_FLAGS_MASK_TOPICID_TYPE (BIT(0) | BIT(1))
#define MQTT_SN_FLAGS_SHIFT_TOPICID_TYPE 0

static inline uint8_t *mqtt_sn_msg_add_u8(struct mqtt_sn_msg *msg, uint8_t val)
{
	return net_buf_add_u8(msg->buf, val);
}

static inline void mqtt_sn_msg_add_be16(struct mqtt_sn_msg *msg, uint16_t val)
{
	return net_buf_add_be16(msg->buf, val);
}

static inline void mqtt_sn_msg_add_le16(struct mqtt_sn_msg *msg, uint16_t val)
{
	return net_buf_add_be16(msg->buf, val);
}

static inline void mqtt_sn_msg_push_u8(struct mqtt_sn_msg *msg, uint8_t val)
{
	return net_buf_push_u8(msg->buf, val);
}

static inline void mqtt_sn_msg_push_be16(struct mqtt_sn_msg *msg, uint16_t val)
{
	return net_buf_push_be16(msg->buf, val);
}

static inline void *mqtt_sn_msg_add_data(struct mqtt_sn_msg *msg, struct mqtt_sn_data *mem)
{
	return net_buf_add_mem(msg->buf, mem->data, mem->size);
}

static inline uint8_t mqtt_sn_msg_remove_u8(struct mqtt_sn_msg *msg)
{
	return net_buf_remove_u8(msg->buf);
}

static inline uint16_t mqtt_sn_msg_remove_be16(struct mqtt_sn_msg *msg)
{
	return net_buf_remove_be16(msg->buf);
}

static inline uint8_t mqtt_sn_msg_pull_u8(struct mqtt_sn_msg *msg)
{
	return net_buf_pull_u8(msg->buf);
}

static inline uint16_t mqtt_sn_msg_pull_be16(struct mqtt_sn_msg *msg)
{
	return net_buf_pull_be16(msg->buf);
}

static inline uint16_t mqtt_sn_msg_pull_le16(struct mqtt_sn_msg *msg)
{
	return net_buf_pull_le16(msg->buf);
}

static inline void mqtt_sn_msg_data(struct mqtt_sn_msg *msg, struct mqtt_sn_data *mem)
{
	mem->data = msg->buf->data;
	mem->size = msg->buf->len;
}

static inline size_t mqtt_sn_msg_size(struct mqtt_sn_msg *msg)
{
	return msg->buf->len;
}

int mqtt_sn_encode_msg_searchgw(struct mqtt_sn_msg *msg, struct mqtt_sn_param_searchgw *params);

int mqtt_sn_encode_msg_gwinfo(struct mqtt_sn_msg *msg, struct mqtt_sn_param_gwinfo *params);

int mqtt_sn_encode_msg_connect(struct mqtt_sn_msg *msg, struct mqtt_sn_param_connect *params);

int mqtt_sn_encode_msg_willtopic(struct mqtt_sn_msg *msg, struct mqtt_sn_param_willtopic *params);

int mqtt_sn_encode_msg_willmsg(struct mqtt_sn_msg *msg, struct mqtt_sn_param_willmsg *params);

int mqtt_sn_encode_msg_register(struct mqtt_sn_msg *msg, struct mqtt_sn_param_register *params);

int mqtt_sn_encode_msg_regack(struct mqtt_sn_msg *msg, struct mqtt_sn_param_regack *params);

int mqtt_sn_encode_msg_publish(struct mqtt_sn_msg *msg, struct mqtt_sn_param_publish *params);

int mqtt_sn_encode_msg_puback(struct mqtt_sn_msg *msg, struct mqtt_sn_param_puback *params);

int mqtt_sn_encode_msg_pubrec(struct mqtt_sn_msg *msg, struct mqtt_sn_param_pubrec *params);

int mqtt_sn_encode_msg_pubrel(struct mqtt_sn_msg *msg, struct mqtt_sn_param_pubrel *params);

int mqtt_sn_encode_msg_pubcomp(struct mqtt_sn_msg *msg, struct mqtt_sn_param_pubcomp *params);

int mqtt_sn_encode_msg_subscribe(struct mqtt_sn_msg *msg, struct mqtt_sn_param_subscribe *params);

int mqtt_sn_encode_msg_unsubscribe(struct mqtt_sn_msg *msg,
				   struct mqtt_sn_param_unsubscribe *params);

int mqtt_sn_encode_msg_pingreq(struct mqtt_sn_msg *msg, struct mqtt_sn_param_pingreq *params);

int mqtt_sn_encode_msg_pingresp(struct mqtt_sn_msg *msg);

int mqtt_sn_encode_msg_disconnect(struct mqtt_sn_msg *msg, struct mqtt_sn_param_disconnect *params);

int mqtt_sn_encode_msg_willtopicupd(struct mqtt_sn_msg *msg,
				    struct mqtt_sn_param_willtopicupd *params);

int mqtt_sn_encode_msg_willmsgupd(struct mqtt_sn_msg *msg, struct mqtt_sn_param_willmsgupd *params);

struct mqtt_sn_decode_param {
	enum mqtt_sn_msg_type type;
	union {
		struct mqtt_sn_param_advertise advertise;
		struct mqtt_sn_param_gwinfo gwinfo;
		struct mqtt_sn_param_connack connack;
		struct mqtt_sn_param_register reg;
		struct mqtt_sn_param_regack regack;
		struct mqtt_sn_param_publish publish;
		struct mqtt_sn_param_puback puback;
		struct mqtt_sn_param_pubrec pubrec;
		struct mqtt_sn_param_pubrel pubrel;
		struct mqtt_sn_param_pubcomp pubcomp;
		struct mqtt_sn_param_suback suback;
		struct mqtt_sn_param_unsuback unsuback;
		struct mqtt_sn_param_disconnect disconnect;
		struct mqtt_sn_param_willtopicresp willtopicresp;
		struct mqtt_sn_param_willmsgresp willmsgresp;
	} params;
};

int mqtt_sn_decode_msg(struct mqtt_sn_msg *msg, struct mqtt_sn_decode_param *params);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_SN_MSG_H_ */
