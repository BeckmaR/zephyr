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

struct mqtt_sn_client {
	struct mqtt_sn_data client_id; /**< 1-23 character unique client ID */
};

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_NET_MQTT_SN_H_ */

/**@}  */
