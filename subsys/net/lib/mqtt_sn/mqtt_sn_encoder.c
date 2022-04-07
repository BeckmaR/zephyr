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

/**
 * @brief Prepare and allocate a message
 * 
 * @param msg Message struct to use
 * @param sz The length of the message's payload without the length field.
 * @param type Message type
 * @return  
 */
static struct mqtt_sn_msg *prepare_message(size_t sz, enum mqtt_sn_msg_type type)
{
	struct mqtt_sn_msg *msg;

	// add size of length field
	sz += (sz > 254 ? 3 : 1);

	LOG_DBG("Preparing message of type %d with size %zu", type, sz);

	// Size must not be larger than an uint16_t can fit
	if (sz > UINT16_MAX) {
		return NULL;
	}

	msg = mqtt_sn_msg_alloc(sz);
	if (!msg) {
		return NULL;
	}

	if (sz <= 255) {
		mqtt_sn_msg_add_u8(msg, (uint8_t)sz);
	} else {
		mqtt_sn_msg_add_u8(msg, MQTT_SN_LENGTH_FIELD_EXTENDED_PREFIX);
		mqtt_sn_msg_add_be16(msg, sz);
	}

	mqtt_sn_msg_add_u8(msg, (uint8_t)type);

	return msg;
}

static void encode_flags(struct mqtt_sn_msg *msg, struct mqtt_sn_flags *flags)
{
	uint8_t b = 0;

	LOG_DBG("Encode flags %d, %d, %d, %d, %d, %d", flags->dup, flags->retain, flags->will,
		flags->clean_session, flags->qos, flags->topic_type);

	b |= flags->dup ? MQTT_SN_FLAGS_DUP : 0;
	b |= flags->retain ? MQTT_SN_FLAGS_RETAIN : 0;
	b |= flags->will ? MQTT_SN_FLAGS_WILL : 0;
	b |= flags->clean_session ? MQTT_SN_FLAGS_CLEANSESSION : 0;

	b |= ((flags->qos << MQTT_SN_FLAGS_SHIFT_QOS) & MQTT_SN_FLAGS_MASK_QOS);
	b |= ((flags->topic_type << MQTT_SN_FLAGS_SHIFT_TOPICID_TYPE) &
	      MQTT_SN_FLAGS_MASK_TOPICID_TYPE);

	mqtt_sn_msg_add_u8(msg, b);
}

static struct mqtt_sn_msg *mqtt_sn_encode_msg_searchgw(struct mqtt_sn_param_searchgw *params)
{
	struct mqtt_sn_msg *msg;
	size_t msgsz = 2;

	msg = prepare_message(msgsz, MQTT_SN_MSG_TYPE_SEARCHGW);
	if (!msg) {
		return NULL;
	}

	mqtt_sn_msg_add_u8(msg, params->radius);

	return msg;
}

static struct mqtt_sn_msg *mqtt_sn_encode_msg_gwinfo(struct mqtt_sn_param_gwinfo *params)
{
	struct mqtt_sn_msg *msg;
	size_t msgsz = 2 + params->gw_add.size;

	msg = prepare_message(msgsz, MQTT_SN_MSG_TYPE_GWINFO);
	if (!msg) {
		return NULL;
	}

	mqtt_sn_msg_add_u8(msg, params->gw_id);

	mqtt_sn_msg_add_data(msg, &params->gw_add);

	return msg;
}

static struct mqtt_sn_msg *mqtt_sn_encode_msg_connect(struct mqtt_sn_param_connect *params)
{
	struct mqtt_sn_msg *msg;
	size_t msgsz = 5 + params->client_id.size;
	struct mqtt_sn_flags flags = { .will = params->will,
				       .clean_session = params->clean_session };

	msg = prepare_message(msgsz, MQTT_SN_MSG_TYPE_CONNECT);
	if (!msg) {
		return NULL;
	}

	encode_flags(msg, &flags);

	mqtt_sn_msg_add_u8(msg, MQTT_SN_PROTOCOL_ID);

	mqtt_sn_msg_add_be16(msg, params->duration);

	mqtt_sn_msg_add_data(msg, &params->client_id);

	return msg;
}

static struct mqtt_sn_msg *mqtt_sn_encode_msg_willtopic(struct mqtt_sn_param_willtopic *params)
{
	struct mqtt_sn_msg *msg;
	size_t msgsz = 2 + params->topic.size;
	struct mqtt_sn_flags flags = { .qos = params->qos, .retain = params->retain };

