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
#include <driver/uart.h>
#include <driver/gpio.h>
#include <nvs_flash.h>
#include <esp_task_wdt.h>

#include <string.h>

#include "periodic.h"
#include "errors.h"
#include "lin.h"
#include "hci.h"

#define ELINPROTO "LIN RX protocol error\n"
#define ELINCHKS "LIN RX checksum error\n"

const int lin_tx_pin = GPIO_NUM_4;
const int lin_rx_pin = GPIO_NUM_16;
const int uart = 1;
const int break_len = 13;

const int CHECKSUM_TYPE_CLASSIC = 0;
const int CHECKSUM_TYPE_ENHANCED = 1;

const int LIN_SYNC = 0x55;

struct
{
	uint8_t flags; /* initialized, running */
} lin_config;

struct lin_frame
{
	uint8_t id;
	uint8_t data[8];
};

static struct
{
	uint16_t period;
	uint16_t offset;
	uint32_t next;
} schedule[64];

static struct
{
	uint64_t rx_frames;
	uint64_t tx_frames;
	uint8_t frame_len[64];
	uint64_t frame_checksums;
	uint8_t frame_data[64][8];
	uint8_t flags;
} config;

const uint8_t LIN_FLAG_INIT = 1 << 0;
const uint8_t LIN_FLAG_RUNNING = 1 << 1;
const uint8_t LIN_FLAG_MASTER = 1 << 2;
const uint8_t LIN_FLAG_CHKS_ENHANCED = 1 << 3;

static QueueHandle_t lin_queue;
static QueueHandle_t uart_queue;
static QueueSetHandle_t lin_queue_set;

struct lin_event
{
	uint8_t event;

	union
	{
		struct
		{
			uint8_t id;
		} send;
	};
};

enum
{
	EVENT_LIN_OFF = 0,
	EVENT_LIN_ON,
	EVENT_LIN_SEND
};


int lin_init()
{
	/* Setup UART for LIN */
	uart_config_t uart_config =
	{
		.baud_rate = 19200,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
	};

	if(uart_set_pin(uart, lin_tx_pin, lin_rx_pin, -1, -1) != ESP_OK)
		goto esp_err;

	if(uart_param_config(uart, &uart_config) != ESP_OK)
		goto esp_err;

	if(uart_driver_install(uart, 1024, 1024, 10, &uart_queue, 0) != ESP_OK)
		goto esp_err;

	lin_queue = xQueueCreate(10, sizeof(struct lin_event));
	if(!lin_queue)
		goto esp_err;

	lin_config.flags |= LIN_FLAG_INIT;

	return 0;

esp_err:
	printf("ERR LIN init failed!\n");
	return -1;
}

static int parse_frame_format(struct lin_frame *frame, const char *frame_str)
{
	uint32_t id;
	int data[8];


	int ret = sscanf(frame_str, "%d#%2x%2x%2x%2x%2x%2x%2x%2x",
		&id, &data[0], &data[1], &data[2], &data[3],
		&data[4], &data[5], &data[6], &data[7]);

	if(ret < 1)
		return -1;

	if(id > 63)
		return -1;

	int len = ret - 1;

	memset(frame, 0, sizeof(*frame));
	frame->id = id;
	for(int i = 0; i < len; i++)
		frame->data[i] = data[i];

	return 0;
}

static void send_break(int break_len)
{
	/* Disable interrupts for precise timing */
	portDISABLE_INTERRUPTS();

	/* Restore GPIO output to LIN tx pin */
	gpio_matrix_out(lin_tx_pin, 0x100, 0, 0);

	/* Send break */
	gpio_set_level(lin_tx_pin, 0);
	ets_delay_us(52.08*break_len);
	gpio_set_level(lin_tx_pin, 1);
	ets_delay_us(52.08*1);

	/* Reenable interrupts and restore uart pins */
	portENABLE_INTERRUPTS();
	uart_set_pin(uart, lin_tx_pin, lin_rx_pin, -1, -1);
}

