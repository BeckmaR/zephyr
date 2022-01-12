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

#include <logging/log.h>
LOG_MODULE_REGISTER(net_mqtt_sn, CONFIG_MQTT_SN_LOG_LEVEL);
