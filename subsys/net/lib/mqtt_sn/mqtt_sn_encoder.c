/*
 * Copyright (c) 2022 René Beckmann
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file mqtt_sn_encoder.c
 *
 * @brief MQTT-SN message encoder.
 */

#include "mqtt_sn_msg.h"
#include <net/mqtt_sn.h>

#include <logging/log.h>
LOG_MODULE_DECLARE(net_mqtt_sn, CONFIG_MQTT_SN_LOG_LEVEL);

static void prepare_message(struct mqtt_sn_msg *msg, enum mqtt_sn_msg_type type)
{
	net_buf_reserve(msg->buf, 3);
	mqtt_sn_msg_add_u8(msg, (uint8_t)type);
}

static int encode_length(struct mqtt_sn_msg *msg)
{
	size_t length = mqtt_sn_msg_size(msg);

	// Size must not be larger than an uint16_t can fit, minus 3 bytes for the length field itself
	if (length > UINT16_MAX - 3) {
		return -EINVAL;
	}

	if (length + 1 <= 255) {
		mqtt_sn_msg_push_u8(msg, (uint8_t)length + 1);
	} else {
		mqtt_sn_msg_push_be16(msg, length + 3);
		mqtt_sn_msg_push_u8(msg, MQTT_SN_LENGTH_FIELD_EXTENDED_PREFIX);
	}

	return 0;
}

static void encode_flags(struct mqtt_sn_msg *msg, struct mqtt_sn_flags *flags)
{
	uint8_t b = 0;

	LOG_DBG("Encode flags %d, %d, %d, %d, %d, %d", flags->dup, flags->retain, flags->will, flags->clean_session, flags->qos, flags->topic_type);

	b |= flags->dup ? MQTT_SN_FLAGS_DUP : 0;
	b |= flags->retain ? MQTT_SN_FLAGS_RETAIN : 0;
	b |= flags->will ? MQTT_SN_FLAGS_WILL : 0;
	b |= flags->clean_session ? MQTT_SN_FLAGS_CLEANSESSION : 0;

	b |= ((flags->qos << MQTT_SN_FLAGS_SHIFT_QOS) & MQTT_SN_FLAGS_MASK_QOS);
	b |= ((flags->topic_type << MQTT_SN_FLAGS_SHIFT_TOPICID_TYPE) &
	      MQTT_SN_FLAGS_MASK_TOPICID_TYPE);

	mqtt_sn_msg_add_u8(msg, b);
}

int mqtt_sn_encode_msg_searchgw(struct mqtt_sn_msg *msg, struct mqtt_sn_param_searchgw *params)
{
	prepare_message(msg, MQTT_SN_MSG_TYPE_SEARCHGW);

	mqtt_sn_msg_add_u8(msg, params->radius);

	return encode_length(msg);
}

int mqtt_sn_encode_msg_gwinfo(struct mqtt_sn_msg *msg, struct mqtt_sn_param_gwinfo *params)
{
	prepare_message(msg, MQTT_SN_MSG_TYPE_GWINFO);

	mqtt_sn_msg_add_u8(msg, params->gw_id);

	mqtt_sn_msg_add_data(msg, &params->gw_add);

	return encode_length(msg);
}

int mqtt_sn_encode_msg_connect(struct mqtt_sn_msg *msg, struct mqtt_sn_param_connect *params)
{
	struct mqtt_sn_flags flags = { .will = params->will,
				       .clean_session = params->clean_session };

	prepare_message(msg, MQTT_SN_MSG_TYPE_CONNECT);

	encode_flags(msg, &flags);

	mqtt_sn_msg_add_u8(msg, MQTT_SN_PROTOCOL_ID);

	mqtt_sn_msg_add_be16(msg, params->duration);

	mqtt_sn_msg_add_data(msg, &params->client_id);

	return encode_length(msg);
}

