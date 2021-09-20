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
#include <esp_task_wdt.h>
#include <driver/uart.h>

#include <string.h>

#include "uart.h"
#include "hci.h"
#include "errors.h"

int uart_tx_pin = 17;
int uart_rx_pin = 5;

static struct
{
	int uart;
	uint32_t baudrate;
	char parity; /* n, o, e (none, odd, even) */
	uint8_t stopbits; /* 1, 2 */
	uint8_t flags;
} config;

const uint8_t UART_FLAG_INIT = (1 << 0);

int uart_init()
{

	/* Setup UART */
	config.baudrate = 115200;
	config.parity = 'n';
	config.stopbits = 1;
	config.uart = 2;

	uart_config_t uart_config =
	{
		.baud_rate = 115200,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
	};

	if(uart_set_pin(config.uart, uart_tx_pin, uart_rx_pin, -1, -1) != ESP_OK)
		goto esp_err;

	if(uart_param_config(config.uart, &uart_config) != ESP_OK)
		goto esp_err;

	if(uart_driver_install(config.uart, 1024, 1024, 0, NULL, 0) != ESP_OK)
		goto esp_err;

	config.flags |= UART_FLAG_INIT;

	return 0;

esp_err:
	printf("ERR UART init failed!\n");
	return -1;
}

void uart_command()
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
			"\n"
			"uart sendline <text> - send text line\n"
			"uart config baudrate [baudrate] - get or set baudrate\n"
			"uart config parity [parity] - get or set parity (n/e/o)\n"
			"uart config stopbits [stopbits] - get or set stopbits (1,2)\n"
			"\n");
	}
	else if(strcmp(cmd, "sendline") == 0)
	{
		const char *payload = strtok(NULL, "");

		if(payload == NULL)
			goto einval;

		uart_write_bytes(config.uart, payload, strlen(payload));
		uart_write_bytes(config.uart, "\n", 1);
		printf("OK\n");
	}
	else if(strcmp(cmd, "config") == 0)
	{
		/* Read config argument */
		const char *param = strtok(NULL, " ");
		if(!param)
			goto einval;

		if(strcmp(param, "baudrate") == 0)
		{
			/* [baudrate] */
			int baudrate;
			char *arg;

			/* Read baudrate argument */
			arg = strtok(NULL, " ");
			if(!arg)
			{
				printf("OK %u\n", config.baudrate);
				return;
			}

			baudrate = atoi(arg);

			if(baudrate < 0)
				goto einval;

			err = uart_set_baudrate(config.uart, baudrate);

			if(err != ESP_OK)
				goto einval;

			config.baudrate = baudrate;
			printf("OK\n");
		}
		else if(strcmp(param, "parity") == 0)
		{
			/* [parity] */
			char parity;
			int parity_value;
			char *arg;

			/* Read parity argument */
			arg = strtok(NULL, " ");
			if(!arg)
			{
				printf("OK %c\n", config.parity);
				return;
			}

			parity = arg[0];

			if(parity == 'n')
				parity_value = UART_PARITY_DISABLE;

			else if(parity == 'o')
				parity_value = UART_PARITY_ODD;

			else if(parity == 'e')
				parity_value = UART_PARITY_EVEN;

			else
				goto einval;

			err = uart_set_parity(config.uart, parity_value);

			if(err != ESP_OK)
				goto einval;

			config.parity = parity;
			printf("OK\n");
		}
		else if(strcmp(param, "stopbits") == 0)
		{
			/* [stopbits] */
			int stopbits;
			int stopbits_value;
			char *arg;

			/* Read stopbits argument */
			arg = strtok(NULL, " ");
			if(!arg)
			{
				printf("OK %u\n", config.stopbits);
				return;
			}

			stopbits = atoi(arg);

			if(stopbits == 1)
				stopbits_value = UART_STOP_BITS_1;

			else if(stopbits == 2)
				stopbits_value = UART_STOP_BITS_2;

			else
				goto einval;

			err = uart_set_stop_bits(config.uart, stopbits_value);

			if(err != ESP_OK)
				goto einval;

			config.stopbits = stopbits;
			printf("OK\n");
		}
		else
			goto einval;
	}
	else
		goto einval;

	return;

einval:
	printf(EINVAL);
	return;
}

void uart_thread(void *parameters)
{
	esp_task_wdt_delete(xTaskGetCurrentTaskHandle());

	while(!(config.flags & UART_FLAG_INIT))
		vTaskDelay(100 * portTICK_PERIOD_MS);

	while(1)
	{
		uint8_t c;
		int ret = uart_read_bytes(config.uart, &c, 1, 100 / portTICK_RATE_MS);
		if(ret > 0)
		{
			if(c < 0x20)
				printf("UART: %02x\n", c);
			else
				printf("UART: %02x %c\n", c, c);
		}
	}
}

