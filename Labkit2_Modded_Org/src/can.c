/*
 *  This file is part of SWT21 lab kit firmware.
 *
 *  SWT21 lab kit firmware is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  SWT21 lab kit firmware is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with SWT21 lab kit firmware.  If not, see <https://www.gnu.org/licenses/>.
 *
 *  Copyright 2021 Joachim Lublin, Binäs Teknik AB
 */

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <driver/can.h>
#include <nvs_flash.h>
#include <esp_task_wdt.h>

#include <string.h>

#include "periodic.h"
#include "errors.h"
#include "can.h"
#include "hci.h"

const int can_tx_pin = GPIO_NUM_0;
const int can_rx_pin = GPIO_NUM_2;

struct
{
	can_timing_config_t timing;
	can_filter_config_t filter;
	can_message_t periodic_messages[8];

	uint8_t flags; /* initialized, rx */
} can_config;

can_general_config_t config =
{
	.mode = CAN_MODE_NORMAL,
	.tx_io = can_tx_pin,
	.rx_io = can_rx_pin,
	.clkout_io = -1,
	.bus_off_io = -1,
	.tx_queue_len = 10,
	.rx_queue_len = 10,
	.alerts_enabled = CAN_ALERT_NONE,
	.clkout_divider = 0
};

const uint8_t CAN_FLAG_INIT = 1 << 0;
const uint8_t CAN_FLAG_RX_ON = 1 << 1;

static QueueHandle_t can_rx_queue;

struct can_rx_event
{
	uint8_t event;
};

enum
{
	EVENT_CAN_RX_OFF = 0,
	EVENT_CAN_RX_ON = 1
};


int can_init()
{
	esp_err_t err;

	/* Setup default timing */
	// can_config.timing.brp = 8;
	// can_config.timing.tseg_1 = 5;
	// can_config.timing.tseg_2 = 4;
	// can_config.timing.sjw = 1;
	//// SimuLink Speed
	can_config.timing.brp = 10;
	can_config.timing.tseg_1 = 8;
	can_config.timing.tseg_2 = 7;
	can_config.timing.sjw = 1;

	/* Setup default filter */
	can_config.filter.acceptance_code = 0;
	can_config.filter.acceptance_mask = 0xffffffff;
	can_config.filter.single_filter = 1;

	err = can_driver_install(&config, &can_config.timing, &can_config.filter);

	if(err != ESP_OK)
	{
		printf("ERR CAN init failed!\n");
		return -1;
	}

	err = can_start();
	if(err != ESP_OK)
	{
		printf("ERR CAN init failed!\n");
		return -1;
	}

	can_config.flags |= CAN_FLAG_INIT;

	return 0;
}

int can_reinstall()
{
	esp_err_t err;

	can_stop();
	can_driver_uninstall();

	/* Setup default timing */
	err = can_driver_install(&config, &can_config.timing, &can_config.filter);

	if(err != ESP_OK)
		return -1;

	err = can_start();

	if(err != ESP_OK)
		return -1;

	return 0;
}

int parse_message_format(can_message_t *msg, const char *fmt)
{
	uint32_t id;
	char remote;

	memset(msg, 0, sizeof(*msg));
	sscanf(fmt, "%x#%c", &id, &remote);

	if(remote == 'R')
	{
		msg->flags |= CAN_MSG_FLAG_RTR;
		msg->identifier = id;
		msg->data_length_code = 0;
	}
	else
	{
		int data[8];
		int ret = sscanf(fmt, "%x#%2x%2x%2x%2x%2x%2x%2x%2x",
			&id, &data[0], &data[1], &data[2], &data[3],
			&data[4], &data[5], &data[6], &data[7]);

		if(ret < 1)
			return -1;

		int len = ret - 1;

		msg->identifier = id;
		msg->data_length_code = len;
		for(int i = 0; i < len; i++)
			msg->data[i] = data[i];
	}

	return 0;
}

