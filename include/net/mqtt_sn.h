/*
 * Copyright (c) 2022 René Beckmann
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file mqtt_sn.h
 *
 * @defgroup mqtt_sn_socket MQTT Client library
 * @ingroup networking
 * @{
 * @brief MQTT-SN Client Implementation
 *
 * @details
 * MQTT-SN Client's Application interface is defined in this header.
 * Targets protocol version 1.2.
 *
 */

#ifndef ZEPHYR_INCLUDE_NET_MQTT_SN_H_
#define ZEPHYR_INCLUDE_NET_MQTT_SN_H_

#include <stddef.h>

#include <zephyr.h>
#include <zephyr/types.h>
#include <net/buf.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mqtt_sn_client;

struct mqtt_sn_msg {
	struct net_buf *buf;
};

enum mqtt_sn_msg_type {
	MQTT_SN_MSG_TYPE_ADVERTISE = 0x00,
	MQTT_SN_MSG_TYPE_SEARCHGW = 0x01,
	MQTT_SN_MSG_TYPE_GWINFO = 0x02,
	MQTT_SN_MSG_TYPE_CONNECT = 0x04,
	MQTT_SN_MSG_TYPE_CONNACK = 0x05,
	MQTT_SN_MSG_TYPE_WILLTOPICREQ = 0x06,
	MQTT_SN_MSG_TYPE_WILLTOPIC = 0x07,
	MQTT_SN_MSG_TYPE_WILLMSGREQ = 0x08,
	MQTT_SN_MSG_TYPE_WILLMSG = 0x09,
	MQTT_SN_MSG_TYPE_REGISTER = 0x0A,
	MQTT_SN_MSG_TYPE_REGACK = 0x0B,
	MQTT_SN_MSG_TYPE_PUBLISH = 0x0C,
	MQTT_SN_MSG_TYPE_PUBACK = 0x0D,
	MQTT_SN_MSG_TYPE_PUBCOMP = 0x0E,
	MQTT_SN_MSG_TYPE_PUBREC = 0x0F,
	MQTT_SN_MSG_TYPE_PUBREL = 0x10,
	MQTT_SN_MSG_TYPE_SUBSCRIBE = 0x12,
	MQTT_SN_MSG_TYPE_SUBACK = 0x13,
	MQTT_SN_MSG_TYPE_UNSUBSCRIBE = 0x14,
	MQTT_SN_MSG_TYPE_UNSUBACK = 0x15,
	MQTT_SN_MSG_TYPE_PINGREQ = 0x16,
	MQTT_SN_MSG_TYPE_PINGRESP = 0x17,
	MQTT_SN_MSG_TYPE_DISCONNECT = 0x18,
	MQTT_SN_MSG_TYPE_WILLTOPICUPD = 0x1A,
	MQTT_SN_MSG_TYPE_WILLTOPICRESP = 0x1B,
	MQTT_SN_MSG_TYPE_WILLMSGUPD = 0x1C,
	MQTT_SN_MSG_TYPE_WILLMSGRESP = 0x1D,
	MQTT_SN_MSG_TYPE_ENCAPSULATED = 0xFE,
};

enum mqtt_sn_qos {
	MQTT_SN_QOS_0, /**< QOS 0 */
	MQTT_SN_QOS_1, /**< QOS 1 */
	MQTT_SN_QOS_2, /**< QOS 2 */
	MQTT_SN_QOS_M1 /**< QOS -1 */
};

enum mqtt_sn_topic_type {
	MQTT_SN_TOPIC_TYPE_NORMAL,
	MQTT_SN_TOPIC_TYPE_PREDEF,
	MQTT_SN_TOPIC_TYPE_SHORT
};

enum mqtt_sn_return_code {
	MQTT_SN_CODE_ACCEPTED = 0x00, /**< Accepted */
	MQTT_SN_CODE_REJECTED_CONGESTION = 0x01, /**< Rejected: congestion */
	MQTT_SN_CODE_REJECTED_TOPIC_ID = 0x02, /**< Rejected: Invalid Topic ID */
	MQTT_SN_CODE_REJECTED_NOTSUP = 0x03, /**< Rejected: Not Supported */
};

/** @brief Abstracts memory buffers. */
struct mqtt_sn_data {
	const uint8_t *data; /**< Pointer to data. */
	size_t size; /**< Size of data, in bytes. */
};

