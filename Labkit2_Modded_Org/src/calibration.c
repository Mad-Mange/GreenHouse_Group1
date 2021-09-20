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
#include <nvs_flash.h>

#include <string.h>

#include "errors.h"
#include "hci.h"

enum
{
	PARAMETER_ADC0_1X_0_2V = 0,
	PARAMETER_ADC0_1X_2V,
	PARAMETER_ADC1_1X_0_2V,
	PARAMETER_ADC1_1X_2V,
	PARAMETER_ADC0_10X_2V,
	PARAMETER_ADC0_10X_20V,
	PARAMETER_ADC1_10X_2V,
	PARAMETER_ADC1_10X_20V,
	PARAMETER_DAC0_1X_MIN,
	PARAMETER_DAC0_1X_MAX,
	PARAMETER_DAC0_10X_MIN,
	PARAMETER_DAC0_10X_80,
	PARAMETER_DAC1_1X_MIN,
	PARAMETER_DAC1_1X_MAX,
	PARAMETER_DAC1_10X_MIN,
	PARAMETER_DAC1_10X_80,

	PARAMETER_COUNT
};

const char * parameter_names[PARAMETER_COUNT] =
{
	"adc0_1x_0_2v",
	"adc0_1x_2v",
	"adc1_1x_0_2v",
	"adc1_1x_2v",
	"adc0_10x_2v",
	"adc0_10x_20v",
	"adc1_10x_2v",
	"adc1_10x_20v",
	"dac0_min_1x",
	"dac0_max_1x",
	"dac0_min_10x",
	"dac0_80_10x",
	"dac1_min_1x",
	"dac1_max_1x",
	"dac1_min_10x",
	"dac1_80_10x",
};

/*******************************************************************************
 *
 ******************************************************************************/
int read_parameter_value(const char *parameter, uint32_t *value)
{
	esp_err_t err;
	nvs_handle_t nvs_handle;
	int found = 0;

	for(int i = 0; i < PARAMETER_COUNT; i++)
	{
		if(strcmp(parameter_names[i], parameter) == 0)
		{
			found = 1;

			err = nvs_open("SWT21 Lab kit", NVS_READWRITE, &nvs_handle);
			if(err)
				goto open_err;

			err = nvs_get_u32(nvs_handle, parameter, value);
			if(err)
				goto get_err;

			nvs_close(nvs_handle);

			break;
		}
	}

	if(!found)
	{
		printf(ENOPARAM);
		return -1;
	}

	return 0;

get_err:
	nvs_close(nvs_handle);

open_err:
	return -1;

}

/*******************************************************************************
 *
 ******************************************************************************/
int write_parameter_value(const char *parameter, uint32_t value)
{
	esp_err_t err;
	nvs_handle_t nvs_handle;
	int found = 0;

	for(int i = 0; i < PARAMETER_COUNT; i++)
	{
		if(strcmp(parameter_names[i], parameter) == 0)
		{
			found = 1;

			err = nvs_open("SWT21 Lab kit", NVS_READWRITE, &nvs_handle);
			if(err)
				goto open_err;

			err = nvs_set_u32(nvs_handle, parameter, value);
			if(err)
				goto set_err;

			err = nvs_commit(nvs_handle);
			if(err)
				goto set_err;

			nvs_close(nvs_handle);

			break;
		}
	}

	if(!found)
	{
		printf(ENOPARAM);
		return -1;
	}

	return 0;

set_err:
	nvs_close(nvs_handle);

open_err:
	printf("ERR Could not read parameter\n");
	return -1;
}

/*******************************************************************************
 *
 ******************************************************************************/
void calibration_command()
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
			"calibration list - print out all calibration parameters\n"
			"calibration write <parameter> <value> - write value to parameter (u32)\n"
			"calibration read <parameter> - print out current parameter value (u32)\n");
	}
	else if(strcmp(cmd, "list") == 0)
	{
		printf("OK %d\n", PARAMETER_COUNT);

		for(int i = 0; i < PARAMETER_COUNT; i++)
		{
			const char *parameter = parameter_names[i];
			uint32_t value;

			if(read_parameter_value(parameter, &value) < 0)
				printf("%s\t<not set>\n", parameter);
			else
				printf("%s\t%u\n", parameter, value);
		}
	}
	else if(strcmp(cmd, "write") == 0)
	{
		char *parameter;
		char *value_str;
		uint32_t value;

		/* Read parameter argument */
		parameter = strtok(NULL, " ");
		if(!parameter)
			goto einval;

		/* Read value argument */
		value_str = strtok(NULL, " ");

		if(value_str)
			value = strtoul(value_str, NULL, 10);
		else
			goto einval;

		/* Send command */
		if(write_parameter_value(parameter, value) < 0)
			return;

		printf("OK\n");
	}
	else if(strcmp(cmd, "read") == 0)
	{
		char *parameter;

		/* Read period argument */
		parameter = strtok(NULL, " ");
		if(!parameter)
			goto einval;

		/* Read parameter value */
		uint32_t value;
		if(read_parameter_value(parameter, &value) < 0)
		{
			printf("ERR Could not read parameter\n");
			return;
		}

		printf("OK %d\n", value);
	}
	else
		goto einval;

	return;

einval:
	printf(EINVAL);
	return;
}