int mqtt_sn_encode_msg_willtopic(struct mqtt_sn_msg *msg, struct mqtt_sn_param_willtopic *params)
{
	struct mqtt_sn_flags flags = { .qos = params->qos, .retain = params->retain };

	prepare_message(msg, MQTT_SN_MSG_TYPE_WILLTOPIC);

	encode_flags(msg, &flags);

	mqtt_sn_msg_add_data(msg, &params->topic);

	return encode_length(msg);
}

int mqtt_sn_encode_msg_willmsg(struct mqtt_sn_msg *msg, struct mqtt_sn_param_willmsg *params)
{
	prepare_message(msg, MQTT_SN_MSG_TYPE_WILLMSG);

	mqtt_sn_msg_add_data(msg, &params->msg);

	return encode_length(msg);
}

int mqtt_sn_encode_msg_register(struct mqtt_sn_msg *msg, struct mqtt_sn_param_register *params)
{
	prepare_message(msg, MQTT_SN_MSG_TYPE_REGISTER);

	// When sent by the client, the topic ID is always 0x0000
	mqtt_sn_msg_add_be16(msg, 0x00);
	mqtt_sn_msg_add_be16(msg, params->msg_id);
	mqtt_sn_msg_add_data(msg, &params->topic);

	return encode_length(msg);
}

int mqtt_sn_encode_msg_regack(struct mqtt_sn_msg *msg, struct mqtt_sn_param_regack *params)
{
	prepare_message(msg, MQTT_SN_MSG_TYPE_REGACK);

	mqtt_sn_msg_add_be16(msg, params->topic_id);
	mqtt_sn_msg_add_be16(msg, params->msg_id);
	mqtt_sn_msg_add_u8(msg, params->ret_code);

	return encode_length(msg);
}

int mqtt_sn_encode_msg_publish(struct mqtt_sn_msg *msg, struct mqtt_sn_param_publish *params)
{
	struct mqtt_sn_flags flags = { .dup = params->dup,
				       .retain = params->retain,
				       .qos = params->qos,
				       .topic_type = params->topic_type };

	prepare_message(msg, MQTT_SN_MSG_TYPE_PUBLISH);
	encode_flags(msg, &flags);

	mqtt_sn_msg_add_be16(msg, params->topic_id);

	if (params->qos == MQTT_SN_QOS_1 || params->qos == MQTT_SN_QOS_2) {
		mqtt_sn_msg_add_be16(msg, params->msg_id);
	} else {
		// only relevant in case of QoS levels 1 and 2, otherwise coded 0x0000.
		mqtt_sn_msg_add_be16(msg, 0x0000);
	}

	mqtt_sn_msg_add_data(msg, &params->data);

	return encode_length(msg);
}

int mqtt_sn_encode_msg_puback(struct mqtt_sn_msg *msg, struct mqtt_sn_param_puback *params)
{
	prepare_message(msg, MQTT_SN_MSG_TYPE_PUBACK);

	mqtt_sn_msg_add_be16(msg, params->topic_id);
	mqtt_sn_msg_add_be16(msg, params->msg_id);
	mqtt_sn_msg_add_u8(msg, params->ret_code);

	return encode_length(msg);
}

int mqtt_sn_encode_msg_pubrec(struct mqtt_sn_msg *msg, struct mqtt_sn_param_pubrec *params)
{
	prepare_message(msg, MQTT_SN_MSG_TYPE_PUBREC);

	mqtt_sn_msg_add_be16(msg, params->msg_id);

	return encode_length(msg);
}

int mqtt_sn_encode_msg_pubrel(struct mqtt_sn_msg *msg, struct mqtt_sn_param_pubrel *params)
{
	prepare_message(msg, MQTT_SN_MSG_TYPE_PUBREL);

	mqtt_sn_msg_add_be16(msg, params->msg_id);

	return encode_length(msg);
}