void lin_command()
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
			"<id> is decimal\n"
			"<data> is hexadecimal\n"
			"<chks-type> is checksum type with: 0: classic, 1: enhanced\n"
			"\n"
			"lin txbuf <id>#<data> - set LIN response data\n"
			"lin single <id> - send single LIN header\n"
			"lin config rx <id> <len> <chks-type> - configure LIN id for reading with\n"
			"                                       len and checksum\n"
			"lin config rx <id> off - disable reading for id\n"
			"lin config rx <id> - read current rx state for id\n"
			"lin config tx <id> <len> <chks-type> - configure LIN id for writing\n"
			"lin config tx <id> off - disable writing for id\n"
			"lin config tx <id> - read current tx state for id\n"
#if 0
/*
 * LIN schedule is TODO in a later release
 */
			"lin config master [1/0] - get or set master role\n"
			"lin config schedule <id> <period> <offset>  - \n"
			"lin config schedule <id> off - \n"
			"lin config schedule <id> - get id schedule\n"
			"lin config schedule - get schedule\n"
#endif
			"\n");
	}
	else if(strcmp(cmd, "on") == 0)
	{
		lin_on();
	}
	else if(strcmp(cmd, "off") == 0)
	{
		lin_off();
	}
	else if(strcmp(cmd, "txbuf") == 0)
	{
		struct lin_frame frame;
		const char *frame_str = strtok(NULL, " ");

		memset(&frame, 0, sizeof(frame));
		if(parse_frame_format(&frame, frame_str) < 0)
			goto einval;

		memcpy(config.frame_data[frame.id], frame.data, 8);

		printf("OK\n");
	}
	else if(strcmp(cmd, "single") == 0)
	{
		int id;
		char *arg;

		/* Read id argument */
		arg = strtok(NULL, " ");
		if(!arg)
			goto einval;

		id = atoi(arg);

		if(id > 63 || id < 0)
			goto einval;

		/* Send command */
		lin_send(id);
	}
	else if(strcmp(cmd, "config") == 0)
	{
		/* Read config argument */
		const char *param = strtok(NULL, " ");
		if(!param)
			goto einval;

		if(strcmp(param, "rx") == 0)
		{
			/* <id> [<len> <chks>] */
			int id;
			int len;
			int chks;
			char *arg;

			/* Read id argument */
			arg = strtok(NULL, " ");
			if(!arg)
				goto einval;

			id = atoi(arg);

			if(id > 63 || id < 0)
				goto einval;

			/* Read len argument */
			arg = strtok(NULL, " ");
			if(!arg)
			{
				int rx = config.rx_frames & (1 << id);
				int len = config.frame_len[id];
				int chks = config.frame_checksums & (1 << id)? 1 : 0;

				printf("OK %d %d %d\n", rx, len, chks);

				return;
			}

			if(strcmp(arg, "off") == 0)
			{
				config.rx_frames &= ~(1 << id);
				printf("OK\n");
				return;
			}

			len = atoi(arg);

			if(len > 8 || len < 1)
				goto einval;

			config.frame_len[id] = len;

			/* Read chks argument */
			arg = strtok(NULL, " ");
			if(!arg)
				goto einval;

			chks = atoi(arg);

			if(chks == 0)
				config.frame_checksums &= ~(1 << id);

			else if(chks == 1)
				config.frame_checksums |= 1 << id;

			else
				goto einval;

			config.rx_frames |= 1 << id;

			printf("OK\n");
		}

		else if(strcmp(param, "tx") == 0)
		{
			/* <id> [<len> <chks>] */
			int id;
			int len;
			int chks;
			char *arg;

			/* Read id argument */
			arg = strtok(NULL, " ");
			if(!arg)
				goto einval;

			id = atoi(arg);

			if(id > 63 || id < 0)
				goto einval;

			/* Read len argument */
			arg = strtok(NULL, " ");
			if(!arg)
			{
				int tx = config.tx_frames & (1 << id);
				int len = config.frame_len[id];
				int chks = config.frame_checksums & (1 << id)? 1 : 0;

				printf("OK %d %d %d\n", tx, len, chks);

				return;
			}

			if(strcmp(arg, "off") == 0)
			{
				config.tx_frames &= ~(1 << id);
				printf("OK\n");
				return;
			}

			len = atoi(arg);

			if(len > 8 || len < 1)
				goto einval;

			config.frame_len[id] = len;

			/* Read chks argument */
			arg = strtok(NULL, " ");
			if(!arg)
				goto einval;

			chks = atoi(arg);

			if(chks == 0)
				config.frame_checksums &= ~(1 << id);

			else if(chks == 1)
				config.frame_checksums |= 1 << id;

			else
				goto einval;

			config.tx_frames |= 1 << id;

			printf("OK\n");
		}
	}
	else
		goto einval;

	return;