void can_command()
{
	char *cmd = strtok(NULL, " ");
	esp_err_t err;

	/* Make sure we have a command */
	if(!cmd)
		goto einval;

	/*
	 * Compare commande against known commands and handle them, otherwise tell
	 * the user it was invalid.
	 */
	if(strcmp(cmd, "help") == 0)
	{
		printf("OK\n");
		printf(
			"Available commands:\n"
			"Source clock: 80 MHz\n"
			"CAN bitrate = 80 000 000 / brp / (1 + tseg_1 + tseg_2)\n"
			"\n"
			"can help - write this text\n"
			"can rx on/off - enable or disable RX\n"
			"can send <id>#<data in hex> - e.g. send 13f#02e8\n"
			"can config brp [value] - get or set current can brp (2-128, even)\n"
			"can config tseg_1 [value] - get or set current can tseg_1 (1-16)\n"
			"can config tseg_2 [value] - get or set current can tseg_2 (1-8)\n"
			"can config sjw [value] - get or set current can sjw (1-4)\n"
			"\n");
	}
	else if(strcmp(cmd, "rx") == 0)
	{
		const char *arg = strtok(NULL, " ");

		if(strcmp(arg, "on") == 0)
			can_rx_on();
		else if(strcmp(arg, "off") == 0)
			can_rx_off();
		else
			goto einval;
	}
	else if(strcmp(cmd, "send") == 0)
	{
		can_message_t msg;
		const char *msg_str = strtok(NULL, " ");

		memset(&msg, 0, sizeof(msg));
		if(parse_message_format(&msg, msg_str) < 0)
			goto einval;

		err = can_transmit(&msg, 0);
		if(err != ESP_OK)
		{
			printf("ERR Transmit failed!\n");
			return;
		}

		printf("OK\n");
	}
	else if(strcmp(cmd, "config") == 0)
	{
		/* Read value argument */
		const char *arg = strtok(NULL, " ");
		if(!arg)
			goto einval;

		if(strcmp(arg, "brp") == 0)
		{
			const char *value_str = strtok(NULL, " ");

			if(!value_str)
				printf("OK %d\n", can_config.timing.brp);

			else
			{
				int value = atoi(value_str);

				if(value < 2 || value > 127 || value % 2)
					goto einval;

				can_config.timing.brp = value;
				if(can_reinstall() < 0)
					printf("ERR Unknown error\n");
				else
					printf("OK\n");
			}
		}
		else if(strcmp(arg, "tseg_1") == 0)
		{
			const char *value_str = strtok(NULL, " ");

			if(!value_str)
				printf("OK %d\n", can_config.timing.tseg_1);

			else
			{
				int value = atoi(value_str);

				if(value < 1 || value > 16 )
					goto einval;

				can_config.timing.tseg_1 = value;
				if(can_reinstall() < 0)
					printf("ERR Unknown error\n");
				else
					printf("OK\n");
			}
		}
		else if(strcmp(arg, "tseg_2") == 0)
		{
			const char *value_str = strtok(NULL, " ");

			if(!value_str)
				printf("OK %d\n", can_config.timing.tseg_2);

			else
			{
				int value = atoi(value_str);

				if(value < 1 || value > 8 )
					goto einval;

				can_config.timing.tseg_2 = value;
				if(can_reinstall() < 0)
					printf("ERR Unknown error\n");
				else
					printf("OK\n");
			}
		}
		else if(strcmp(arg, "sjw") == 0)
		{
			const char *value_str = strtok(NULL, " ");

			if(!value_str)
				printf("OK %d\n", can_config.timing.sjw);

			else
			{
				int value = atoi(value_str);

				if(value < 1 || value > 4 )
					goto einval;

				can_config.timing.sjw = value;

				if(can_reinstall() < 0)
					printf("ERR Unknown error\n");
				else
					printf("OK\n");
			}
		}
		else
			goto einval;
	}
	else if(strcmp(cmd, "status") == 0)
	{
	}
	else
		goto einval;

	return;

einval:
	printf(EINVAL);
	return;
}

void can_rx_off()
{
	struct can_rx_event event =
	{
		.event = EVENT_CAN_RX_OFF
	};

	xQueueSendToBack(can_rx_queue, &event, 0);
}

void can_rx_on()
{
	struct can_rx_event event =
	{
		.event = EVENT_CAN_RX_ON
	};

	xQueueSendToBack(can_rx_queue, &event, 0);
}

void can_rx_thread(void *parameters)
{
	/* Check that CAN initialized correctly */
	while(!(can_config.flags & CAN_FLAG_INIT))
		vTaskDelay(100 * portTICK_PERIOD_MS);

	esp_task_wdt_delete(xTaskGetCurrentTaskHandle());

	can_rx_queue = xQueueCreate(10, sizeof(struct can_rx_event));

	int rx_running = 0;

	while(1)
	{
		can_message_t msg;
		while(can_receive(&msg, 100) == ESP_OK)
		{
			if(rx_running)
			{
				printf("CAN RX: %x#", msg.identifier);
				if(msg.flags & CAN_MSG_FLAG_RTR)
					printf("R");
				else
					for(int i = 0; i < msg.data_length_code; i++)
						printf("%02x", msg.data[i]);

				printf("\n");

			/////////////////////////////// Added Code by Group1 ///////////////////////////////////////////
				can_transmit(&msg, 0);
				printf("CAN TX: %x#", msg.identifier);
				if(msg.flags & CAN_MSG_FLAG_RTR)
					printf("R");
				else
					for(int i = 0; i < msg.data_length_code; i++)
						printf("%02x", msg.data[i]);
				printf("\n");
			////////////////////////////////////////////////////////////////////////////////////////////////
			}
		}

		struct can_rx_event event;
		if(xQueueReceive(can_rx_queue, &event, 0))
		{
			if(event.event == EVENT_CAN_RX_OFF)
			{
				rx_running = 0;
				printf("OK\n");
			}

			else if(event.event == EVENT_CAN_RX_ON)
			{
				rx_running = 1;
				printf("OK\n");
			}
		}
	}
}