int mqtt_sn_encode_msg_pubcomp(struct mqtt_sn_msg *msg, struct mqtt_sn_param_pubcomp *params)
{
	prepare_message(msg, MQTT_SN_MSG_TYPE_PUBCOMP);

	mqtt_sn_msg_add_be16(msg, params->msg_id);

	return encode_length(msg);
}

int mqtt_sn_encode_msg_subscribe(struct mqtt_sn_msg *msg, struct mqtt_sn_param_subscribe *params)
{
	struct mqtt_sn_flags flags = { .dup = params->dup,
				       .qos = params->qos,
				       .topic_type = params->topic_type };

	prepare_message(msg, MQTT_SN_MSG_TYPE_SUBSCRIBE);
	encode_flags(msg, &flags);

	mqtt_sn_msg_add_be16(msg, params->msg_id);

	if (params->topic_type == MQTT_SN_TOPIC_TYPE_NORMAL) {
		mqtt_sn_msg_add_data(msg, &params->topic.topic_name);
	} else {
		mqtt_sn_msg_add_be16(msg, params->topic.topic_id);
	}

	return encode_length(msg);
}

int mqtt_sn_encode_msg_unsubscribe(struct mqtt_sn_msg *msg,
				   struct mqtt_sn_param_unsubscribe *params)
{
	struct mqtt_sn_flags flags = { .topic_type = params->topic_type };

	prepare_message(msg, MQTT_SN_MSG_TYPE_UNSUBSCRIBE);
	encode_flags(msg, &flags);

	mqtt_sn_msg_add_be16(msg, params->msg_id);

	if (params->topic_type == MQTT_SN_TOPIC_TYPE_NORMAL) {
		mqtt_sn_msg_add_data(msg, &params->topic.topic_name);
	} else if (params->topic_type == MQTT_SN_TOPIC_TYPE_SHORT) {
		mqtt_sn_msg_add_le16(msg, params->topic.topic_id);
	} else if (params->topic_type == MQTT_SN_TOPIC_TYPE_PREDEF) {
		mqtt_sn_msg_add_be16(msg, params->topic.topic_id);
	}

	return encode_length(msg);
}

int mqtt_sn_encode_msg_pingreq(struct mqtt_sn_msg *msg, struct mqtt_sn_param_pingreq *params)
{
	prepare_message(msg, MQTT_SN_MSG_TYPE_PINGREQ);

	if (params->client_id.size) {
		mqtt_sn_msg_add_data(msg, &params->client_id);
	}

	return encode_length(msg);
}

int mqtt_sn_encode_msg_pingresp(struct mqtt_sn_msg *msg)
{
	prepare_message(msg, MQTT_SN_MSG_TYPE_PINGRESP);

	return encode_length(msg);
}

int mqtt_sn_encode_msg_disconnect(struct mqtt_sn_msg *msg, struct mqtt_sn_param_disconnect *params)
{
	prepare_message(msg, MQTT_SN_MSG_TYPE_DISCONNECT);

	if (params->duration) {
		mqtt_sn_msg_add_be16(msg, params->duration);
	}

	return encode_length(msg);
}

int mqtt_sn_encode_msg_willtopicupd(struct mqtt_sn_msg *msg, struct mqtt_sn_param_willtopicupd *params)
{
	struct mqtt_sn_flags flags = { .qos = params->qos, .retain = params->retain };

	prepare_message(msg, MQTT_SN_MSG_TYPE_WILLTOPICUPD);

	// If the topic is empty, send an empty message to delete the will topic & message.
	if (params->topic.size) {
		encode_flags(msg, &flags);

		mqtt_sn_msg_add_data(msg, &params->topic);
	}

	return encode_length(msg);
}

int mqtt_sn_encode_msg_willmsgupd(struct mqtt_sn_msg *msg, struct mqtt_sn_param_willmsgupd *params)
{
	prepare_message(msg, MQTT_SN_MSG_TYPE_WILLMSGUPD);

	mqtt_sn_msg_add_data(msg, &params->msg);

	return encode_length(msg);
}
