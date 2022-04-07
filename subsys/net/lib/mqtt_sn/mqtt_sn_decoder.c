/*
 * Copyright (c) 2022 René Beckmann
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file mqtt_sn_decoder.c
 *
 * @brief MQTT-SN message decoder.
 */

#include "mqtt_sn_msg.h"
#include <net/mqtt_sn.h>

#include <logging/log.h>
LOG_MODULE_DECLARE(net_mqtt_sn, CONFIG_MQTT_SN_LOG_LEVEL);

static int decode_payload_length(struct mqtt_sn_msg *msg, size_t *l)
{
	size_t length;
	const size_t buflen = mqtt_sn_msg_size(msg);
	size_t length_field_s = 1;

	// Size must not be larger than an uint16_t can fit, minus 3 bytes for the length field itself
	if (buflen > UINT16_MAX) {
		LOG_ERR("Message too large");
		return -EFBIG;
	}

	length = mqtt_sn_msg_pull_u8(msg);
	if (length == MQTT_SN_LENGTH_FIELD_EXTENDED_PREFIX) {
		length = mqtt_sn_msg_pull_be16(msg);
		length_field_s = 3;
	}

	if (length != buflen) {
		LOG_ERR("Message length %zu != buffer size %zu", length, buflen);
		return -EPROTO;
	}

	if (length <+ length_field_s) {
		LOG_ERR("Message length %zu - contains no data?", length);
		return -ENODATA;
	}

	// subtract the size of the length field to get message length
	*l = length - length_field_s;

	return 0;
}

static void decode_flags(struct mqtt_sn_msg *msg, struct mqtt_sn_flags *flags)
{
	uint8_t b = mqtt_sn_msg_pull_u8(msg);

	flags->dup = (bool)(b & MQTT_SN_FLAGS_DUP);
	flags->retain = (bool)(b & MQTT_SN_FLAGS_RETAIN);
	flags->will = (bool)(b & MQTT_SN_FLAGS_WILL);
	flags->clean_session = (bool)(b & MQTT_SN_FLAGS_CLEANSESSION);

	flags->qos = (enum mqtt_sn_qos)((b & MQTT_SN_FLAGS_MASK_QOS) >> MQTT_SN_FLAGS_SHIFT_QOS);

	flags->topic_type = (enum mqtt_sn_topic_type)((b & MQTT_SN_FLAGS_MASK_TOPICID_TYPE) >>
						      MQTT_SN_FLAGS_SHIFT_TOPICID_TYPE);
}

static int decode_empty_message(struct mqtt_sn_msg *msg)
{
	if (mqtt_sn_msg_size(msg)) {
		LOG_ERR("Message not empty");
		return -EPROTO;
	}

	return 0;
}

static int decode_msg_advertise(struct mqtt_sn_msg *msg, struct mqtt_sn_param_advertise *params)
{
	if (mqtt_sn_msg_size(msg) != 3) {
		return -EPROTO;
	}

	params->gw_id = mqtt_sn_msg_pull_u8(msg);
	params->duration = mqtt_sn_msg_pull_be16(msg);

	return 0;
}

static int decode_msg_gwinfo(struct mqtt_sn_msg *msg, struct mqtt_sn_param_gwinfo *params)
{
	if (mqtt_sn_msg_size(msg) < 1) {
		return -EPROTO;
	}

	params->gw_id = mqtt_sn_msg_pull_u8(msg);

	if (mqtt_sn_msg_size(msg)) {
		mqtt_sn_msg_data(msg, &params->gw_add);
	}

	return 0;
}

static int decode_msg_connack(struct mqtt_sn_msg *msg, struct mqtt_sn_param_connack *params)
{
	if (mqtt_sn_msg_size(msg) != 1) {
		return -EPROTO;
	}

	params->ret_code = mqtt_sn_msg_pull_u8(msg);

	return 0;
}

static int decode_msg_willtopicreq(struct mqtt_sn_msg *msg)
{
	return decode_empty_message(msg);
}

static int decode_msg_willmsgreq(struct mqtt_sn_msg *msg)
{
	return decode_empty_message(msg);
}