	msg = prepare_message(msgsz, MQTT_SN_MSG_TYPE_WILLTOPIC);
	if (!msg) {
		return NULL;
	}

	encode_flags(msg, &flags);

	mqtt_sn_msg_add_data(msg, &params->topic);

	return msg;
}

static struct mqtt_sn_msg *mqtt_sn_encode_msg_willmsg(struct mqtt_sn_param_willmsg *params)
{
	struct mqtt_sn_msg *msg;
	size_t msgsz = 1 + params->msg.size;

	msg = prepare_message(msgsz, MQTT_SN_MSG_TYPE_WILLMSG);
	if (!msg) {
		return NULL;
	}

	mqtt_sn_msg_add_data(msg, &params->msg);

	return msg;
}

static struct mqtt_sn_msg *mqtt_sn_encode_msg_register(struct mqtt_sn_param_register *params)
{
	struct mqtt_sn_msg *msg;
	size_t msgsz = 5 + params->topic.size;

	msg = prepare_message(msgsz, MQTT_SN_MSG_TYPE_REGISTER);
	if (!msg) {
		return NULL;
	}

	// When sent by the client, the topic ID is always 0x0000
	mqtt_sn_msg_add_be16(msg, 0x00);
	mqtt_sn_msg_add_be16(msg, params->msg_id);
	mqtt_sn_msg_add_data(msg, &params->topic);

	return msg;
}

static struct mqtt_sn_msg *mqtt_sn_encode_msg_regack(struct mqtt_sn_param_regack *params)
{
	struct mqtt_sn_msg *msg;
	size_t msgsz = 6;

	msg = prepare_message(msgsz, MQTT_SN_MSG_TYPE_REGACK);
	if (!msg) {
		return NULL;
	}

	mqtt_sn_msg_add_be16(msg, params->topic_id);
	mqtt_sn_msg_add_be16(msg, params->msg_id);
	mqtt_sn_msg_add_u8(msg, params->ret_code);

	return msg;
}

static struct mqtt_sn_msg *mqtt_sn_encode_msg_publish(struct mqtt_sn_param_publish *params)
{
	struct mqtt_sn_msg *msg;
	size_t msgsz = 6 + params->data.size;
	struct mqtt_sn_flags flags = { .dup = params->dup,
				       .retain = params->retain,
				       .qos = params->qos,
				       .topic_type = params->topic_type };

	msg = prepare_message(msgsz, MQTT_SN_MSG_TYPE_PUBLISH);
	if (!msg) {
		return NULL;
	}
	encode_flags(msg, &flags);

	mqtt_sn_msg_add_be16(msg, params->topic_id);

	if (params->qos == MQTT_SN_QOS_1 || params->qos == MQTT_SN_QOS_2) {
		mqtt_sn_msg_add_be16(msg, params->msg_id);
	} else {
		// only relevant in case of QoS levels 1 and 2, otherwise coded 0x0000.
		mqtt_sn_msg_add_be16(msg, 0x0000);
	}

	mqtt_sn_msg_add_data(msg, &params->data);

	return msg;
}

static struct mqtt_sn_msg *mqtt_sn_encode_msg_puback(struct mqtt_sn_param_puback *params)
{
	struct mqtt_sn_msg *msg;
	size_t msgsz = 6;

	msg = prepare_message(msgsz, MQTT_SN_MSG_TYPE_PUBACK);
	if (!msg) {
		return NULL;
	}

	mqtt_sn_msg_add_be16(msg, params->topic_id);
	mqtt_sn_msg_add_be16(msg, params->msg_id);
	mqtt_sn_msg_add_u8(msg, params->ret_code);

	return msg;
}

static struct mqtt_sn_msg *mqtt_sn_encode_msg_pubrec(struct mqtt_sn_param_pubrec *params)
{
	struct mqtt_sn_msg *msg;
	size_t msgsz = 3;

	msg = prepare_message(msgsz, MQTT_SN_MSG_TYPE_PUBREC);
	if (!msg) {
		return NULL;
	}

	mqtt_sn_msg_add_be16(msg, params->msg_id);

	return msg;
}

static struct mqtt_sn_msg *mqtt_sn_encode_msg_pubrel(struct mqtt_sn_param_pubrel *params)
{
	struct mqtt_sn_msg *msg;
	size_t msgsz = 3;

