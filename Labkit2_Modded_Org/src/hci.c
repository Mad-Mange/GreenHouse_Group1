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
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <driver/uart.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "config.h"
#include "hci.h"
#include "periodic.h"
#include "adc.h"
#include "dac.h"
#include "calibration.h"
#include "can.h"
#include "led.h"
#include "lin.h"
#include "uart.h"

static void hci_line_handler(char *line);

static const int uart = UART_NUM_0;
static QueueHandle_t uart_queue;
static QueueSetHandle_t hci_queue_set;

/* TX functionality */
#define TX_SLOTS 10
static struct
{
	uint16_t period; /* 0 = inactive */
	uint16_t bytes;
} tx_scheduling[TX_SLOTS];

static const int baudrate = 2000000;
static const int bytes_per_second = baudrate / 10;

/*******************************************************************************
 * May be called from other threads
 *
 * Return value: TX handle
 ******************************************************************************/
int hci_alloc_tx_slot(uint16_t period, uint16_t bytes)
{
	int slot = -1;
	float bytes_per_tick = 0;

	if(period == 0)
		return -1;

	for(int i = 0; i < TX_SLOTS; i++)
	{
		if(slot < 0 && tx_scheduling[i].period == 0)
			slot = i;

		if(tx_scheduling[i].period != 0)
			bytes_per_tick +=
				tx_scheduling[i].bytes / (float) tx_scheduling[i].period;
	}

	if(slot < 0)
		return -1;

	bytes_per_tick += bytes / (float) period;

	/* Only allow 90 % TX to periodic data */
	if(bytes_per_tick / bytes_per_second > 0.9)
		return -1;

	tx_scheduling[slot].period = period;
	tx_scheduling[slot].bytes = bytes;

	return slot;
}

/*******************************************************************************
 * May be called from other threads
 ******************************************************************************/
void hci_free_tx_slot(int tx_handle)
{
	tx_scheduling[tx_handle].period = 0;
}

/*******************************************************************************
 * May be called from other threads
 ******************************************************************************/
void hci_print_str(const char *format, ...)
{
	va_list arglist;
	va_start(arglist, format);

	char buf[1024];
	vsnprintf(buf, 1023, format, arglist);
	buf[1023] = 0;
	uart_write_bytes(uart, buf, strlen(buf));

	va_end(arglist);
}

/*******************************************************************************
 * May be called from other threads
 ******************************************************************************/
void hci_print_bytes(const uint8_t *data, int len)
{
	uart_write_bytes(uart, (const char*)data, len);
}

/*******************************************************************************
 *
 ******************************************************************************/
static const char *get_reset_reason()
{
	esp_reset_reason_t reason = esp_reset_reason();
	const char *reason_str;

	if(reason == ESP_RST_POWERON)
		reason_str = "Power on";

	else if(reason == ESP_RST_SW)
		reason_str = "SW reset";

	else if(reason == ESP_RST_PANIC)
		reason_str = "OS panic";

	else if(reason == ESP_RST_INT_WDT ||
	        reason == ESP_RST_TASK_WDT ||
	        reason == ESP_RST_WDT)
		reason_str = "WDT reset";

	else if(reason == ESP_RST_BROWNOUT)
		reason_str = "Brownout";

	else
		reason_str = "Unknown";

	return reason_str;
}

/*******************************************************************************
 *
 ******************************************************************************/
void hci_init()
{
	/* Setup UART */

	uart_config_t uart_config =
	{
		.baud_rate = 2000000,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
	};
	uart_param_config(uart, &uart_config);
	uart_driver_install(uart, 1024, 8192, 10, &uart_queue, 0);

	const char *reset_reason = get_reset_reason();

	printf("SWT21 lab kit\nBooting...\n");
	printf("Firmware version: %s\n", GIT_TAG);
	printf("Source code revision: %s\n", GIT_REV);
	printf("Reset reason: %s\n\n", reset_reason);

	/* Clear RX buffer */
	uart_flush(uart);
}

/*******************************************************************************
 *
 ******************************************************************************/
void hci_thread(void *parameters)
{
	esp_task_wdt_delete(xTaskGetCurrentTaskHandle());

	hci_queue_set = xQueueCreateSet(20);
	xQueueAddToSet(uart_queue, hci_queue_set);

	/* Start hci RX */
	const int buffer_size = 256;

	char line[buffer_size];
	int index = 0;

	while(1)
	{
		QueueSetMemberHandle_t queue =
			xQueueSelectFromSet(hci_queue_set, 1000 / portTICK_RATE_MS);

		if(queue == uart_queue)
		{
			uart_event_t event;
			xQueueReceive(uart_queue, &event, 0);

			/* Only handle received data */
			if(event.type != UART_DATA)
				continue;

			while(1)
			{
				uint8_t c;
				int ret = uart_read_bytes(uart, &c, 1, 100 / portTICK_RATE_MS);

				/* If we did not receive any character then break loop */
				if(ret != 1)
					break;

				/* Convert DEL (0x7f) into backspace (0x08), needed in some terminals */
				if(c == '\x7f')
					c = '\b';

				/* Handle backspace specifically */
				if(c == '\b')
				{
					if(index > 0)
					{
						index -= 1;
						printf("\b \b"); /* Back, overwrite with space and back again */
					}
				}
				/* Handle new line */
				else if(c == '\n')
				{
					printf("\n");
					line[index] = 0;
					hci_line_handler(line);
					index = 0;
				}
				/* Ignore all other control characters */
				else if(c < 0x20)
				{
					continue;
				}
				/* Otherwise echo character written */
				else
				{
					uart_write_bytes(uart, (char*)&c, 1);
					line[index++] = c;
				}
			}
		}
	}
}

/*******************************************************************************
 *
 ******************************************************************************/
static void hci_line_handler(char *line)
{
	char *cmd = strtok(line, " ");

	/* Make sure we have a command */
	if(cmd == NULL)
		return;

	/*
	 * Compare commande against known commands and handle them, otherwise tell
	 * the user it was invalid.
	 */
	if(strcmp(cmd, "help") == 0)
	{
		printf("OK\n");
		printf(
			"Available commands:\n"
			"\n"
			"help - write this text\n"
			"adc0 help - write all adc0 commands\n"
			"adc1 help - write all adc1 commands\n"
			"dac0 help - write all dac0 commands\n"
			"dac1 help - write all dac1 commands\n"
			"calibration help - write all calibration commands\n"
			"can help - write all can commands\n"
			"led help - write all led commands\n"
			"\n");
	}
	else if(strcmp(cmd, "adc0") == 0)
		adc_command(0);

	else if(strcmp(cmd, "adc1") == 0)
		adc_command(1);

	else if(strcmp(cmd, "dac0") == 0)
		dac_command(0);

	else if(strcmp(cmd, "dac1") == 0)
		dac_command(1);

	else if(strcmp(cmd, "calibration") == 0)
		calibration_command();

	else if(strcmp(cmd, "can") == 0)
		can_command();

	else if(strcmp(cmd, "led") == 0)
		led_command();

	else if(strcmp(cmd, "lin") == 0)
		lin_command();

	else if(strcmp(cmd, "uart") == 0)
		uart_command();

	else
		printf("ERR Unknown command\n");
}
