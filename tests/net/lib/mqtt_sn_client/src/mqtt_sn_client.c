/*
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tc_util.h>
#include <mqtt_sn_msg.h>
#include <net/mqtt_sn.h>
#include <sys/util.h> /* for ARRAY_SIZE */
#include <ztest.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(test);


static const struct mqtt_sn_data client_id = MQTT_SN_DATA_STRING_LITERAL("zephyr");

static struct msg_send_data {
	int called;
	size_t msg_sz;
	int ret;
	const void *recipient;
} msg_send_data;

static uint8_t gateway = 42;

static int msg_send(struct mqtt_sn_msg *msg, const void *recipient)
{
	msg_send_data.called++;
	msg_send_data.msg_sz = msg->buf.len;
	msg_send_data.recipient = recipient;

	return msg_send_data.ret;
}

static void assert_msg_send(int called, size_t msg_sz, void *recipient) {
	zassert_equal(msg_send_data.called, called, "msg_send called %d times instead of %d", msg_send_data.called, called);
	zassert_equal(msg_send_data.msg_sz, msg_sz, "msg_sz is %zu instead of %zu", msg_send_data.msg_sz, msg_sz);
	zassert_equal_ptr(msg_send_data.recipient, recipient, "msg recipient is %p, not %p", msg_send_data.recipient, recipient);

	memset(&msg_send_data, 0, sizeof(msg_send_data));
}

static struct mqtt_sn_callbacks callbacks = { .msg_send = msg_send };

static void mqtt_sn_connect_no_will(struct mqtt_sn_client *client)
{
	// connack with return code accepted
	static uint8_t connack[] = {3, 0x05, 0x00};
	int err;

	err = mqtt_sn_client_init(client, &client_id, &callbacks);
	zassert_equal(err, 0, "unexpected error %d");

	err = mqtt_sn_connect(client, (void*)&gateway, false, false);
	zassert_equal(err, 0, "unexpected error %d");
	assert_msg_send(1, 12, &gateway);
	zassert_equal(client->state, 0, "Wrong state");

	mqtt_sn_recv(client, connack, sizeof(connack));
	zassert_equal(client->state, 1, "Wrong state");
	k_sleep(K_MSEC(10));
}

static void test_mqtt_sn_connect_no_will(void)
{
	struct mqtt_sn_client client;
	mqtt_sn_connect_no_will(&client);
}

static void test_mqtt_sn_connect_will(void)
{
	struct mqtt_sn_client client;
	static uint8_t willtopicreq[] = {2, 0x06};
	static uint8_t willmsgreq[] = {2, 0x08};
	static uint8_t connack[] = {3, 0x05, 0x00};

	int err;

	err = mqtt_sn_client_init(&client, &client_id, &callbacks);
	zassert_equal(err, 0, "unexpected error %d");

	client.will_topic = MQTT_SN_DATA_STRING_LITERAL("topic");
	client.will_msg = MQTT_SN_DATA_STRING_LITERAL("msg");

	err = mqtt_sn_connect(&client, (void*)&gateway, true, false);
	zassert_equal(err, 0, "unexpected error %d");
	assert_msg_send(1, 12, &gateway);
	zassert_equal(client.state, 0, "Wrong state");

	mqtt_sn_recv(&client, willtopicreq, sizeof(willtopicreq));
	zassert_equal(client.state, 0, "Wrong state");
	assert_msg_send(1, 8, &gateway);

	mqtt_sn_recv(&client, willmsgreq, sizeof(willmsgreq));
	zassert_equal(client.state, 0, "Wrong state");
	assert_msg_send(1, 5, &gateway);

	mqtt_sn_recv(&client, connack, sizeof(connack));
	zassert_equal(client.state, 1, "Wrong state");
	k_sleep(K_MSEC(10));
}


static void test_mqtt_sn_publish_qos0(void)
{
	struct mqtt_sn_client client;
	struct mqtt_sn_data data = MQTT_SN_DATA_STRING_LITERAL("Hello, World!");
	struct mqtt_sn_data topic = MQTT_SN_DATA_STRING_LITERAL("zephyr");
	// registration ack with topic ID 0x1A1B, msg ID 0x0001, return code accepted
	uint8_t regack[] = { 7, 0x0B, 0x1A, 0x1B, 0x00, 0x01, 0 };
	int err;

	mqtt_sn_connect_no_will(&client);
	err = mqtt_sn_publish(&client, MQTT_SN_QOS_0, &topic, false, &data);
	zassert_equal(err, 0, "Unexpected error %d", err);

	assert_msg_send(0, 0, NULL);
	k_sleep(K_MSEC(10));
	// Expect a REGISTER to be sent
	assert_msg_send(1, 12, &gateway);
	mqtt_sn_recv(&client, regack, sizeof(regack));
	assert_msg_send(0, 0, NULL);
	k_sleep(K_MSEC(10));
	assert_msg_send(1, 20, &gateway);

	zassert_true(sys_slist_is_empty(&client.publish), "Publish not empty");
	zassert_false(sys_slist_is_empty(&client.topic), "Topic empty");
}


void test_main(void)
{
	ztest_test_suite(test_mqtt_sn_client_fn, 
		ztest_unit_test(test_mqtt_sn_connect_no_will),
		ztest_unit_test(test_mqtt_sn_connect_will),
		ztest_unit_test(test_mqtt_sn_publish_qos0)
		);
	ztest_run_test_suite(test_mqtt_sn_client_fn);
}