einval:
	printf(EINVAL);
	return;
}

void lin_off()
{
	struct lin_event event =
	{
		.event = EVENT_LIN_OFF
	};

	xQueueSendToBack(lin_queue, &event, 0);
}

void lin_on()
{
	struct lin_event event =
	{
		.event = EVENT_LIN_ON
	};

	xQueueSendToBack(lin_queue, &event, 0);
}

void lin_send(int id)
{
	struct lin_event event =
	{
		.event = EVENT_LIN_SEND,
		.send.id = id
	};

	xQueueSendToBack(lin_queue, &event, 0);
}

static uint8_t checksummed_id(uint8_t id)
{
	int p0 = ((id >> 0) ^ (id >> 1) ^ (id >> 2) ^ (id << 4)) & 0x1;
	int p1 = ((id >> 1) ^ (id >> 3) ^ (id >> 4) ^ (id << 5)) & 0x1;

	return (id & 0x3f) | (p0 << 6) | (p1 << 7);
}

int frame_rx(uint8_t id)
{
	if(id >= 64)
		return 0;

	if((config.rx_frames >> id) & 0x1)
		return 1;

	return 0;
}

int frame_tx(uint8_t id)
{
	if(id >= 64)
		return 0;

	if((config.tx_frames >> id) & 0x1)
		return 1;

	return 0;
}

static uint8_t checksum_data(uint8_t id, uint8_t *data, int len)
{
	int type = (config.frame_checksums >> id) & 0x1;
	uint16_t checksum = 0;

	if(type == CHECKSUM_TYPE_CLASSIC)
	{
		for(int i = 0; i < len; i++)
			checksum += data[i];
	}
	else
	{
		for(int i = 0; i < len; i++)
		{
			checksum += data[i];
			if(checksum >= 256)
				checksum -= 255;
		}
	}

	return checksum;
}

static void handle_lin_rx(uint8_t id, uint8_t *data, int len)
{
	char buf[17];

	for(int i = 0; i < len; i++)
		sprintf(&buf[2*i], "%02x", data[i]);

	printf("LIN RX: %d#%s\n", id, buf);
}

static void send_lin_header(int id)
{
	send_break(break_len);

	char hdr[] = { 0x55, checksummed_id(id) };

	uart_write_bytes(uart, hdr, 2);
}