	msg = prepare_message(msgsz, MQTT_SN_MSG_TYPE_PUBREL);
	if (!msg) {
		return NULL;
	}

	mqtt_sn_msg_add_be16(msg, params->msg_id);

	return msg;
}

static struct mqtt_sn_msg *mqtt_sn_encode_msg_pubcomp(struct mqtt_sn_param_pubcomp *params)
{
	struct mqtt_sn_msg *msg;
	size_t msgsz = 3;

	msg = prepare_message(msgsz, MQTT_SN_MSG_TYPE_PUBCOMP);
	if (!msg) {
		return NULL;
	}

	mqtt_sn_msg_add_be16(msg, params->msg_id);

	return msg;
}

static struct mqtt_sn_msg *mqtt_sn_encode_msg_subscribe(struct mqtt_sn_param_subscribe *params)
{
	struct mqtt_sn_msg *msg;
	size_t msgsz = 4;

	struct mqtt_sn_flags flags = { .dup = params->dup,
				       .qos = params->qos,
				       .topic_type = params->topic_type };

	if (params->topic_type == MQTT_SN_TOPIC_TYPE_NORMAL) {
		msgsz += params->topic.topic_name.size;
	} else {
		msgsz += 2;
	}

	msg = prepare_message(msgsz, MQTT_SN_MSG_TYPE_SUBSCRIBE);
	if (!msg) {
		return NULL;
	}
	encode_flags(msg, &flags);

	mqtt_sn_msg_add_be16(msg, params->msg_id);

	if (params->topic_type == MQTT_SN_TOPIC_TYPE_NORMAL) {
		mqtt_sn_msg_add_data(msg, &params->topic.topic_name);
	} else {
		mqtt_sn_msg_add_be16(msg, params->topic.topic_id);
	}

	return msg;
}

static struct mqtt_sn_msg *mqtt_sn_encode_msg_unsubscribe(struct mqtt_sn_param_unsubscribe *params)
{
	struct mqtt_sn_msg *msg;
	size_t msgsz = 4;

	struct mqtt_sn_flags flags = { .topic_type = params->topic_type };

	if (params->topic_type == MQTT_SN_TOPIC_TYPE_NORMAL) {
		msgsz += params->topic.topic_name.size;
	} else {
		msgsz += 2;
	}

	msg = prepare_message(msgsz, MQTT_SN_MSG_TYPE_UNSUBSCRIBE);
	if (!msg) {
		return NULL;
	}
	encode_flags(msg, &flags);

	mqtt_sn_msg_add_be16(msg, params->msg_id);

	if (params->topic_type == MQTT_SN_TOPIC_TYPE_NORMAL) {
		mqtt_sn_msg_add_data(msg, &params->topic.topic_name);
	} else {
		mqtt_sn_msg_add_be16(msg, params->topic.topic_id);
	}

	return msg;
}

static struct mqtt_sn_msg *mqtt_sn_encode_msg_pingreq(struct mqtt_sn_param_pingreq *params)
{
	struct mqtt_sn_msg *msg;
	size_t msgsz = 1 + params->client_id.size;

	msg = prepare_message(msgsz, MQTT_SN_MSG_TYPE_PINGREQ);
	if (!msg) {
		return NULL;
	}

	if (params->client_id.size) {
		mqtt_sn_msg_add_data(msg, &params->client_id);
	}

	return msg;
}

static struct mqtt_sn_msg *mqtt_sn_encode_msg_pingresp()
{
	struct mqtt_sn_msg *msg;
	size_t msgsz = 1;

	msg = prepare_message(msgsz, MQTT_SN_MSG_TYPE_PINGRESP);
	if (!msg) {
		return NULL;
	}

	return msg;
}

static struct mqtt_sn_msg *mqtt_sn_encode_msg_disconnect(struct mqtt_sn_param_disconnect *params)
{
	struct mqtt_sn_msg *msg;
	size_t msgsz = params->duration ? 3 : 1;

	msg = prepare_message(msgsz, MQTT_SN_MSG_TYPE_DISCONNECT);
	if (!msg) {
		return NULL;
	}

	if (params->duration) {
		mqtt_sn_msg_add_be16(msg, params->duration);
	}

	return msg;
}

static struct mqtt_sn_msg *mqtt_sn_encode_msg_willtopicupd(struct mqtt_sn_param_willtopicupd *params)
{
	struct mqtt_sn_msg *msg;
	size_t msgsz = 2 + params->topic.size;
	struct mqtt_sn_flags flags = { .qos = params->qos, .retain = params->retain };