static int decode_msg_register(struct mqtt_sn_msg *msg, struct mqtt_sn_param_register *params)
{
	if (mqtt_sn_msg_size(msg) < 5) {
		return -EPROTO;
	}

	params->topic_id = mqtt_sn_msg_pull_be16(msg);
	params->msg_id = mqtt_sn_msg_pull_be16(msg);
	mqtt_sn_msg_data(msg, &params->topic);

	return 0;
}

static int decode_msg_regack(struct mqtt_sn_msg *msg, struct mqtt_sn_param_regack *params)
{
	if (mqtt_sn_msg_size(msg) != 5) {
		return -EPROTO;
	}

	params->topic_id = mqtt_sn_msg_pull_be16(msg);
	params->msg_id = mqtt_sn_msg_pull_be16(msg);
	params->ret_code = mqtt_sn_msg_pull_u8(msg);

	return 0;
}

static int decode_msg_publish(struct mqtt_sn_msg *msg, struct mqtt_sn_param_publish *params)
{
	struct mqtt_sn_flags flags;

	if (mqtt_sn_msg_size(msg) < 6) {
		return -EPROTO;
	}

	decode_flags(msg, &flags);
	params->dup = flags.dup;
	params->qos = flags.qos;
	params->retain = flags.retain;
	params->topic_type = flags.topic_type;
	params->topic_id = mqtt_sn_msg_pull_be16(msg);
	params->msg_id = mqtt_sn_msg_pull_be16(msg);
	mqtt_sn_msg_data(msg, &params->data);

	return 0;
}

static int decode_msg_puback(struct mqtt_sn_msg *msg, struct mqtt_sn_param_puback *params)
{
	if (mqtt_sn_msg_size(msg) != 5) {
		return -EPROTO;
	}

	params->topic_id = mqtt_sn_msg_pull_be16(msg);
	params->msg_id = mqtt_sn_msg_pull_be16(msg);
	params->ret_code = mqtt_sn_msg_pull_u8(msg);

	return 0;
}

static int decode_msg_pubrec(struct mqtt_sn_msg *msg, struct mqtt_sn_param_pubrec *params)
{
	if (mqtt_sn_msg_size(msg) != 2) {
		return -EPROTO;
	}

	params->msg_id = mqtt_sn_msg_pull_be16(msg);

	return 0;
}

static int decode_msg_pubrel(struct mqtt_sn_msg *msg, struct mqtt_sn_param_pubrel *params)
{
	if (mqtt_sn_msg_size(msg) != 2) {
		return -EPROTO;
	}

	params->msg_id = mqtt_sn_msg_pull_be16(msg);

	return 0;
}

static int decode_msg_pubcomp(struct mqtt_sn_msg *msg, struct mqtt_sn_param_pubcomp *params)
{
	if (mqtt_sn_msg_size(msg) != 2) {
		return -EPROTO;
	}

	params->msg_id = mqtt_sn_msg_pull_be16(msg);

	return 0;
}

static int decode_msg_suback(struct mqtt_sn_msg *msg, struct mqtt_sn_param_suback *params)
{
	struct mqtt_sn_flags flags;

	if (mqtt_sn_msg_size(msg) != 6) {
		return -EPROTO;
	}

	decode_flags(msg, &flags);

	params->qos = flags.qos;

	params->topic_id = mqtt_sn_msg_pull_be16(msg);
	params->msg_id = mqtt_sn_msg_pull_be16(msg);
	params->ret_code = mqtt_sn_msg_pull_u8(msg);

	return 0;
}

static int decode_msg_unsuback(struct mqtt_sn_msg *msg, struct mqtt_sn_param_unsuback *params)
{
	if (mqtt_sn_msg_size(msg) != 2) {
		return -EPROTO;
	}

	params->msg_id = mqtt_sn_msg_pull_be16(msg);

	return 0;
}

static int decode_msg_pingreq(struct mqtt_sn_msg *msg)
{
	// The client_id field is only set if the message was sent by a client.
	return decode_empty_message(msg);
}

static int decode_msg_pingresp(struct mqtt_sn_msg *msg)
{
	return decode_empty_message(msg);
}

