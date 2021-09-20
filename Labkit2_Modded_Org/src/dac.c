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
#include <driver/gpio.h> /* Require by driver/dac.h */
#include <driver/dac.h>
#include <nvs_flash.h>

#include <string.h>

#include "errors.h"
#include "dac.h"
#include "hci.h"

int dac_channel[DAC_COUNT] =
{
	DAC_CHANNEL_1,
	DAC_CHANNEL_2
};

struct
{
	float min_1x;
	float max_1x;
	float min_10x;
	float max_10x;
	uint8_t flags; /* initialized */
} dac_config[DAC_COUNT];

const uint8_t DAC_FLAG_INIT = 1 << 0;
const uint8_t DAC_FLAG_AMP10X = 1 << 1;

int dac_init()
{
	esp_err_t err;

	nvs_handle_t nvs_handle;
	err = nvs_open("SWT21 Lab kit", NVS_READWRITE, &nvs_handle);
	if(err != ESP_OK)
	{
		printf("ERR DAC init failed!\n");
		return -1;
	}


	for(int i = 0; i < DAC_COUNT; i++)
	{
		int error_flag = 0;
		char parameter_name[24] = {0};
		union
		{
			float f;
			uint32_t u;
		} value;

		snprintf(parameter_name, 23, "dac%d_min_1x", i);
		err = nvs_get_u32(nvs_handle, parameter_name, &value.u);
		if(err)
		{
			printf("ERR %s not configured\n", parameter_name);
			error_flag = 1;
		}
		dac_config[i].min_1x = value.f;

		snprintf(parameter_name, 23, "dac%d_max_1x", i);
		err = nvs_get_u32(nvs_handle, parameter_name, &value.u);
		if(err)
		{
			printf("ERR %s not configured\n", parameter_name);
			error_flag = 1;
		}
		dac_config[i].max_1x = value.f;

		snprintf(parameter_name, 23, "dac%d_min_10x", i);
		err = nvs_get_u32(nvs_handle, parameter_name, &value.u);
		if(err)
		{
			printf("ERR %s not configured\n", parameter_name);
			error_flag = 1;
		}
		dac_config[i].min_10x = value.f;

		snprintf(parameter_name, 23, "dac%d_80_10x", i);
		err = nvs_get_u32(nvs_handle, parameter_name, &value.u);
		if(err)
		{
			printf("ERR %s not configured\n", parameter_name);
			error_flag = 1;
		}
		dac_config[i].max_10x = value.f;

		/* Enable output */
		dac_output_enable(dac_channel[i]);

		/* Mark as initialized if we did not have any errors */
		if(!error_flag)
			dac_config[0].flags |= DAC_FLAG_INIT;
	}

	nvs_close(nvs_handle);

	return 0;
}

void dac_command(int dac)
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
			"dac%d voltage <voltage> - set dac voltage\n"
			"dac%d raw <value> - set dac raw value (0-255)\n"
			"dac%d config 10x [on/off] - set or get current amplification\n"
			"\n", dac, dac, dac);
	}
	else if(strcmp(cmd, "voltage") == 0)
	{
		/* Read value argument */
		const char *arg = strtok(NULL, " ");
		if(!arg)
			goto einval;

		float voltage = strtof(arg, NULL);

		if(dac_config[dac].flags & DAC_FLAG_AMP10X)
		{
			if(voltage < 0 || voltage > 33)
				goto einval;

			uint8_t value =
				(voltage - dac_config[dac].min_10x) * 80 /
				(dac_config[dac].max_10x - dac_config[dac].min_10x);

			dac_output_voltage(dac_channel[dac], value);
		}
		else
		{
			int value =
				(voltage - dac_config[dac].min_1x) * 255 /
				(dac_config[dac].max_1x - dac_config[dac].min_1x);

			uint8_t dac_value;

			if(value < 0)
				dac_value = 0;

			else if(value > 255)
				dac_value = 255;

			else
				dac_value = value;

			dac_output_voltage(dac_channel[dac], dac_value);
		}
	}
	else if(strcmp(cmd, "raw") == 0)
	{
		/* Read value argument */
		const char *arg = strtok(NULL, " ");
		if(!arg)
			goto einval;

		int raw = atoi(arg);

		if(raw < 0 || raw > 255)
			goto einval;

		dac_output_voltage(dac_channel[dac], raw);
		printf("OK\n");
	}
	else if(strcmp(cmd, "config") == 0)
	{
		/* Read value argument */
		const char *arg = strtok(NULL, " ");
		if(!arg)
			goto einval;

		if(strcmp(arg, "10x") == 0)
		{
			const char *value_str = strtok(NULL, " ");
			if(!value_str)
			{
				if(dac_config[dac].flags & DAC_FLAG_AMP10X)
					printf("OK on\n");
				else
					printf("OK off\n");
			}
			else
			{
				if(strcmp(value_str, "on") == 0)
					dac_config[dac].flags |= DAC_FLAG_AMP10X;
				else if(strcmp(value_str, "off") == 0)
					dac_config[dac].flags &= ~DAC_FLAG_AMP10X;
				else
					goto einval;

				printf("OK\n");
			}
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