	msg = prepare_message(msgsz, MQTT_SN_MSG_TYPE_WILLTOPICUPD);
	if (!msg) {
		return NULL;
	}

	// If the topic is empty, send an empty message to delete the will topic & message.
	if (params->topic.size) {
		encode_flags(msg, &flags);

		mqtt_sn_msg_add_data(msg, &params->topic);
	}

	return msg;
}

static struct mqtt_sn_msg *mqtt_sn_encode_msg_willmsgupd(struct mqtt_sn_param_willmsgupd *params)
{
	struct mqtt_sn_msg *msg;
	size_t msgsz = 1 + params->msg.size;

	msg = prepare_message(msgsz, MQTT_SN_MSG_TYPE_WILLMSGUPD);
	if (!msg) {
		return NULL;
	}

	mqtt_sn_msg_add_data(msg, &params->msg);

	return msg;
}

struct mqtt_sn_msg *mqtt_sn_encode_msg(struct mqtt_sn_param *param)
{
	struct mqtt_sn_msg *msg;

	switch (param->type) {
	case MQTT_SN_MSG_TYPE_SEARCHGW:
		msg = mqtt_sn_encode_msg_searchgw(&param->params.searchgw);
		break;
	case MQTT_SN_MSG_TYPE_GWINFO:
		msg = mqtt_sn_encode_msg_gwinfo(&param->params.gwinfo);
		break;
	case MQTT_SN_MSG_TYPE_CONNECT:
		msg = mqtt_sn_encode_msg_connect(&param->params.connect);
		break;
	case MQTT_SN_MSG_TYPE_WILLTOPIC:
		msg = mqtt_sn_encode_msg_willtopic(&param->params.willtopic);
		break;
	case MQTT_SN_MSG_TYPE_WILLMSG:
		msg = mqtt_sn_encode_msg_willmsg(&param->params.willmsg);
		break;
	case MQTT_SN_MSG_TYPE_REGISTER:
		msg = mqtt_sn_encode_msg_register(&param->params.reg);
		break;
	case MQTT_SN_MSG_TYPE_REGACK:
		msg = mqtt_sn_encode_msg_regack(&param->params.regack);
		break;
	case MQTT_SN_MSG_TYPE_PUBLISH:
		msg = mqtt_sn_encode_msg_publish(&param->params.publish);
		break;
	case MQTT_SN_MSG_TYPE_PUBACK:
		msg = mqtt_sn_encode_msg_puback(&param->params.puback);
		break;
	case MQTT_SN_MSG_TYPE_PUBREC:
		msg = mqtt_sn_encode_msg_pubrec(&param->params.pubrec);
		break;
	case MQTT_SN_MSG_TYPE_PUBREL:
		msg = mqtt_sn_encode_msg_pubrel(&param->params.pubrel);
		break;
	case MQTT_SN_MSG_TYPE_PUBCOMP:
		msg = mqtt_sn_encode_msg_pubcomp(&param->params.pubcomp);
		break;
	case MQTT_SN_MSG_TYPE_SUBSCRIBE:
		msg = mqtt_sn_encode_msg_subscribe(&param->params.subscribe);
		break;
	case MQTT_SN_MSG_TYPE_UNSUBSCRIBE:
		msg = mqtt_sn_encode_msg_unsubscribe(&param->params.unsubscribe);
		break;
	case MQTT_SN_MSG_TYPE_PINGREQ:
		msg = mqtt_sn_encode_msg_pingreq(&param->params.pingreq);
		break;
	case MQTT_SN_MSG_TYPE_PINGRESP:
		msg = mqtt_sn_encode_msg_pingresp();
		break;
	case MQTT_SN_MSG_TYPE_DISCONNECT:
		msg = mqtt_sn_encode_msg_disconnect(&param->params.disconnect);
		break;
	case MQTT_SN_MSG_TYPE_WILLTOPICUPD:
		msg = mqtt_sn_encode_msg_willtopicupd(&param->params.willtopicupd);
		break;
	case MQTT_SN_MSG_TYPE_WILLMSGUPD:
		msg = mqtt_sn_encode_msg_willmsgupd(&param->params.willmsgupd);
		break;
	default:
		LOG_ERR("Unsupported msg type %d", param->type);
		msg = NULL;
		break;
	}

	return msg;
}
