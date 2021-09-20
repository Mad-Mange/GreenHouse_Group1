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
#include <driver/gpio.h>
#include <nvs_flash.h>
#include <esp_task_wdt.h>

#include <string.h>

#include "periodic.h"
#include "errors.h"
#include "led.h"
#include "hci.h"

const int led_pin = GPIO_NUM_23;

static struct
{
	uint8_t flags; /* initialized */
} led_config;

const uint8_t LED_FLAG_INIT = 1 << 0;

int led_init()
{
	gpio_pad_select_gpio(led_pin);

	if(gpio_set_direction(led_pin, GPIO_MODE_OUTPUT) != ESP_OK)
		goto esp_err;

	led_config.flags |= LED_FLAG_INIT;

	return 0;

esp_err:
	printf("ERR LED init failed!\n");
	return -1;
}

void led_command()
{
	char *cmd = strtok(NULL, " ");

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
			"led on - \n"
			"led off - \n"
			"lin blink [half period] - half period in ms (int, default: 500)\n");
	}
	else if(strcmp(cmd, "on") == 0)
	{
		led_off(1);
	}
	else if(strcmp(cmd, "off") == 0)
	{
		led_off(0);
	}
	else if(strcmp(cmd, "blink") == 0)
	{
		const char *arg = strtok(NULL, " ");
		int period = 500;

		if(arg)
			period = atoi(arg);

		led_blink(period, 0);
	}
	else
		goto einval;

	return;

einval:
	printf(EINVAL);
	return;
}

void led_set_state(int state)
{
	gpio_set_level(led_pin, state);
}