void lin_thread(void *parameters)
{
	esp_task_wdt_delete(xTaskGetCurrentTaskHandle());

	/* Check that LIN initialized correctly */
	while(!(lin_config.flags & LIN_FLAG_INIT))
		vTaskDelay(100 * portTICK_PERIOD_MS);

	lin_queue_set = xQueueCreateSet(20);
	xQueueAddToSet(uart_queue, lin_queue_set);
	xQueueAddToSet(lin_queue, lin_queue_set);

	int rx_running = 0;
	uint64_t id_pending = 0;

	enum
	{
		STATE_IDLE = 0,
		STATE_WAIT_SYNC,
		STATE_WAIT_ID,
		STATE_WAIT_DATA,
		STATE_WAIT_CHECKSUM
	} state = STATE_IDLE;

	uint8_t current_id = 0;
	uint8_t data_len = 0;
	uint8_t data_read = 0;
	uint8_t data_rx[8];

	while(1)
	{
		QueueSetMemberHandle_t queue =
			xQueueSelectFromSet(lin_queue_set, 10 / portTICK_RATE_MS);

		if(queue == uart_queue)
		{
			uart_event_t event;
			xQueueReceive(uart_queue, &event, 0);

			/* Only handle received data */
			if(event.type == UART_DATA)
			{
				uint8_t c;
				for(int i = 0; i < event.size; i++)
				{
					int ret = uart_read_bytes(uart, &c, 1, 1);

					if(ret != 1)
						continue;

					if(state == STATE_WAIT_SYNC)
					{
						state = STATE_WAIT_ID;

						if(c != LIN_SYNC)
						{
							printf(ELINPROTO);
							uart_flush(uart);
							state = STATE_IDLE;
						}
					}
					else if(state == STATE_WAIT_ID)
					{
						current_id = c & 0x3f;
						if(checksummed_id(current_id) != c)
						{
							printf(ELINPROTO);
							uart_flush(uart);
							state = STATE_IDLE;
						}

						/* Send data if we have it */
						if(frame_tx(current_id))
						{
							data_len = config.frame_len[current_id];
							uart_write_bytes(uart, (char*)config.frame_data[current_id], data_len);

							char checksum = checksum_data(current_id, config.frame_data[current_id], data_len);
							uart_write_bytes(uart, &checksum, 1);
						}

						/* Read data if RX configured otherwise wait for next break */
						if(frame_rx(current_id))
						{
							state = STATE_WAIT_DATA;
							data_len = config.frame_len[current_id];
							data_read = 0;
						}
						else
							state = STATE_IDLE;
					}
					else if(state == STATE_WAIT_DATA)
					{
						data_rx[data_read] = c;
						data_read += 1;

						if(data_read == data_len)
							state = STATE_WAIT_CHECKSUM;
					}
					else if(state == STATE_WAIT_CHECKSUM)
					{
						uint8_t checksum = checksum_data(current_id, data_rx, data_len);

						if(c != checksum)
							printf(ELINCHKS);
						else
							handle_lin_rx(current_id, data_rx, data_len);

						state = STATE_IDLE;

						if(id_pending)
						{
							for(int i = 0; i < 64; i++)
								if(id_pending & (1 << i ))
									break;

							id_pending &= ~(1 << i);
							current_id = i;

							send_lin_header(current_id);
							state = STATE_WAIT_DATA;
						}
					}
					else
					{
						/* Ignore, e.g. non-interesting id */
					}
				}
			}
			else if(event.type == UART_BREAK)
			{
				uint8_t c;
				int ret = uart_read_bytes(uart, &c, 1, 10);

				state = STATE_WAIT_SYNC;
			}
			else
				continue;

		}
		else if(queue == lin_queue)
		{
			struct lin_event event;

			if(xQueueReceive(lin_queue, &event, 0))
			{
				if(event.event == EVENT_LIN_OFF)
				{
					rx_running = 0;
					printf("OK\n");
				}

				else if(event.event == EVENT_LIN_ON)
				{
					rx_running = 1;
					printf("OK\n");
				}

				else if(event.event == EVENT_LIN_SEND)
				{
					if(state == STATE_IDLE)
					{
						current_id = event.send.id;
						send_lin_header(current_id);
						state = STATE_WAIT_DATA;
					}
					else
						id_pending |= 1 << event.send.id;

					printf("OK\n");
				}
			}
		}

		if(!queue)
		{
			/* Queue wait timeout */
			if(state != STATE_IDLE)
			{
				printf("ERR LIN frame %d timed out!\n", current_id);
				state = STATE_IDLE;
			}
		}
	}
}