static int decode_msg_disconnect(struct mqtt_sn_msg *msg, struct mqtt_sn_param_disconnect *params)
{
	// The duration field is only set if the message was sent by a client.
	return decode_empty_message(msg);
}

static int decode_msg_willtopicresp(struct mqtt_sn_msg *msg,
				    struct mqtt_sn_param_willtopicresp *params)
{
	if (mqtt_sn_msg_size(msg) != 1) {
		return -EPROTO;
	}

	params->ret_code = mqtt_sn_msg_pull_u8(msg);

	return 0;
}

static int decode_msg_willmsgresp(struct mqtt_sn_msg *msg, struct mqtt_sn_param_willmsgresp *params)
{
	if (mqtt_sn_msg_size(msg) != 1) {
		return -EPROTO;
	}

	params->ret_code = mqtt_sn_msg_pull_u8(msg);

	return 0;
}

int mqtt_sn_decode_msg(struct mqtt_sn_msg *msg, struct mqtt_sn_param *params)
{
	size_t len;
	int err;

	if (!msg || !params) {
		return -EINVAL;
	}

	err = decode_payload_length(msg, &len);
	if (err) {
		LOG_ERR("Could not decode message: %d", err);
		return err;
	}

	params->type = (enum mqtt_sn_msg_type)mqtt_sn_msg_pull_u8(msg);

	LOG_INF("Decoding message type: %d", params->type);

	switch (params->type) {
	case MQTT_SN_MSG_TYPE_ADVERTISE:
		return decode_msg_advertise(msg, &params->params.advertise);
		break;
	case MQTT_SN_MSG_TYPE_GWINFO:
		return decode_msg_gwinfo(msg, &params->params.gwinfo);
		break;
	case MQTT_SN_MSG_TYPE_CONNACK:
		return decode_msg_connack(msg, &params->params.connack);
		break;
	case MQTT_SN_MSG_TYPE_WILLTOPICREQ:
		return decode_msg_willtopicreq(msg);
		break;
	case MQTT_SN_MSG_TYPE_WILLMSGREQ:
		return decode_msg_willmsgreq(msg);
		break;
	case MQTT_SN_MSG_TYPE_REGISTER:
		return decode_msg_register(msg, &params->params.reg);
		break;
	case MQTT_SN_MSG_TYPE_REGACK:
		return decode_msg_regack(msg, &params->params.regack);
		break;
	case MQTT_SN_MSG_TYPE_PUBLISH:
		return decode_msg_publish(msg, &params->params.publish);
		break;
	case MQTT_SN_MSG_TYPE_PUBACK:
		return decode_msg_puback(msg, &params->params.puback);
		break;
	case MQTT_SN_MSG_TYPE_PUBREC:
		return decode_msg_pubrec(msg, &params->params.pubrec);
		break;
	case MQTT_SN_MSG_TYPE_PUBREL:
		return decode_msg_pubrel(msg, &params->params.pubrel);
		break;
	case MQTT_SN_MSG_TYPE_PUBCOMP:
		return decode_msg_pubcomp(msg, &params->params.pubcomp);
		break;
	case MQTT_SN_MSG_TYPE_SUBACK:
		return decode_msg_suback(msg, &params->params.suback);
		break;
	case MQTT_SN_MSG_TYPE_UNSUBACK:
		return decode_msg_unsuback(msg, &params->params.unsuback);
		break;
	case MQTT_SN_MSG_TYPE_PINGREQ:
		return decode_msg_pingreq(msg);
		break;
	case MQTT_SN_MSG_TYPE_PINGRESP:
		return decode_msg_pingresp(msg);
		break;
	case MQTT_SN_MSG_TYPE_DISCONNECT:
		return decode_msg_disconnect(msg, &params->params.disconnect);
		break;
	case MQTT_SN_MSG_TYPE_WILLTOPICRESP:
		return decode_msg_willtopicresp(msg, &params->params.willtopicresp);
		break;
	case MQTT_SN_MSG_TYPE_WILLMSGRESP:
		return decode_msg_willmsgresp(msg, &params->params.willmsgresp);
		break;
	default:
		LOG_ERR("Got unexpected message type %d", params->type);
		return -EINVAL;
		break;
	}
}