/**
 * @brief Initialize memory buffer from C literal string.
 *
 * Use it as follows:
 *
 * struct mqtt_sn_data password = MQTT_SN_DATA_STRING_LITERAL("my_pass");
 *
 * @param[in] literal Literal string from which to generate mqtt_sn_data object.
 */
#define MQTT_SN_DATA_STRING_LITERAL(literal) ((struct mqtt_sn_data){ literal, sizeof(literal) - 1 })

/**
 * @brief Initialize memory buffer from single bytes.
 *
 * Use it as follows:
 *
 * struct mqtt_sn_data password = MQTT_SN_DATA_BYTES(0x13, 0x37);
 */
#define MQTT_SN_DATA_BYTES(...) ((struct mqtt_sn_data){ (uint8_t []){ __VA_ARGS__ }, sizeof((uint8_t []){__VA_ARGS__})})


struct mqtt_sn_param_advertise {
	uint8_t gw_id;
	uint16_t duration;
};

struct mqtt_sn_param_searchgw {
	uint8_t radius;
};

struct mqtt_sn_param_gwinfo {
	uint8_t gw_id;
	struct mqtt_sn_data gw_add;
};

struct mqtt_sn_param_connect {
	bool will;
	bool clean_session;
	uint16_t duration;
	struct mqtt_sn_data client_id;
};

struct mqtt_sn_param_connack {
	enum mqtt_sn_return_code ret_code;
};

struct mqtt_sn_param_willtopic {
	enum mqtt_sn_qos qos;
	bool retain;
	struct mqtt_sn_data topic;
};

struct mqtt_sn_param_willmsg {
	struct mqtt_sn_data msg;
};

struct mqtt_sn_param_register {
	uint16_t msg_id;
	uint16_t topic_id;
	struct mqtt_sn_data topic;
};

struct mqtt_sn_param_regack {
	uint16_t msg_id;
	uint16_t topic_id;
	enum mqtt_sn_return_code ret_code;
};

struct mqtt_sn_param_publish {
	bool dup;
	enum mqtt_sn_qos qos;
	bool retain;
	enum mqtt_sn_topic_type topic_type;
	uint16_t topic_id;
	uint16_t msg_id;
	struct mqtt_sn_data data;
};

struct mqtt_sn_param_puback {
	uint16_t msg_id;
	uint16_t topic_id;
	enum mqtt_sn_return_code ret_code;
};

struct mqtt_sn_param_pubrec {
	uint16_t msg_id;
};

struct mqtt_sn_param_pubrel {
	uint16_t msg_id;
};

struct mqtt_sn_param_pubcomp {
	uint16_t msg_id;
};

struct mqtt_sn_param_subscribe {
	bool dup;
	enum mqtt_sn_qos qos;
	enum mqtt_sn_topic_type topic_type;
	uint16_t msg_id;
	union {
		struct mqtt_sn_data topic_name;
		uint16_t topic_id;
	} topic;
};

struct mqtt_sn_param_suback {
	enum mqtt_sn_qos qos;
	uint16_t topic_id;
	uint16_t msg_id;
	enum mqtt_sn_return_code ret_code;
};

struct mqtt_sn_param_unsubscribe {
	enum mqtt_sn_topic_type topic_type;
	uint16_t msg_id;
	union {
		struct mqtt_sn_data topic_name;
		uint16_t topic_id;
	} topic;
};

struct mqtt_sn_param_unsuback {
	uint16_t msg_id;
};

struct mqtt_sn_param_pingreq {
	struct mqtt_sn_data client_id;
};

struct mqtt_sn_param_disconnect {
	uint16_t duration;
};

struct mqtt_sn_param_willtopicupd {
	enum mqtt_sn_qos qos;
	bool retain;
	struct mqtt_sn_data topic;
};

struct mqtt_sn_param_willmsgupd {
	struct mqtt_sn_data msg;
};

struct mqtt_sn_param_willtopicresp {
	enum mqtt_sn_return_code ret_code;
};

struct mqtt_sn_param_willmsgresp {
	enum mqtt_sn_return_code ret_code;
};

struct mqtt_sn_client {
	struct mqtt_sn_data client_id; /**< 1-23 character unique client ID */
};

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_NET_MQTT_SN_H_ */

/**@}  */
