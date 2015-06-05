/*
 * Synaptics DSX touchscreen driver
 *
 * Copyright (C) 2012 Synaptics Incorporated
 *
 * Copyright (C) 2012 Alexandra Chin <alexandra.chin@tw.synaptics.com>
 * Copyright (C) 2012 Scott Lin <scott.lin@tw.synaptics.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
//#ifdef KERNEL_ABOVE_2_6_38
#include <linux/input/mt.h>
//#endif
/*** ZTEMT Added by luochangyang, 2013/03/27 ***/
#ifdef CONFIG_OF
#include <linux/err.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#endif
/***ZTEMT END***/

#include "synaptics_dsx.h"
#include "synaptics_dsx_i2c.h"

#define SYNAPTICS_DRIVER_NAME       "synaptics_dsx_i2c"
#define INPUT_PHYS_NAME             "synaptics_dsx_i2c/input0"

#define RESET_DELAY 100

#ifdef KERNEL_ABOVE_2_6_38
#define TYPE_B_PROTOCOL
#endif

#define NO_0D_WHILE_2D
/*
#define REPORT_2D_Z
*/
#define REPORT_2D_W

#define RPT_TYPE (1 << 0)
#define RPT_X_LSB (1 << 1)
#define RPT_X_MSB (1 << 2)
#define RPT_Y_LSB (1 << 3)
#define RPT_Y_MSB (1 << 4)
#define RPT_Z (1 << 5)
#define RPT_WX (1 << 6)
#define RPT_WY (1 << 7)
#define RPT_DEFAULT (RPT_TYPE | RPT_X_LSB | RPT_X_MSB | RPT_Y_LSB | RPT_Y_MSB)

#define EXP_FN_DET_INTERVAL     1000 /* ms */
#define POLLING_PERIOD          1 /* ms */
#define SYN_I2C_RETRY_TIMES     1 /*10  Modify by luochangyang*/
#define MAX_ABS_MT_TOUCH_MAJOR  15

#define F01_STD_QUERY_LEN       21
#define F01_BUID_ID_OFFSET      18
#define F11_STD_QUERY_LEN       9
#define F11_STD_CTRL_LEN        10
#define F11_STD_DATA_LEN        12

#define NORMAL_OPERATION (0 << 0)
#define SENSOR_SLEEP (1 << 0)
#define NO_SLEEP_OFF (0 << 2)
#define NO_SLEEP_ON (1 << 2)
#define ZTEMT_SYNAPTICS_PALM_SLEEP  0   //Added by luochangyang, for palm sleep  2013/11/30

enum device_status {
	STATUS_NO_ERROR = 0x00,
	STATUS_RESET_OCCURRED = 0x01,
	STATUS_INVALID_CONFIG = 0x02,
	STATUS_DEVICE_FAILURE = 0x03,
	STATUS_CONFIG_CRC_FAILURE = 0x04,
	STATUS_FIRMWARE_CRC_FAILURE = 0x05,
	STATUS_CRC_IN_PROGRESS = 0x06
};

static int synaptics_rmi4_i2c_read(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data,
		unsigned short length);

static int synaptics_rmi4_i2c_write(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data,
		unsigned short length);

static int synaptics_rmi4_reset_device(struct synaptics_rmi4_data *rmi4_data);

static void synaptics_rmi4_swap_axis(struct synaptics_rmi4_data *rmi4_data);

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_FB)
static ssize_t synaptics_rmi4_full_pm_cycle_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_full_pm_cycle_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static int synaptics_rmi4_suspend(struct device *dev);

static int synaptics_rmi4_resume(struct device *dev);
#endif
#if defined(CONFIG_FB)
static int synaptics_rmi4_fb_notifier_callback(struct notifier_block *self,
				 unsigned long event, void *data);
#elif defined(CONFIG_HAS_EARLYSUSPEND)
static void synaptics_rmi4_early_suspend(struct early_suspend *h);

static void synaptics_rmi4_late_resume(struct early_suspend *h);
#endif

static ssize_t synaptics_rmi4_f01_reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t synaptics_rmi4_f01_productinfo_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_f01_buildid_show(struct device *dev,
		struct device_attribute *attr, char *buf);

/*** ZTEMT Added by luochangyang, 2013/11/19 ***/
static ssize_t synaptics_wakeup_gesture_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t synaptics_wakeup_gesture_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size);
/***ZTEMT END***/

static ssize_t synaptics_rmi4_f01_flashprog_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_0dbutton_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_0dbutton_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t synaptics_rmi4_flipx_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_flipx_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t synaptics_rmi4_flipy_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_flipy_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t synaptics_rmi4_swap_axes_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

struct synaptics_rmi4_f01_device_status {
	union {
		struct {
			unsigned char status_code:4;
			unsigned char reserved:2;
			unsigned char flash_prog:1;
			unsigned char unconfigured:1;
		} __packed;
		unsigned char data[1];
	};
};

struct synaptics_rmi4_f1a_query {
	union {
		struct {
			unsigned char max_button_count:3;
			unsigned char reserved:5;
			unsigned char has_general_control:1;
			unsigned char has_interrupt_enable:1;
			unsigned char has_multibutton_select:1;
			unsigned char has_tx_rx_map:1;
			unsigned char has_perbutton_threshold:1;
			unsigned char has_release_threshold:1;
			unsigned char has_strongestbtn_hysteresis:1;
			unsigned char has_filter_strength:1;
		} __packed;
		unsigned char data[2];
	};
};

struct synaptics_rmi4_f1a_control_0 {
	union {
		struct {
			unsigned char multibutton_report:2;
			unsigned char filter_mode:2;
			unsigned char reserved:4;
		} __packed;
		unsigned char data[1];
	};
};

struct synaptics_rmi4_f1a_control_3_4 {
	unsigned char transmitterbutton;
	unsigned char receiverbutton;
};

struct synaptics_rmi4_f1a_control {
	struct synaptics_rmi4_f1a_control_0 general_control;
	unsigned char *button_int_enable;
	unsigned char *multi_button;
	struct synaptics_rmi4_f1a_control_3_4 *electrode_map;
	unsigned char *button_threshold;
	unsigned char button_release_threshold;
	unsigned char strongest_button_hysteresis;
	unsigned char filter_strength;
};

struct synaptics_rmi4_f1a_handle {
	int button_bitmask_size;
	unsigned char button_count;
	unsigned char valid_button_count;
	unsigned char *button_data_buffer;
	unsigned char *button_map;
	struct synaptics_rmi4_f1a_query button_query;
	struct synaptics_rmi4_f1a_control button_control;
};

struct synaptics_rmi4_f54_query {
	union {
		struct {
			/* query 0 */
			unsigned char num_of_rx_electrodes;
			/* query 1 */
			unsigned char num_of_tx_electrodes;
			/* query 2 */
			unsigned char f54_query2_b0__1:2;
			unsigned char has_baseline:1;
			unsigned char has_image8:1;
			unsigned char f54_query2_b4__5:2;
			unsigned char has_image16:1;
			unsigned char f54_query2_b7:1;
			/* queries 3.0 and 3.1 */
			unsigned short clock_rate;
			/* query 4 */
			unsigned char touch_controller_family;
			/* query 5 */
			unsigned char has_pixel_touch_threshold_adjustment:1;
			unsigned char f54_query5_b1__7:7;
			/* query 6 */
			unsigned char has_sensor_assignment:1;
			unsigned char has_interference_metric:1;
			unsigned char has_sense_frequency_control:1;
			unsigned char has_firmware_noise_mitigation:1;
			unsigned char has_ctrl11:1;
			unsigned char has_two_byte_report_rate:1;
			unsigned char has_one_byte_report_rate:1;
			unsigned char has_relaxation_control:1;
		} __packed;
		unsigned char data[8];
	};
};

struct synaptics_rmi4_exp_fn {
	enum exp_fn fn_type;
	bool inserted;
	int (*func_init)(struct synaptics_rmi4_data *rmi4_data);
	void (*func_remove)(struct synaptics_rmi4_data *rmi4_data);
	void (*func_attn)(struct synaptics_rmi4_data *rmi4_data,
			unsigned char intr_mask);
	struct list_head link;
};

static struct device_attribute attrs[] = {
#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_FB)
	__ATTR(full_pm_cycle, (0660),
			synaptics_rmi4_full_pm_cycle_show,
			synaptics_rmi4_full_pm_cycle_store),
#endif
	__ATTR(reset, 0660,
			synaptics_rmi4_show_error,
			synaptics_rmi4_f01_reset_store),
	__ATTR(productinfo, 0660,
			synaptics_rmi4_f01_productinfo_show,
			synaptics_rmi4_store_error),
	__ATTR(buildid, 0664,
			synaptics_rmi4_f01_buildid_show,
			synaptics_rmi4_store_error),
    /*** ZTEMT Added by luochangyang, 2013/11/19 ***/
	__ATTR(wakeup_gesture, 0660,
			synaptics_wakeup_gesture_show,
			synaptics_wakeup_gesture_store),
    /***ZTEMT END***/
	__ATTR(flashprog, 0660,
			synaptics_rmi4_f01_flashprog_show,
			synaptics_rmi4_store_error),
	__ATTR(0dbutton, (0660),
			synaptics_rmi4_0dbutton_show,
			synaptics_rmi4_0dbutton_store),
	__ATTR(flipx, (0660),
			synaptics_rmi4_flipx_show,
			synaptics_rmi4_flipx_store),
	__ATTR(flipy, (0660),
			synaptics_rmi4_flipy_show,
			synaptics_rmi4_flipy_store),
	__ATTR(swapaxes, (0660),
			synaptics_rmi4_show_error,
			synaptics_rmi4_swap_axes_store),
};

static bool exp_fn_inited;
static struct mutex exp_fn_list_mutex;
static struct list_head exp_fn_list;

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_FB)
static ssize_t synaptics_rmi4_full_pm_cycle_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
			rmi4_data->full_pm_cycle);
}

static ssize_t synaptics_rmi4_full_pm_cycle_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;

	rmi4_data->full_pm_cycle = input > 0 ? 1 : 0;

	return count;
}
#endif

static ssize_t synaptics_rmi4_f01_reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int reset;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	if (sscanf(buf, "%u", &reset) != 1)
		return -EINVAL;

	if (reset != 1)
		return -EINVAL;

	retval = synaptics_rmi4_reset_device(rmi4_data);
	if (retval < 0) {
		dev_err(dev,
				"%s: Failed to issue reset command, error = %d\n",
				__func__, retval);
		return retval;
	}

	return count;
}

static ssize_t synaptics_rmi4_f01_productinfo_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "0x%02x 0x%02x\n",
			(rmi4_data->rmi4_mod_info.product_info[0]),
			(rmi4_data->rmi4_mod_info.product_info[1]));
}

static ssize_t synaptics_rmi4_f01_buildid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned int build_id;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	build_id = (unsigned int)rmi->build_id[0] +
			(unsigned int)rmi->build_id[1] * 0x100 +
			(unsigned int)rmi->build_id[2] * 0x10000;
    
    /*** ZTEMT Modify by luochangyang, 2013/05/02 ***/
	return snprintf(buf, PAGE_SIZE, "Build ID: %u\n"
	        "Config ID: 0x%02x 0x%02x 0x%02x 0x%02x\n", build_id, 
			(rmi4_data->rmi4_mod_info.config_id[0]),
			(rmi4_data->rmi4_mod_info.config_id[1]),
			(rmi4_data->rmi4_mod_info.config_id[2]),
			(rmi4_data->rmi4_mod_info.config_id[3]));
    /***ZTEMT END***/
}

/*** ZTEMT Added by luochangyang, 2013/11/19 ***/
static ssize_t synaptics_wakeup_gesture_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	
	return snprintf(buf, PAGE_SIZE, "0x%02X\n",	rmi4_data->wakeup_gesture);
}

static ssize_t synaptics_wakeup_gesture_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	unsigned long value;
	int ret;

	ret = kstrtoul(buf, 10, &value);
	if (ret < 0)
		return ret;

	if (value > 0xFF && value < 0)
		return -EINVAL;

	rmi4_data->wakeup_gesture = (u8)value;

	if (ret)
		return ret;

	return size;
}
/***ZTEMT END***/

static ssize_t synaptics_rmi4_f01_flashprog_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int retval;
	struct synaptics_rmi4_f01_device_status device_status;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_data_base_addr,
			device_status.data,
			sizeof(device_status.data));
	if (retval < 0) {
		dev_err(dev,
				"%s: Failed to read device status, error = %d\n",
				__func__, retval);
		return retval;
	}

	return snprintf(buf, PAGE_SIZE, "%u\n",
			device_status.flash_prog);
}

static ssize_t synaptics_rmi4_0dbutton_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
			rmi4_data->button_0d_enabled);
}

static ssize_t synaptics_rmi4_0dbutton_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int input;
	unsigned char ii;
	unsigned char intr_enable;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;

	input = input > 0 ? 1 : 0;

	if (rmi4_data->button_0d_enabled == input)
		return count;

	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->fn_number == SYNAPTICS_RMI4_F1A) {
				ii = fhandler->intr_reg_num;

				retval = synaptics_rmi4_i2c_read(rmi4_data,
						rmi4_data->f01_ctrl_base_addr +
						1 + ii,
						&intr_enable,
						sizeof(intr_enable));
				if (retval < 0)
					return retval;

				if (input == 1)
					intr_enable |= fhandler->intr_mask;
				else
					intr_enable &= ~fhandler->intr_mask;

				retval = synaptics_rmi4_i2c_write(rmi4_data,
						rmi4_data->f01_ctrl_base_addr +
						1 + ii,
						&intr_enable,
						sizeof(intr_enable));
				if (retval < 0)
					return retval;
			}
		}
	}

	rmi4_data->button_0d_enabled = input;

	return count;
}

static ssize_t synaptics_rmi4_flipx_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
			rmi4_data->flip_x);
}

static ssize_t synaptics_rmi4_flipx_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;

	rmi4_data->flip_x = input > 0 ? 1 : 0;

	return count;
}

static ssize_t synaptics_rmi4_flipy_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
			rmi4_data->flip_y);
}

static ssize_t synaptics_rmi4_flipy_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;

	rmi4_data->flip_y = input > 0 ? 1 : 0;

	return count;
}

static ssize_t synaptics_rmi4_swap_axes_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;

	if (input != 1)
		return -EINVAL;

	synaptics_rmi4_swap_axis(rmi4_data);

	return count;
}

static void synaptics_rmi4_swap_axis(struct synaptics_rmi4_data *rmi4_data)
{
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_device_info *rmi;
	struct i2c_client *client = rmi4_data->i2c_client;
	struct synaptics_rmi4_fn *sensor_tuning = NULL;

	rmi = &(rmi4_data->rmi4_mod_info);

	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			printk("function number 0x%x\n", fhandler->fn_number);
			if (fhandler->fn_number == SYNAPTICS_RMI4_F55)
				sensor_tuning = fhandler;
			if (sensor_tuning == NULL && 
				fhandler->fn_number == SYNAPTICS_RMI4_F54)
				sensor_tuning = fhandler;
		}
	}

	if (sensor_tuning != NULL) {
		int retval = 0;
		unsigned char val;
		unsigned short swap_ctrl_addr;
		unsigned short offset;

		if (sensor_tuning->fn_number == SYNAPTICS_RMI4_F55)
			swap_ctrl_addr = sensor_tuning->full_addr.ctrl_base + 0;
		else {
			struct synaptics_rmi4_f54_query f54_query;
			retval = synaptics_rmi4_i2c_read(rmi4_data,
				sensor_tuning->full_addr.query_base,
				(unsigned char *)f54_query.data,
				sizeof(f54_query.data));
			if (retval < 0)
				dev_err(&client->dev,
				"%s: Failed to read swap control registers\n",
				__func__);

			/* general ctrl 0 */
			offset = 1;
			/* ctrl 1/4/5/6/8.0/8.1/9*/
			if (f54_query.touch_controller_family == 0x00 ||
				f54_query.touch_controller_family == 0x01)
				offset += 7;
			/* ctrl 2/2.1 */
			offset += 2;
			/* ctrl 3 */
			if (f54_query.has_pixel_touch_threshold_adjustment)
				offset++;	
			/* ctrl 7*/
			if (f54_query.touch_controller_family == 0x01)
					offset += 1;
			/* ctrl 10 */
			if (f54_query.has_interference_metric)
				offset++;	
			/* ctrl 11/11.0 */
			if (f54_query.has_ctrl11)
				offset +=2;
			/* ctrl 12/13 */
			if (f54_query.has_relaxation_control)
				offset +=2;
			if (!f54_query.has_sensor_assignment)
				dev_err(&client->dev,
				"%s: Sensor assignment properties not exist\n",
				__func__);
			swap_ctrl_addr = sensor_tuning->full_addr.ctrl_base + offset;
		}
		retval = synaptics_rmi4_i2c_read(rmi4_data,
				swap_ctrl_addr,
				(unsigned char *)&val,
				sizeof(val));
		if (retval < 0)
			dev_err(&client->dev,
			"%s: Failed to read swap control registers\n",
			__func__);

		val = (val & 0xFE) | (!val & 0x01);

		dev_info(&client->dev,
			"swap value :0x%x, rmi address 0x%02X\n",
			val, swap_ctrl_addr);

		retval = synaptics_rmi4_i2c_write(rmi4_data,
				swap_ctrl_addr,
				(unsigned char *)&val,
				sizeof(val));
		if (retval < 0)
			dev_err(&client->dev,
			"%s: Failed to write swap control registers\n",
			__func__);
	}else
		dev_err(&client->dev,
			"%s: Firmware not support swap function\n",
			__func__);
}

 /**
 * synaptics_rmi4_set_page()
 *
 * Called by synaptics_rmi4_i2c_read() and synaptics_rmi4_i2c_write().
 *
 * This function writes to the page select register to switch to the
 * assigned page.
 */
static int synaptics_rmi4_set_page(struct synaptics_rmi4_data *rmi4_data,
		unsigned int address)
{
	int retval = 0;
	unsigned char retry;
	unsigned char buf[PAGE_SELECT_LEN];
	unsigned char page;
	struct i2c_client *i2c = rmi4_data->i2c_client;

	page = ((address >> 8) & MASK_8BIT);
	if (page != rmi4_data->current_page) {
		buf[0] = MASK_8BIT;
		buf[1] = page;
		for (retry = 0; retry < SYN_I2C_RETRY_TIMES; retry++) {
			retval = i2c_master_send(i2c, buf, PAGE_SELECT_LEN);
			if (retval != PAGE_SELECT_LEN) {
				dev_err(&i2c->dev,
						"%s: I2C retry %d\n",
						__func__, retry + 1);
				msleep(20);
			} else {
				rmi4_data->current_page = page;
				break;
			}
		}
	} else
		return PAGE_SELECT_LEN;
	return (retval == PAGE_SELECT_LEN) ? retval : -EIO;
}

 /**
 * synaptics_rmi4_i2c_read()
 *
 * Called by various functions in this driver, and also exported to
 * other expansion Function modules such as rmi_dev.
 *
 * This function reads data of an arbitrary length from the sensor,
 * starting from an assigned register address of the sensor, via I2C
 * with a retry mechanism.
 */
static int synaptics_rmi4_i2c_read(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data, unsigned short length)
{
	int retval;
	unsigned char retry;
	unsigned char buf;
	struct i2c_msg msg[] = {
		{
			.addr = rmi4_data->i2c_client->addr,
			.flags = 0,
			.len = 1,
			.buf = &buf,
		},
		{
			.addr = rmi4_data->i2c_client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = data,
		},
	};

	buf = addr & MASK_8BIT;

	mutex_lock(&(rmi4_data->rmi4_io_ctrl_mutex));

	retval = synaptics_rmi4_set_page(rmi4_data, addr);
	if (retval != PAGE_SELECT_LEN)
		goto exit;

	for (retry = 0; retry < SYN_I2C_RETRY_TIMES; retry++) {
		if (i2c_transfer(rmi4_data->i2c_client->adapter, msg, 2) == 2) {
			retval = length;
			break;
		}
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: I2C retry %d\n",
				__func__, retry + 1);
		msleep(20);
	}

	if (retry == SYN_I2C_RETRY_TIMES) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: I2C read over retry limit\n",
				__func__);
		retval = -EIO;
	}

exit:
	mutex_unlock(&(rmi4_data->rmi4_io_ctrl_mutex));

	return retval;
}

 /**
 * synaptics_rmi4_i2c_write()
 *
 * Called by various functions in this driver, and also exported to
 * other expansion Function modules such as rmi_dev.
 *
 * This function writes data of an arbitrary length to the sensor,
 * starting from an assigned register address of the sensor, via I2C with
 * a retry mechanism.
 */
static int synaptics_rmi4_i2c_write(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data, unsigned short length)
{
	int retval;
	unsigned char retry;
	unsigned char buf[length + 1];
	struct i2c_msg msg[] = {
		{
			.addr = rmi4_data->i2c_client->addr,
			.flags = 0,
			.len = length + 1,
			.buf = buf,
		}
	};

	mutex_lock(&(rmi4_data->rmi4_io_ctrl_mutex));

	retval = synaptics_rmi4_set_page(rmi4_data, addr);
	if (retval != PAGE_SELECT_LEN)
		goto exit;

	buf[0] = addr & MASK_8BIT;
	memcpy(&buf[1], &data[0], length);

	for (retry = 0; retry < SYN_I2C_RETRY_TIMES; retry++) {
		if (i2c_transfer(rmi4_data->i2c_client->adapter, msg, 1) == 1) {
			retval = length;
			break;
		}
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: I2C retry %d\n",
				__func__, retry + 1);
		msleep(20);
	}

	if (retry == SYN_I2C_RETRY_TIMES) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: I2C write over retry limit\n",
				__func__);
		retval = -EIO;
	}

exit:
	mutex_unlock(&(rmi4_data->rmi4_io_ctrl_mutex));

	return retval;
}

 /**
 * synaptics_rmi4_f11_abs_report()
 *
 * Called by synaptics_rmi4_report_touch() when valid Function $11
 * finger data has been detected.
 *
 * This function reads the Function $11 data registers, determines the
 * status of each finger supported by the Function, processes any
 * necessary coordinate manipulation, reports the finger data to
 * the input subsystem, and returns the number of fingers detected.
 */
static int synaptics_rmi4_f11_abs_report(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	unsigned char touch_count = 0; /* number of touch points */
	unsigned char reg_index;
	unsigned char finger;
	unsigned char fingers_supported;
	unsigned char num_of_finger_status_regs;
	unsigned char finger_shift;
	unsigned char finger_status;
	unsigned char data_reg_blk_size;
	unsigned char finger_status_reg[3];
	unsigned char data[F11_STD_DATA_LEN];
	unsigned short data_addr;
	unsigned short data_offset;
	int x;
	int y;
	int wx;
	int wy;
	int z;

	/*
	 * The number of finger status registers is determined by the
	 * maximum number of fingers supported - 2 bits per finger. So
	 * the number of finger status registers to read is:
	 * register_count = ceil(max_num_of_fingers / 4)
	 */
	fingers_supported = fhandler->num_of_data_points;
	num_of_finger_status_regs = (fingers_supported + 3) / 4;
	data_addr = fhandler->full_addr.data_base;
	data_reg_blk_size = fhandler->size_of_data_register_block;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			data_addr,
			finger_status_reg,
			num_of_finger_status_regs);
	if (retval < 0)
		return 0;

	for (finger = 0; finger < fingers_supported; finger++) {
		reg_index = finger / 4;
		finger_shift = (finger % 4) * 2;
		finger_status = (finger_status_reg[reg_index] >> finger_shift)
				& MASK_2BIT;

		/*
		 * Each 2-bit finger status field represents the following:
		 * 00 = finger not present
		 * 01 = finger present and data accurate
		 * 10 = finger present but data may be inaccurate
		 * 11 = reserved
		 */
#ifdef TYPE_B_PROTOCOL
		input_mt_slot(rmi4_data->input_dev, finger);
		input_mt_report_slot_state(rmi4_data->input_dev,
				MT_TOOL_FINGER, finger_status != 0);
#endif

		if (finger_status) {
			data_offset = data_addr +
					num_of_finger_status_regs +
					(finger * data_reg_blk_size);
			retval = synaptics_rmi4_i2c_read(rmi4_data,
					data_offset,
					data,
					data_reg_blk_size);
			if (retval < 0)
				return 0;

			x = (data[0] << 4) | (data[2] & MASK_4BIT);
			y = (data[1] << 4) | ((data[2] >> 4) & MASK_4BIT);
			wx = (data[3] & MASK_4BIT);
			wy = (data[3] >> 4) & MASK_4BIT;
			z = data[4];

			if (rmi4_data->flip_x)
				x = rmi4_data->sensor_max_x - x;
			if (rmi4_data->flip_y)
				y = rmi4_data->sensor_max_y - y;

			/*dev_dbg(&rmi4_data->i2c_client->dev,
					"%s: Finger %d:\n"
					"status = 0x%02x\n"
					"x = %d\n"
					"y = %d\n"
					"wx = %d\n"
					"wy = %d\n",
					__func__, finger,
					finger_status,
					x, y, wx, wy);*/

			input_report_key(rmi4_data->input_dev,
					BTN_TOUCH, 1);
			input_report_key(rmi4_data->input_dev,
					BTN_TOOL_FINGER, 1);
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_POSITION_X, x);
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_POSITION_Y, y);
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_PRESSURE, z);

#ifdef REPORT_2D_W
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_TOUCH_MAJOR, max(wx, wy));
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_TOUCH_MINOR, min(wx, wy));
#endif
#ifndef TYPE_B_PROTOCOL
			input_mt_sync(rmi4_data->input_dev);
#endif
			touch_count++;
		}
	}

#ifndef TYPE_B_PROTOCOL
	if (!touch_count)
		input_mt_sync(rmi4_data->input_dev);
#else
	/* sync after groups of events */
	#ifdef KERNEL_ABOVE_3_7
	input_mt_sync_frame(rmi4_data->input_dev);
	#endif
#endif

	input_sync(rmi4_data->input_dev);

	return touch_count;
}

static void synaptics_rmi4_f1a_report(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	unsigned char button;
	unsigned char index;
	unsigned char shift;
	unsigned char status;
	unsigned char *data;
	unsigned short data_addr = fhandler->full_addr.data_base;
	struct synaptics_rmi4_f1a_handle *f1a = fhandler->data;
	static unsigned char do_once = 1;
	static bool current_status[MAX_NUMBER_OF_BUTTONS];
#ifdef NO_0D_WHILE_2D
	static bool before_2d_status[MAX_NUMBER_OF_BUTTONS];
	static bool while_2d_status[MAX_NUMBER_OF_BUTTONS];
#endif

	if (do_once) {
		memset(current_status, 0, sizeof(current_status));
#ifdef NO_0D_WHILE_2D
		memset(before_2d_status, 0, sizeof(before_2d_status));
		memset(while_2d_status, 0, sizeof(while_2d_status));
#endif
		do_once = 0;
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			data_addr,
			f1a->button_data_buffer,
			f1a->button_bitmask_size);
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to read button data registers\n",
				__func__);
		return;
	}

	data = f1a->button_data_buffer;

	for (button = 0; button < f1a->valid_button_count; button++) {
		index = button / 8;
		shift = button % 8;
		status = ((data[index] >> shift) & MASK_1BIT);

		if (current_status[button] == status)
			continue;
		else
			current_status[button] = status;

		dev_dbg(&rmi4_data->i2c_client->dev,
				"%s: Button %d (code %d) ->%d\n",
				__func__, button,
				f1a->button_map[button],
				status);
#ifdef NO_0D_WHILE_2D
		if (rmi4_data->fingers_on_2d == false) {
			if (status == 1) {
				before_2d_status[button] = 1;
			} else {
				if (while_2d_status[button] == 1) {
					while_2d_status[button] = 0;
					continue;
				} else {
					before_2d_status[button] = 0;
				}
			}
			input_report_key(rmi4_data->input_dev,
					f1a->button_map[button],
					status);
		} else {
			if (before_2d_status[button] == 1) {
				before_2d_status[button] = 0;
				input_report_key(rmi4_data->input_dev,
						f1a->button_map[button],
						status);
			} else {
				if (status == 1)
					while_2d_status[button] = 1;
				else
					while_2d_status[button] = 0;
			}
		}
#else
		input_report_key(rmi4_data->input_dev,
				f1a->button_map[button],
				status);
#endif
	}

	input_sync(rmi4_data->input_dev);

	return;
}

 /**
 * synaptics_rmi4_report_touch()
 *
 * Called by synaptics_rmi4_sensor_report().
 *
 * This function calls the appropriate finger data reporting function
 * based on the function handler it receives and returns the number of
 * fingers detected.
 */
static void synaptics_rmi4_report_touch(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler,
		unsigned char *touch_count)
{
	unsigned char touch_count_2d;

	/*dev_dbg(&rmi4_data->i2c_client->dev,
			"%s: Function %02x reporting\n",
			__func__, fhandler->fn_number);*/

	switch (fhandler->fn_number) {
	case SYNAPTICS_RMI4_F11:
		touch_count_2d = synaptics_rmi4_f11_abs_report(rmi4_data,
				fhandler);

		*touch_count += touch_count_2d;

		if (touch_count_2d)
			rmi4_data->fingers_on_2d = true;
		else
			rmi4_data->fingers_on_2d = false;
		break;

	case SYNAPTICS_RMI4_F1A:
		synaptics_rmi4_f1a_report(rmi4_data, fhandler);
		break;

	default:
		break;
	}

	return;
}

 /**
 * synaptics_rmi4_sensor_report()
 *
 * Called by synaptics_rmi4_irq().
 *
 * This function determines the interrupt source(s) from the sensor
 * and calls synaptics_rmi4_report_touch() with the appropriate
 * function handler for each function with valid data inputs.
 */
static int synaptics_rmi4_sensor_report(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
    /*** ZTEMT Added by luochangyang, 2013/11/19 ***/
    #if ZTEMT_SYNAPTICS_PALM_SLEEP
    int t;
    unsigned char extended_status;
    #endif
    unsigned char report_mode;
    /***ZTEMT END***/
	unsigned char touch_count = 0;
	unsigned char intr[MAX_INTR_REGISTERS];
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_exp_fn *exp_fhandler;
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	/*
	 * Get interrupt status information from F01 Data1 register to
	 * determine the source(s) that are flagging the interrupt.
	 */
	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_data_base_addr + 1,
			intr,
			rmi4_data->num_of_intr_regs);
	if (retval < 0)
		return retval;

    /*** ZTEMT Added by luochangyang, 2013/11/19 ***/
    /*The function of palm to sleep*/
    #if ZTEMT_SYNAPTICS_PALM_SLEEP
    if (!rmi4_data->sensor_sleep) {
        retval = synaptics_rmi4_i2c_read(rmi4_data,
    				0x004A,
    				&extended_status,
    				sizeof(extended_status));
    	if (retval < 0) {
    		dev_err(&(rmi4_data->input_dev->dev),
    				"%s: Failed to read extended_status\n",
    				__func__);
    		return -ENODATA;
    	}
        
        if (extended_status & 0x02) {
            /* For large area event */
            input_mt_report_slot_state(rmi4_data->input_dev, MT_TOOL_FINGER, true);
            input_report_abs(rmi4_data->input_dev, ABS_MT_PRESSURE, 300);
            input_sync(rmi4_data->input_dev);

            /* Release all finger */
        	for (t = 0; t < 10; t++) {
        		input_mt_slot(rmi4_data->input_dev, t);
        		input_mt_report_slot_state(rmi4_data->input_dev, MT_TOOL_FINGER, false);
        	}
            input_sync(rmi4_data->input_dev);

            return 0;
        }
    }
    #endif
    if (rmi4_data->sensor_sleep && rmi4_data->wakeup_gesture) {
    	retval = synaptics_rmi4_i2c_read(rmi4_data,
    			rmi4_data->f01_ctrl_base_addr + 5,
    			&report_mode,
    			sizeof(report_mode));
    	if (retval < 0) {
    		dev_err(&(rmi4_data->input_dev->dev),
    				"%s: Failed to read report mode\n",
    				__func__);
    		return -ENODATA;
    	}

        dev_info(&rmi4_data->i2c_client->dev,
            "%s: report_mode = %d \n",
            __func__, report_mode);

        if (0x04 == (report_mode & 0x07)) {
            input_report_key(rmi4_data->input_dev, KEY_POWER, 1);
            input_sync(rmi4_data->input_dev);
            
            input_report_key(rmi4_data->input_dev, KEY_POWER, 0);
            input_sync(rmi4_data->input_dev);

            return 0;
        }
    }    
    /***ZTEMT END***/

	/*
	 * Traverse the function handler list and service the source(s)
	 * of the interrupt accordingly.
	 */
	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->num_of_data_sources) {
				if (fhandler->intr_mask &
						intr[fhandler->intr_reg_num]) {
					synaptics_rmi4_report_touch(rmi4_data,
							fhandler, &touch_count);
				}
			}
		}
	}

	mutex_lock(&exp_fn_list_mutex);
	if (!list_empty(&exp_fn_list)) {
		list_for_each_entry(exp_fhandler, &exp_fn_list, link) {
			if (exp_fhandler->inserted &&
					(exp_fhandler->func_attn != NULL))
				exp_fhandler->func_attn(rmi4_data, intr[0]);
		}
	}
	mutex_unlock(&exp_fn_list_mutex);

	return touch_count;
}

 /**
 * synaptics_rmi4_irq()
 *
 * Called by the kernel when an interrupt occurs (when the sensor
 * asserts the attention irq).
 *
 * This function is the ISR thread and handles the acquisition
 * and the reporting of finger data when the presence of fingers
 * is detected.
 */
static irqreturn_t synaptics_rmi4_irq(int irq, void *data)
{
	struct synaptics_rmi4_data *rmi4_data = data;
    
	synaptics_rmi4_sensor_report(rmi4_data);

	return IRQ_HANDLED;
}

 /**
 * synaptics_rmi4_irq_enable()
 *
 * Called by synaptics_rmi4_irq_acquire() and power management 
 * functions in this driver
 *
 * This function handles the enabling and disabling of the attention
 * irq including the setting up of the ISR thread.
 */
static int synaptics_rmi4_irq_enable(struct synaptics_rmi4_data *rmi4_data,
		bool enable)
{
	int retval = 0;
	unsigned char intr_status;

	if (enable) {
		/* Clear interrupts first */
		retval = synaptics_rmi4_i2c_read(rmi4_data,
				rmi4_data->f01_data_base_addr + 1,
				&intr_status,
				rmi4_data->num_of_intr_regs);
        
		if (retval < 0)
			return retval;

		enable_irq(rmi4_data->irq);
	} else
		disable_irq(rmi4_data->irq);

	return retval;
}

static irqreturn_t synaptics_hard_irq(int irq, void *handle)
{
	return IRQ_WAKE_THREAD;
}

 /**
 * synaptics_rmi4_irq_acquire()
 *
 * Called by synaptics_rmi4_probe()  in this driver and also exported 
 * to other expansion Function modules such as rmi_dev.
 *
 * This function handles the enabling and disabling of the attention
 * irq including the setting up of the ISR thread.
 */
static int synaptics_rmi4_irq_acquire(struct synaptics_rmi4_data *rmi4_data,
		bool enable)
{
	int retval = 0;
	const struct synaptics_dsx_platform_data *pdata = rmi4_data->board;
        unsigned char intr_status;

	if (enable) {
		/* Clear interrupts first */
		retval = synaptics_rmi4_i2c_read(rmi4_data,
				rmi4_data->f01_data_base_addr + 1,
				&intr_status,
				rmi4_data->num_of_intr_regs);
		if (retval < 0)
			return retval;

		//retval = request_threaded_irq(rmi4_data->irq, NULL,
		retval = request_threaded_irq(rmi4_data->irq, synaptics_hard_irq,
			synaptics_rmi4_irq, pdata->irq_flags,
			SYNAPTICS_DRIVER_NAME, rmi4_data);
		if (retval < 0) {
			dev_err(&rmi4_data->i2c_client->dev,
			"%s: Failed to request_threaded_irq\n",
			__func__);
			return retval;
		}
	} else
		free_irq(rmi4_data->irq, rmi4_data);
	return retval;
}
 
 /**
 * synaptics_rmi4_f11_init()
 *
 * Called by synaptics_rmi4_query_device().
 *
 * This funtion parses information from the Function 11 registers
 * and determines the number of fingers supported, x and y data ranges,
 * offset to the associated interrupt status register, interrupt bit
 * mask, and gathers finger data acquisition capabilities from the query
 * registers.
 */
static int synaptics_rmi4_f11_init(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler,
		struct synaptics_rmi4_fn_desc *fd,
		unsigned int intr_count)
{
	int retval;
	unsigned char ii;
	unsigned char intr_offset;
	unsigned char abs_data_size;
	unsigned char abs_data_blk_size;
	unsigned char query[F11_STD_QUERY_LEN];
	unsigned char control[F11_STD_CTRL_LEN];

	fhandler->fn_number = fd->fn_number;
	fhandler->num_of_data_sources = fd->intr_src_count;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base,
			query,
			sizeof(query));
	if (retval < 0)
		return retval;

	/* Maximum number of fingers supported */
	if ((query[1] & MASK_3BIT) <= 4)
		fhandler->num_of_data_points = (query[1] & MASK_3BIT) + 1;
	else if ((query[1] & MASK_3BIT) == 5)
		fhandler->num_of_data_points = 10;

	rmi4_data->num_of_fingers = fhandler->num_of_data_points;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.ctrl_base,
			control,
			sizeof(control));
	if (retval < 0)
		return retval;

	/* Maximum x and y */
	rmi4_data->sensor_max_x = ((control[6] & MASK_8BIT) << 0) |
			((control[7] & MASK_4BIT) << 8);
	rmi4_data->sensor_max_y = ((control[8] & MASK_8BIT) << 0) |
			((control[9] & MASK_4BIT) << 8);
	dev_dbg(&rmi4_data->i2c_client->dev,
			"%s: Function %02x max x = %d max y = %d\n",
			__func__, fhandler->fn_number,
			rmi4_data->sensor_max_x,
			rmi4_data->sensor_max_y);

	fhandler->intr_reg_num = (intr_count + 7) / 8;
	if (fhandler->intr_reg_num != 0)
		fhandler->intr_reg_num -= 1;

	/* Set an enable bit for each data source */
	intr_offset = intr_count % 8;
	fhandler->intr_mask = 0;
	for (ii = intr_offset;
			ii < ((fd->intr_src_count & MASK_3BIT) +
			intr_offset);
			ii++)
		fhandler->intr_mask |= 1 << ii;

	abs_data_size = query[5] & MASK_2BIT;
	abs_data_blk_size = 3 + (2 * (abs_data_size == 0 ? 1 : 0));
	fhandler->size_of_data_register_block = abs_data_blk_size;

	return retval;
}

static int synaptics_rmi4_f1a_alloc_mem(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	struct synaptics_rmi4_f1a_handle *f1a;

	f1a = kzalloc(sizeof(*f1a), GFP_KERNEL);
	if (!f1a) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to alloc mem for function handle\n",
				__func__);
		return -ENOMEM;
	}

	fhandler->data = (void *)f1a;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base,
			f1a->button_query.data,
			sizeof(f1a->button_query.data));
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to read query registers\n",
				__func__);
		return retval;
	}

	f1a->button_count = f1a->button_query.max_button_count + 1;
	f1a->button_bitmask_size = (f1a->button_count + 7) / 8;

	f1a->button_data_buffer = kcalloc(f1a->button_bitmask_size,
			sizeof(*(f1a->button_data_buffer)), GFP_KERNEL);
	if (!f1a->button_data_buffer) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to alloc mem for data buffer\n",
				__func__);
		return -ENOMEM;
	}

	f1a->button_map = kcalloc(f1a->button_count,
			sizeof(*(f1a->button_map)), GFP_KERNEL);
	if (!f1a->button_map) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to alloc mem for button map\n",
				__func__);
		return -ENOMEM;
	}

	return 0;
}

static int synaptics_rmi4_cap_button_map(
				struct synaptics_rmi4_data *rmi4_data,
				struct synaptics_rmi4_fn *fhandler)
{
	unsigned char ii;
	struct synaptics_rmi4_f1a_handle *f1a = fhandler->data;
	const struct synaptics_dsx_platform_data *pdata = rmi4_data->board;

	if (!pdata->cap_button_map) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: cap_button_map is" \
				"NULL in board file\n",
				__func__);
		return -ENODEV;
	} else if (!pdata->cap_button_map->map) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Button map is missing in board file\n",
				__func__);
		return -ENODEV;
	} else {
		if (pdata->cap_button_map->nbuttons !=
			f1a->button_count) {
			f1a->valid_button_count = min(f1a->button_count,
				pdata->cap_button_map->nbuttons);
		} else {
			f1a->valid_button_count = f1a->button_count;
		}

		for (ii = 0; ii < f1a->valid_button_count; ii++)
			f1a->button_map[ii] =
					pdata->cap_button_map->map[ii];
	}

	return 0;
}

static void synaptics_rmi4_f1a_kfree(struct synaptics_rmi4_fn *fhandler)
{
	struct synaptics_rmi4_f1a_handle *f1a = fhandler->data;

	if (f1a) {
		kfree(f1a->button_data_buffer);
		kfree(f1a->button_map);
		kfree(f1a);
		fhandler->data = NULL;
	}

	return;
}

static int synaptics_rmi4_f1a_init(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler,
		struct synaptics_rmi4_fn_desc *fd,
		unsigned int intr_count)
{
	int retval;
	unsigned char ii;
	unsigned short intr_offset;

	fhandler->fn_number = fd->fn_number;
	fhandler->num_of_data_sources = fd->intr_src_count;

	fhandler->intr_reg_num = (intr_count + 7) / 8;
	if (fhandler->intr_reg_num != 0)
		fhandler->intr_reg_num -= 1;

	/* Set an enable bit for each data source */
	intr_offset = intr_count % 8;
	fhandler->intr_mask = 0;
	for (ii = intr_offset;
			ii < ((fd->intr_src_count & MASK_3BIT) +
			intr_offset);
			ii++)
		fhandler->intr_mask |= 1 << ii;

	retval = synaptics_rmi4_f1a_alloc_mem(rmi4_data, fhandler);
	if (retval < 0)
		goto error_exit;

	retval = synaptics_rmi4_cap_button_map(rmi4_data, fhandler);
	if (retval < 0)
		goto error_exit;

	rmi4_data->button_0d_enabled = 1;

	return 0;

error_exit:
	synaptics_rmi4_f1a_kfree(fhandler);

	return retval;
}

static int synaptics_rmi4_alloc_fh(struct synaptics_rmi4_fn **fhandler,
		struct synaptics_rmi4_fn_desc *rmi_fd, int page_number)
{
	*fhandler = kzalloc(sizeof(**fhandler), GFP_KERNEL);
	if (!(*fhandler))
		return -ENOMEM;

	(*fhandler)->full_addr.data_base =
			(rmi_fd->data_base_addr |
			(page_number << 8));
	(*fhandler)->full_addr.ctrl_base =
			(rmi_fd->ctrl_base_addr |
			(page_number << 8));
	(*fhandler)->full_addr.cmd_base =
			(rmi_fd->cmd_base_addr |
			(page_number << 8));
	(*fhandler)->full_addr.query_base =
			(rmi_fd->query_base_addr |
			(page_number << 8));
	(*fhandler)->fn_number = rmi_fd->fn_number;

	return 0;
}


 /**
 * synaptics_rmi4_query_device_info()
 *
 * Called by synaptics_rmi4_query_device().
 *
 */
static int synaptics_rmi4_query_device_info(
					struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char f01_query[F01_STD_QUERY_LEN];
	struct synaptics_rmi4_device_info *rmi = &(rmi4_data->rmi4_mod_info);

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_query_base_addr,
			f01_query,
			sizeof(f01_query));
	if (retval < 0)
		return retval;

	/* RMI Version 4.0 currently supported */
	rmi->version_major = 4;
	rmi->version_minor = 0;

	rmi->manufacturer_id = f01_query[0];
	rmi->product_props = f01_query[1];
	rmi->product_info[0] = f01_query[2] & MASK_7BIT;
	rmi->product_info[1] = f01_query[3] & MASK_7BIT;
	rmi->date_code[0] = f01_query[4] & MASK_5BIT;
	rmi->date_code[1] = f01_query[5] & MASK_4BIT;
	rmi->date_code[2] = f01_query[6] & MASK_5BIT;
	rmi->tester_id = ((f01_query[7] & MASK_7BIT) << 8) |
			(f01_query[8] & MASK_7BIT);
	rmi->serial_number = ((f01_query[9] & MASK_7BIT) << 8) |
			(f01_query[10] & MASK_7BIT);
	memcpy(rmi->product_id_string, &f01_query[11], 10);

	if (rmi->manufacturer_id != 1) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Non-Synaptics device found, manufacturer ID = %d\n",
				__func__, rmi->manufacturer_id);
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_query_base_addr + F01_BUID_ID_OFFSET,
			rmi->build_id,
			sizeof(rmi->build_id));
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to read firmware build id (code %d)\n",
				__func__, retval);
		return retval;
	}
    
    /*** ZTEMT Added by luochangyang, 2013/05/02 ***/
    if (((unsigned int)rmi->build_id[0] +
			(unsigned int)rmi->build_id[1] * 0x100 +
			(unsigned int)rmi->build_id[2] * 0x10000) == (1562749)) {
        retval = synaptics_rmi4_i2c_read(rmi4_data, 0x56, rmi->config_id, sizeof(rmi->config_id));
    	if (retval < 0) {
    		dev_err(&rmi4_data->i2c_client->dev,
    				"%s: Failed to read firmware config id (code %d)\n",
    				__func__, retval);
    		return retval;
    	}
    } else {
        retval = synaptics_rmi4_i2c_read(rmi4_data, 0x59, rmi->config_id, sizeof(rmi->config_id));
    	if (retval < 0) {
    		dev_err(&rmi4_data->i2c_client->dev,
    				"%s: Failed to read firmware config id (code %d)\n",
    				__func__, retval);
    		return retval;
    	}
    }
    /***ZTEMT END***/
    
	return 0;
}

 /**
 * synaptics_rmi4_query_device()
 *
 * Called by synaptics_rmi4_probe().
 *
 * This funtion scans the page description table, records the offsets
 * to the register types of Function $01, sets up the function handlers
 * for Function $11 and Function $12, determines the number of interrupt
 * sources from the sensor, adds valid Functions with data inputs to the
 * Function linked list, parses information from the query registers of
 * Function $01, and enables the interrupt sources from the valid Functions
 * with data inputs.
 */
static int synaptics_rmi4_query_device(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char ii;
	unsigned char page_number;
	unsigned char intr_count = 0;
	unsigned char data_sources = 0;
	unsigned short pdt_entry_addr;
	unsigned short intr_addr;
	struct synaptics_rmi4_f01_device_status status;
	struct synaptics_rmi4_fn_desc rmi_fd;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	INIT_LIST_HEAD(&rmi->support_fn_list);

	/* Scan the page description tables of the pages to service */
	for (page_number = 0; page_number < PAGES_TO_SERVICE; page_number++) {
		for (pdt_entry_addr = PDT_START; pdt_entry_addr > PDT_END;
				pdt_entry_addr -= PDT_ENTRY_SIZE) {
			pdt_entry_addr |= (page_number << 8);

			retval = synaptics_rmi4_i2c_read(rmi4_data,
					pdt_entry_addr,
					(unsigned char *)&rmi_fd,
					sizeof(rmi_fd));
			if (retval < 0)
				return retval;

			fhandler = NULL;

			if (rmi_fd.fn_number == 0) {
				dev_dbg(&rmi4_data->i2c_client->dev,
						"%s: Reached end of PDT\n",
						__func__);
				break;
			}

			dev_dbg(&rmi4_data->i2c_client->dev,
					"%s: F%02x found (page %d)\n",
					__func__, rmi_fd.fn_number,
					page_number);

			switch (rmi_fd.fn_number) {
			case SYNAPTICS_RMI4_F01:
				rmi4_data->f01_query_base_addr =
						rmi_fd.query_base_addr;
				rmi4_data->f01_ctrl_base_addr =
						rmi_fd.ctrl_base_addr;
				rmi4_data->f01_data_base_addr =
						rmi_fd.data_base_addr;
				rmi4_data->f01_cmd_base_addr =
						rmi_fd.cmd_base_addr;

				retval =
				synaptics_rmi4_query_device_info(rmi4_data);
				if (retval < 0)
					return retval;

				retval = synaptics_rmi4_i2c_read(rmi4_data,
						rmi4_data->f01_data_base_addr,
						status.data,
						sizeof(status.data));
				if (retval < 0)
					return retval;

				while (status.status_code ==
						STATUS_CRC_IN_PROGRESS) {
					msleep(20);
					retval = synaptics_rmi4_i2c_read(
						rmi4_data,
						rmi4_data->f01_data_base_addr,
						status.data,
						sizeof(status.data));
					if (retval < 0)
						return retval;
				}

				if (status.flash_prog == 1) {
					pr_notice("%s: In flash prog mode, status = 0x%02x\n",
							__func__,
							status.status_code);
					goto flash_prog_mode;
				}
				break;

			case SYNAPTICS_RMI4_F11:
				if (rmi_fd.intr_src_count == 0)
					break;

				retval = synaptics_rmi4_alloc_fh(&fhandler,
						&rmi_fd, page_number);
				if (retval < 0) {
					dev_err(&rmi4_data->i2c_client->dev,
							"%s: Failed to alloc for F%d\n",
							__func__,
							rmi_fd.fn_number);
					return retval;
				}

				retval = synaptics_rmi4_f11_init(rmi4_data,
						fhandler, &rmi_fd, intr_count);
				if (retval < 0)
					return retval;
				break;

			case SYNAPTICS_RMI4_F1A:
				if (rmi_fd.intr_src_count == 0)
					break;

				retval = synaptics_rmi4_alloc_fh(&fhandler,
						&rmi_fd, page_number);
				if (retval < 0) {
					dev_err(&rmi4_data->i2c_client->dev,
							"%s: Failed to alloc for F%d\n",
							__func__,
							rmi_fd.fn_number);
					return retval;
				}

				retval = synaptics_rmi4_f1a_init(rmi4_data,
						fhandler, &rmi_fd, intr_count);
				if (retval < 0)
					return retval;
				break;

			case SYNAPTICS_RMI4_F54:
			case SYNAPTICS_RMI4_F55:
				if (rmi_fd.intr_src_count == 0)
					break;

				retval = synaptics_rmi4_alloc_fh(&fhandler,
						&rmi_fd, page_number);
				if (retval < 0) {
					dev_err(&rmi4_data->i2c_client->dev,
							"%s: Failed to alloc for F%d\n",
							__func__,
							rmi_fd.fn_number);
					return retval;
				}
				break;

			default:
				break;
			}

			/* Accumulate the interrupt count */
			intr_count += (rmi_fd.intr_src_count & MASK_3BIT);

			if (fhandler && rmi_fd.intr_src_count) {
				list_add_tail(&fhandler->link,
						&rmi->support_fn_list);
			}
		}
	}

flash_prog_mode:
	rmi4_data->num_of_intr_regs = (intr_count + 7) / 8;
	dev_dbg(&rmi4_data->i2c_client->dev,
			"%s: Number of interrupt registers = %d\n",
			__func__, rmi4_data->num_of_intr_regs);

	memset(rmi4_data->intr_mask, 0x00, sizeof(rmi4_data->intr_mask));

	/*
	 * Map out the interrupt bit masks for the interrupt sources
	 * from the registered function handlers.
	 */
	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link)
			data_sources += fhandler->num_of_data_sources;
	}
	if (data_sources) {
		if (!list_empty(&rmi->support_fn_list)) {
			list_for_each_entry(fhandler,
						&rmi->support_fn_list, link) {
				if (fhandler->num_of_data_sources) {
					rmi4_data->intr_mask[fhandler->intr_reg_num] |=
							fhandler->intr_mask;
				}
			}
		}
	}

	/* Enable the interrupt sources */
	for (ii = 0; ii < rmi4_data->num_of_intr_regs; ii++) {
		if (rmi4_data->intr_mask[ii] != 0x00) {
			dev_dbg(&rmi4_data->i2c_client->dev,
					"%s: Interrupt enable mask %d = 0x%02x\n",
					__func__, ii, rmi4_data->intr_mask[ii]);
			intr_addr = rmi4_data->f01_ctrl_base_addr + 1 + ii;
			retval = synaptics_rmi4_i2c_write(rmi4_data,
					intr_addr,
					&(rmi4_data->intr_mask[ii]),
					sizeof(rmi4_data->intr_mask[ii]));
			if (retval < 0)
				return retval;
		}
	}

	return 0;
}


static int synaptics_rmi4_reset_command(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	int page_number;
	unsigned char command = 0x01;
	unsigned short pdt_entry_addr;
	struct synaptics_rmi4_fn_desc rmi_fd;
	bool done = false;
	
	/* Scan the page description tables of the pages to service */
	for (page_number = 0; page_number < PAGES_TO_SERVICE; page_number++) {
		for (pdt_entry_addr = PDT_START; pdt_entry_addr > PDT_END;
				pdt_entry_addr -= PDT_ENTRY_SIZE) {
			pdt_entry_addr |= (page_number << 8);

			retval = synaptics_rmi4_i2c_read(rmi4_data,
					pdt_entry_addr,
					(unsigned char *)&rmi_fd,
					sizeof(rmi_fd));
			if (retval < 0)
				return retval;

			if (rmi_fd.fn_number == 0)
				break;

			switch (rmi_fd.fn_number) {
			case SYNAPTICS_RMI4_F01:
				rmi4_data->f01_cmd_base_addr =
						rmi_fd.cmd_base_addr;
				done = true;
				break;
			}
		}
		if (done) {
			dev_info(&rmi4_data->i2c_client->dev,
				"%s: Find F01 in page description table 0x%x\n",
				__func__, rmi4_data->f01_cmd_base_addr);
			break;
		}
	}


	if (!done) {
		dev_err(&rmi4_data->i2c_client->dev,
			"%s: Cannot find F01 in page description table\n",
			__func__);
		return -EINVAL;;
	}

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f01_cmd_base_addr,
			&command,
			sizeof(command));
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to issue reset command, error = %d\n",
				__func__, retval);
		return retval;
	}

	msleep(RESET_DELAY);
	return retval;
}

static int synaptics_rmi4_reset_device(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);


	retval = synaptics_rmi4_reset_command(rmi4_data);
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to send command reset\n",
				__func__);
		return retval;
	}

	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->fn_number == SYNAPTICS_RMI4_F1A)
				synaptics_rmi4_f1a_kfree(fhandler);
			else
				kfree(fhandler->data);
			kfree(fhandler);
		}
	}

	retval = synaptics_rmi4_query_device(rmi4_data);
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to query device\n",
				__func__);
		return retval;
	}

	return 0;
}

/**
* synaptics_rmi4_detection_work()
*
* Called by the kernel at the scheduled time.
*
* This function is a self-rearming work thread that checks for the
* insertion and removal of other expansion Function modules such as
* rmi_dev and calls their initialization and removal callback functions
* accordingly.
*/
static void synaptics_rmi4_detection_work(struct work_struct *work)
{
	struct synaptics_rmi4_exp_fn *exp_fhandler, *next_list_entry;
	struct synaptics_rmi4_data *rmi4_data =
			container_of(work, struct synaptics_rmi4_data,
			det_work.work);

	mutex_lock(&exp_fn_list_mutex);
	if (!list_empty(&exp_fn_list)) {
		list_for_each_entry_safe(exp_fhandler,
				next_list_entry,
				&exp_fn_list,
				link) {
			if ((exp_fhandler->func_init != NULL) &&
					(exp_fhandler->inserted == false)) {
				exp_fhandler->func_init(rmi4_data);
				exp_fhandler->inserted = true;
			} else if ((exp_fhandler->func_init == NULL) &&
					(exp_fhandler->inserted == true)) {
				exp_fhandler->func_remove(rmi4_data);
				list_del(&exp_fhandler->link);
				kfree(exp_fhandler);
			}
		}
	}
	mutex_unlock(&exp_fn_list_mutex);

	return;
}

/**
* synaptics_rmi4_new_function()
*
* Called by other expansion Function modules in their module init and
* module exit functions.
*
* This function is used by other expansion Function modules such as
* rmi_dev to register themselves with the driver by providing their
* initialization and removal callback function pointers so that they
* can be inserted or removed dynamically at module init and exit times,
* respectively.
*/
void synaptics_rmi4_new_function(enum exp_fn fn_type, bool insert,
		int (*func_init)(struct synaptics_rmi4_data *rmi4_data),
		void (*func_remove)(struct synaptics_rmi4_data *rmi4_data),
		void (*func_attn)(struct synaptics_rmi4_data *rmi4_data,
		unsigned char intr_mask))
{
	struct synaptics_rmi4_exp_fn *exp_fhandler;

	if (!exp_fn_inited) {
		mutex_init(&exp_fn_list_mutex);
		INIT_LIST_HEAD(&exp_fn_list);
		exp_fn_inited = 1;
	}

	mutex_lock(&exp_fn_list_mutex);
	if (insert) {
		exp_fhandler = kzalloc(sizeof(*exp_fhandler), GFP_KERNEL);
		if (!exp_fhandler) {
			pr_err("%s: Failed to alloc mem for expansion function\n",
					__func__);
			goto exit;
		}
		exp_fhandler->fn_type = fn_type;
		exp_fhandler->func_init = func_init;
		exp_fhandler->func_attn = func_attn;
		exp_fhandler->func_remove = func_remove;
		exp_fhandler->inserted = false;
		list_add_tail(&exp_fhandler->link, &exp_fn_list);
	} else {
		if (!list_empty(&exp_fn_list)) {
			list_for_each_entry(exp_fhandler, &exp_fn_list, link) {
				if (exp_fhandler->func_init == func_init) {
					exp_fhandler->inserted = false;
					exp_fhandler->func_init = NULL;
					exp_fhandler->func_attn = NULL;
					goto exit;
				}
			}
		}
	}

exit:
	mutex_unlock(&exp_fn_list_mutex);

	return;
}
EXPORT_SYMBOL(synaptics_rmi4_new_function);

/*** ZTEMT Added by luochangyang, 2013/11/28 ***/
#ifdef CONFIG_OF
#if 0
static int reg_set_optimum_mode_check(struct regulator *reg, int load_uA)
{
	return (regulator_count_voltages(reg) > 0) ?
		regulator_set_optimum_mode(reg, load_uA) : 0;
}

static int synaptics_power_on(struct device *dev)
{
	int rc;
    static struct regulator *vcc_ana;
    static struct regulator *vcc_i2c;

	vcc_ana = regulator_get(dev, "vdd_ana");
	if (IS_ERR(vcc_ana))
    {
		rc = PTR_ERR(vcc_ana);
		dev_err(dev, "Regulator get failed vcc_ana rc=%d\n", rc);
		return rc;
	}

	if (regulator_count_voltages(vcc_ana) > 0)
    {
		rc = regulator_set_voltage(vcc_ana, 2850000, 2850000);
		if (rc)
        {
			dev_err(dev, "Regulator set ana vtg failed rc=%d\n", rc);
			goto error_set_vtg_vcc_ana;
		}
	}
    
    rc = reg_set_optimum_mode_check(vcc_ana, 15000);
    if (rc < 0)
    {
        dev_err(dev, "Regulator vcc_ana set_opt failed rc=%d\n", rc);
        return rc;
    }
    
    rc = regulator_enable(vcc_ana);
    if (rc)
    {
        dev_err(dev, "Regulator vcc_ana enable failed rc=%d\n", rc);
        goto error_reg_en_vcc_ana;
    }

	vcc_i2c = regulator_get(dev, "vcc_i2c");
	if (IS_ERR(vcc_i2c))
    {
		rc = PTR_ERR(vcc_i2c);
		dev_err(dev, "Regulator get failed rc=%d\n", rc);
		goto error_reg_opt_vcc_dig;
	}

    rc = regulator_enable(vcc_i2c);
    if (rc)
    {
        dev_err(dev, "Regulator vcc_i2c enable failed rc=%d\n", rc);
        goto error_reg_en_vcc_i2c;
    }

    msleep(100);
    
    return 0;

error_reg_en_vcc_i2c:
    reg_set_optimum_mode_check(vcc_i2c, 0);
error_reg_opt_vcc_dig:
    regulator_disable(vcc_ana);

error_reg_en_vcc_ana:
    reg_set_optimum_mode_check(vcc_ana, 0);
error_set_vtg_vcc_ana:
	regulator_put(vcc_ana);
	return rc;
}
#endif
static int synaptics_gpio_setup(unsigned gpio, bool configure)
{
	int retval = 0;

	if (configure) {
		retval = gpio_request(gpio, "rmi4_attn");
		if (retval) {
			pr_err("%s: Failed to get attn gpio %d (code: %d)",
					 __func__, gpio, retval);
			return retval;
		}

		retval = gpio_direction_input(gpio);
		if (retval) {
			pr_err("%s: Failed to setup attn gpio %d (code: %d)",
					__func__, gpio, retval);
			gpio_free(gpio);
		}
	} else {
		pr_warn("%s: No way to deconfigure gpio %d",
				__func__, gpio);
	}

	return retval;
}
static unsigned char TM_f1a_button_codes[] = {KEY_MENU, KEY_MENU, KEY_HOME, KEY_BACK, KEY_BACK};

static struct synaptics_dsx_cap_button_map TM_cap_button_map = {
	.nbuttons = ARRAY_SIZE(TM_f1a_button_codes),
	.map = TM_f1a_button_codes,
};

static int synaptics_parse_dt(struct device *dev, 
        struct synaptics_dsx_platform_data *pdata)
{
	struct device_node *np = dev->of_node;

	/* reset, irq gpio info */
	pdata->reset_gpio = of_get_named_gpio(np, "synaptics,reset-gpio", 0);
	pdata->irq_gpio = of_get_named_gpio(np, "synaptics,irq-gpio", 0);
    pdata->irq_flags = IRQF_TRIGGER_FALLING;

    pdata->regulator_en = of_property_read_bool(np, "synaptics,regulator_en");
    pdata->gpio_config = synaptics_gpio_setup;
    pdata->cap_button_map = &TM_cap_button_map;
    
	return 0;
}
#else
static int synaptics_parse_dt(struct device *dev, 
        struct synaptics_dsx_platform_data *pdata;)
{
	return -ENODEV;
}
#endif
/***ZTEMT END***/

#define SYNAPTICS_PINCTRL_STATE_SLEEP    "synaptics_int_sleep"
#define SYNAPTICS_PINCTRL_STATE_DEFAULT  "synaptics_int_default"

struct synaptics_pinctrl_info {
	struct pinctrl *pinctrl;
	struct pinctrl_state *gpio_state_active;
	struct pinctrl_state *gpio_state_suspend;
};

static struct synaptics_pinctrl_info synaptics_pctrl;
static int synaptics_pinctrl_init(struct device *dev)
{
	synaptics_pctrl.pinctrl = devm_pinctrl_get(dev);

	if (IS_ERR_OR_NULL(synaptics_pctrl.pinctrl)) {
		pr_err("%s:%d Getting pinctrl handle failed\n",
			__func__, __LINE__);
		return -EINVAL;
	}
	synaptics_pctrl.gpio_state_active = pinctrl_lookup_state(
					       synaptics_pctrl.pinctrl,
					       SYNAPTICS_PINCTRL_STATE_DEFAULT);

	if (IS_ERR_OR_NULL(synaptics_pctrl.gpio_state_active)) {
		pr_err("%s:%d Failed to get the active state pinctrl handle\n",
			__func__, __LINE__);
		return -EINVAL;
	}
	synaptics_pctrl.gpio_state_suspend = pinctrl_lookup_state(
						synaptics_pctrl.pinctrl,
						SYNAPTICS_PINCTRL_STATE_SLEEP);

	if (IS_ERR_OR_NULL(synaptics_pctrl.gpio_state_suspend)) {
		pr_err("%s:%d Failed to get the suspend state pinctrl handle\n",
				__func__, __LINE__);
		return -EINVAL;
	}
	return 0;
}

 /**
 * synaptics_rmi4_probe()
 *
 * Called by the kernel when an association with an I2C device of the
 * same name is made (after doing i2c_add_driver).
 *
 * This funtion allocates and initializes the resources for the driver
 * as an input driver, turns on the power to the sensor, queries the
 * sensor for its supported Functions and characteristics, registers
 * the driver to the input subsystem, sets up the interrupt, handles
 * the registration of the early_suspend and late_resume functions,
 * and creates a work queue for detection of other expansion Function
 * modules.
 */
static int synaptics_rmi4_probe(struct i2c_client *client,
		const struct i2c_device_id *dev_id)
{
	int retval;
	unsigned char ii;
	unsigned char attr_count;
	struct synaptics_rmi4_f1a_handle *f1a;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_data *rmi4_data;
	struct synaptics_rmi4_device_info *rmi;
	struct synaptics_dsx_platform_data *platform_data;

	if (!i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev,
				"%s: SMBus byte data not supported\n",
				__func__);
		return -EIO;
	}

    /*** ZTEMT Added by luochangyang, 2013/11/28 ***/
    if (client->dev.of_node) {
        platform_data = devm_kzalloc(&client->dev,
            sizeof(struct synaptics_dsx_platform_data), GFP_KERNEL);
        if (!platform_data) {
            dev_err(&client->dev, "Failed to allocate memory\n");
            return -ENOMEM;
        }

        retval = synaptics_parse_dt(&client->dev, platform_data);
        if (retval)
            return retval;
    } else
        platform_data = client->dev.platform_data;
    /***ZTEMT END***/

	if (!platform_data) {
		dev_err(&client->dev,
				"%s: No platform data found\n",
				__func__);
		return -EINVAL;
	}

	synaptics_pinctrl_init(&client->dev);
	retval = pinctrl_select_state(synaptics_pctrl.pinctrl,
			synaptics_pctrl.gpio_state_active);
	if (retval)
		dev_err(&client->dev, "%s:%d cannot set pin to active state",
			__func__, __LINE__);

	rmi4_data = kzalloc(sizeof(*rmi4_data) * 2, GFP_KERNEL);
	if (!rmi4_data) {
		dev_err(&client->dev,
				"%s: Failed to alloc mem for rmi4_data\n",
				__func__);
		return -ENOMEM;
	}

	rmi = &(rmi4_data->rmi4_mod_info);

	rmi4_data->input_dev = input_allocate_device();
	if (rmi4_data->input_dev == NULL) {
		dev_err(&client->dev,
				"%s: Failed to allocate input device\n",
				__func__);
		retval = -ENOMEM;
		goto err_input_device;
	}

	if (platform_data->regulator_en) {
        /*** ZTEMT Modify by luochangyang, 2013/09/03 ***/
        #ifdef CONFIG_OF
        //synaptics_power_on(&client->dev);
        
    	rmi4_data->regulator_vcc = regulator_get(&client->dev, "vcc_i2c");
    	if (IS_ERR(rmi4_data->regulator_vcc))
        {
    		retval = PTR_ERR(rmi4_data->regulator_vcc);
    		dev_err(&client->dev, "Regulator get failed rc=%d\n", retval);
    		goto err_regulator;
    	}

        retval = regulator_enable(rmi4_data->regulator_vcc);
        if (retval)
        {
            dev_err(&client->dev, "Regulator vcc_i2c enable failed rc=%d\n", retval);
            goto err_regulator;
        }
        
	    rmi4_data->regulator = regulator_get(&client->dev, "vdd_ana");
        #else
    	//rmi4_data->regulator = regulator_get(&client->dev, "pm8226_l19");
		rmi4_data->regulator = regulator_get(&client->dev, "pm8916_l17");
        #endif
        /***ZTEMT END***/
        
    	if (IS_ERR(rmi4_data->regulator)) {
    		dev_err(&client->dev,
    				"%s: Failed to get regulator\n",
    				__func__);
    		retval = PTR_ERR(rmi4_data->regulator);
    		goto err_regulator;
    	}
        
    	retval = regulator_set_voltage(rmi4_data->regulator, 2850000, 2850000);
    	if (retval) {
    		pr_err("set_voltage rmi4_data->regulator failed, rc=%d\n", retval);
    		goto err_regulator;
    	}

    	retval = regulator_enable(rmi4_data->regulator);
		if (retval) {
		    dev_err(&client->dev, "regulator vdd_ana enable failed rc=%d\n", retval);
		    goto err_regulator;
	    }
	}

	rmi4_data->i2c_client = client;
	rmi4_data->current_page = MASK_8BIT;
	rmi4_data->board = platform_data;
	rmi4_data->touch_stopped = false;
	rmi4_data->sensor_sleep = false;

	rmi4_data->i2c_read = synaptics_rmi4_i2c_read;
	rmi4_data->i2c_write = synaptics_rmi4_i2c_write;
	rmi4_data->irq_enable = synaptics_rmi4_irq_acquire;
	rmi4_data->reset_device = synaptics_rmi4_reset_device;

	rmi4_data->flip_x = rmi4_data->board->x_flip;
	rmi4_data->flip_y = rmi4_data->board->y_flip;
	rmi4_data->swap_axes = false;
    /*** ZTEMT Added by luochangyang, 2013/11/19 ***/
    rmi4_data->wakeup_gesture = false;
    /***ZTEMT END***/

	init_waitqueue_head(&rmi4_data->wait);
	mutex_init(&(rmi4_data->rmi4_io_ctrl_mutex));

	if (platform_data->reset_gpio) {
		if (gpio_is_valid(platform_data->reset_gpio)) {
			retval = gpio_request(platform_data->reset_gpio, "touch_reset");
			if (retval) {
				dev_err(&client->dev,
					"%s: Failed to request touch_reset GPIO, rc=%d\n",
					__func__, retval);
				goto err_request_reset_gpio;
			}		
			retval = gpio_direction_output(platform_data->reset_gpio, 1);
			if (retval) {
				dev_err(&client->dev,
					"%s: Failed to set reset GPIO direction, rc=%d\n",
					__func__, retval);
				goto err_reset;
			}
			gpio_set_value(platform_data->reset_gpio, 0);
			msleep(10);
			gpio_set_value(platform_data->reset_gpio, 1);
			msleep(RESET_DELAY);
		} 
	} else {
		synaptics_rmi4_reset_command(rmi4_data);
	}

	retval = synaptics_rmi4_query_device(rmi4_data);
	if (retval < 0) {
		dev_err(&client->dev,
				"%s: Failed to query device\n",
				__func__);
		goto err_query_device;
	}

	if (rmi4_data->swap_axes)
		synaptics_rmi4_swap_axis(rmi4_data);

	i2c_set_clientdata(client, rmi4_data);

	rmi4_data->input_dev->name = SYNAPTICS_DRIVER_NAME;
	rmi4_data->input_dev->phys = INPUT_PHYS_NAME;
	rmi4_data->input_dev->id.bustype = BUS_I2C;
	rmi4_data->input_dev->id.product = SYNAPTICS_DSX_DRIVER_PRODUCT;
	rmi4_data->input_dev->id.version = SYNAPTICS_DSX_DRIVER_VERSION;
	rmi4_data->input_dev->dev.parent = &client->dev;
	input_set_drvdata(rmi4_data->input_dev, rmi4_data);

	set_bit(EV_SYN, rmi4_data->input_dev->evbit);
	set_bit(EV_KEY, rmi4_data->input_dev->evbit);
	set_bit(EV_ABS, rmi4_data->input_dev->evbit);
	set_bit(BTN_TOUCH, rmi4_data->input_dev->keybit);
	set_bit(BTN_TOOL_FINGER, rmi4_data->input_dev->keybit);

#ifdef INPUT_PROP_DIRECT
	set_bit(INPUT_PROP_DIRECT, rmi4_data->input_dev->propbit);
#endif

    /*** ZTEMT Added by luochangyang, 2013/11/19 ***/
    __set_bit(KEY_POWER, rmi4_data->input_dev->keybit);
//    __set_bit(KEY_A, rmi4_data->input_dev->keybit);
    /***ZTEMT END***/

	input_set_abs_params(rmi4_data->input_dev, ABS_X,
			     0, rmi4_data->sensor_max_x, 0, 0);
	input_set_abs_params(rmi4_data->input_dev, ABS_Y,
			     0, rmi4_data->sensor_max_y, 0, 0);
	input_set_abs_params(rmi4_data->input_dev, ABS_PRESSURE,
			     0, 255, 0, 0);

	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_POSITION_X, 0,
			rmi4_data->sensor_max_x, 0, 0);
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_POSITION_Y, 0,
			rmi4_data->sensor_max_y, 0, 0);
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_PRESSURE, 0, 255, 0, 0);
#ifdef REPORT_2D_W
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_TOUCH_MAJOR, 0,
			MAX_ABS_MT_TOUCH_MAJOR, 0, 0);
#endif

#ifdef TYPE_B_PROTOCOL
#ifdef KERNEL_ABOVE_3_7
	/* input_mt_init_slots now has a "flags" parameter */
	input_mt_init_slots(rmi4_data->input_dev,
			rmi4_data->num_of_fingers, INPUT_MT_DIRECT);
#else
	input_mt_init_slots(rmi4_data->input_dev,
			rmi4_data->num_of_fingers);
#endif
#endif

	f1a = NULL;
	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->fn_number == SYNAPTICS_RMI4_F1A)
				f1a = fhandler->data;
		}
	}

	if (f1a) {
		for (ii = 0; ii < f1a->valid_button_count; ii++) {
			set_bit(f1a->button_map[ii],
					rmi4_data->input_dev->keybit);
			input_set_capability(rmi4_data->input_dev,
					EV_KEY, f1a->button_map[ii]);
		}
	}

	retval = input_register_device(rmi4_data->input_dev);
	if (retval) {
		dev_err(&client->dev,
				"%s: Failed to register input device\n",
				__func__);
		goto err_register_input;
	}
    
    /* ZTEMT Added by luochangyang, 2013/11/29 */
#if defined(CONFIG_FB)
    rmi4_data->fb_notif.notifier_call = synaptics_rmi4_fb_notifier_callback;

    retval = fb_register_client(&rmi4_data->fb_notif);
    if (retval) {
        dev_err(&client->dev, "%s:Unable to register fb_notifier: %d\n", __func__, retval);
    }
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	rmi4_data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	rmi4_data->early_suspend.suspend = synaptics_rmi4_early_suspend;
	rmi4_data->early_suspend.resume = synaptics_rmi4_late_resume;
	register_early_suspend(&rmi4_data->early_suspend);
#endif

	if (!exp_fn_inited) {
		mutex_init(&exp_fn_list_mutex);
		INIT_LIST_HEAD(&exp_fn_list);
		exp_fn_inited = 1;
	}

	rmi4_data->det_workqueue =
			create_singlethread_workqueue("rmi_det_workqueue");
	INIT_DELAYED_WORK(&rmi4_data->det_work,
			synaptics_rmi4_detection_work);
	queue_delayed_work(rmi4_data->det_workqueue,
			&rmi4_data->det_work,
			msecs_to_jiffies(EXP_FN_DET_INTERVAL));

	if (platform_data->gpio_config) {
		retval = platform_data->gpio_config(platform_data->irq_gpio,
							true);
		if (retval < 0) {
			dev_err(&client->dev,
					"%s: Failed to configure GPIO\n",
					__func__);
			goto err_gpio;
		}
	}

	rmi4_data->irq = gpio_to_irq(platform_data->irq_gpio);
	retval = synaptics_rmi4_irq_acquire(rmi4_data, true);
	if (retval < 0) {
		dev_err(&client->dev,
				"%s: Failed to acquire irq\n",
				__func__);
		goto err_enable_irq;
	}

	for (attr_count = 0; attr_count < ARRAY_SIZE(attrs); attr_count++) {
		retval = sysfs_create_file(&rmi4_data->input_dev->dev.kobj,
				&attrs[attr_count].attr);
		if (retval < 0) {
			dev_err(&client->dev,
					"%s: Failed to create sysfs attributes\n",
					__func__);
			goto err_sysfs;
		}
	}
    
    /*** ZTEMT Added by luochangyang, 2013/11/21 ***/
	device_init_wakeup(&client->dev, 1);
    /***ZTEMT END***/
    
	return retval;

err_sysfs:
	for (attr_count--; attr_count >= 0; attr_count--) {
		sysfs_remove_file(&rmi4_data->input_dev->dev.kobj,
				&attrs[attr_count].attr);
	}

err_enable_irq:
err_gpio:
	input_unregister_device(rmi4_data->input_dev);

err_register_input:
err_query_device:
	if (platform_data->regulator_en) {
		regulator_disable(rmi4_data->regulator);
		regulator_put(rmi4_data->regulator);
	}

	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->fn_number == SYNAPTICS_RMI4_F1A)
				synaptics_rmi4_f1a_kfree(fhandler);
			else
				kfree(fhandler->data);
			kfree(fhandler);
		}
	}
err_request_reset_gpio:	
err_reset:
	if (gpio_is_valid(platform_data->reset_gpio))
		gpio_free(platform_data->reset_gpio);
err_regulator:
	input_free_device(rmi4_data->input_dev);
	rmi4_data->input_dev = NULL;
err_input_device:
	kfree(rmi4_data);

	return retval;
}

 /**
 * synaptics_rmi4_remove()
 *
 * Called by the kernel when the association with an I2C device of the
 * same name is broken (when the driver is unloaded).
 *
 * This funtion terminates the work queue, stops sensor data acquisition,
 * frees the interrupt, unregisters the driver from the input subsystem,
 * turns off the power to the sensor, and frees other allocated resources.
 */
static int synaptics_rmi4_remove(struct i2c_client *client)
{
	unsigned char attr_count;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_data *rmi4_data = i2c_get_clientdata(client);
	struct synaptics_rmi4_device_info *rmi;
	const struct synaptics_dsx_platform_data *platform_data =
			rmi4_data->board;

	rmi = &(rmi4_data->rmi4_mod_info);

	cancel_delayed_work_sync(&rmi4_data->det_work);
	flush_workqueue(rmi4_data->det_workqueue);
	destroy_workqueue(rmi4_data->det_workqueue);

	rmi4_data->touch_stopped = true;
	wake_up(&rmi4_data->wait);

	synaptics_rmi4_irq_acquire(rmi4_data, false);

	for (attr_count = 0; attr_count < ARRAY_SIZE(attrs); attr_count++) {
		sysfs_remove_file(&rmi4_data->input_dev->dev.kobj,
				&attrs[attr_count].attr);
	}

	input_unregister_device(rmi4_data->input_dev);
    /*** ZTEMT Added by luochangyang, 2013/11/29 ***/
#if defined(CONFIG_FB)
    if (fb_unregister_client(&rmi4_data->fb_notif))
        dev_err(&client->dev, "Error occurred while unregistering fb_notifier.\n");
#elif defined(CONFIG_HAS_EARLYSUSPEND)
    unregister_early_suspend(&rmi4_data->early_suspend);
#endif
    /***ZTEMT END***/

	if (platform_data->regulator_en) {
		regulator_disable(rmi4_data->regulator);
		regulator_put(rmi4_data->regulator);
  	}

	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->fn_number == SYNAPTICS_RMI4_F1A)
				synaptics_rmi4_f1a_kfree(fhandler);
			else
				kfree(fhandler->data);
			kfree(fhandler);
		}
	}
	input_free_device(rmi4_data->input_dev);

	kfree(rmi4_data);

	return 0;
}

#ifdef CONFIG_PM
 /**
 * synaptics_rmi4_sensor_sleep()
 *
 * Called by synaptics_rmi4_early_suspend() and synaptics_rmi4_suspend().
 *
 * This function stops finger data acquisition and puts the sensor to sleep.
 */
static void synaptics_rmi4_sensor_sleep(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char device_ctrl;
    /*** ZTEMT Added by luochangyang, 2013/11/19 ***/
    unsigned char report_mode;

    if (!(rmi4_data->sensor_sleep) && rmi4_data->wakeup_gesture) {
    	retval = synaptics_rmi4_i2c_read(rmi4_data,
    			rmi4_data->f01_ctrl_base_addr + 5,
    			&report_mode,
    			sizeof(report_mode));
    	if (retval < 0) {
    		dev_err(&(rmi4_data->input_dev->dev),
    				"%s: Failed to change report mode\n", __func__);
    		rmi4_data->sensor_sleep = false;
    		return;
    	}

        report_mode |= 0x04;

    	retval = synaptics_rmi4_i2c_write(rmi4_data,
    			rmi4_data->f01_ctrl_base_addr + 5,
    			&report_mode,
    			sizeof(report_mode));
    	if (retval < 0) {
    		dev_err(&(rmi4_data->input_dev->dev),
    				"%s: Failed to change report mode\n",
    				__func__);
    		rmi4_data->sensor_sleep = false;
    		return;
    	} else {
    		rmi4_data->sensor_sleep = true;
    	}
        return;
    }
    /***ZTEMT END***/

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		dev_err(&(rmi4_data->input_dev->dev),
				"%s: Failed to enter sleep mode\n",
				__func__);
		rmi4_data->sensor_sleep = false;
		return;
	}
    
	device_ctrl = (device_ctrl & ~MASK_3BIT);
	device_ctrl = (device_ctrl | NO_SLEEP_OFF | SENSOR_SLEEP);

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		dev_err(&(rmi4_data->input_dev->dev),
				"%s: Failed to enter sleep mode\n",
				__func__);
		rmi4_data->sensor_sleep = false;
		return;
	} else {
		rmi4_data->sensor_sleep = true;
	}

	return;
}

 /**
 * synaptics_rmi4_sensor_wake()
 *
 * Called by synaptics_rmi4_resume() and synaptics_rmi4_late_resume().
 *
 * This function wakes the sensor from sleep.
 */
static void synaptics_rmi4_sensor_wake(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char device_ctrl;
    /*** ZTEMT Added by luochangyang, 2013/11/19 ***/
    unsigned char report_mode;

    if (rmi4_data->sensor_sleep && rmi4_data->wakeup_gesture) {
    	retval = synaptics_rmi4_i2c_read(rmi4_data,
    			rmi4_data->f01_ctrl_base_addr + 5,
    			&report_mode,
    			sizeof(report_mode));
    	if (retval < 0) {
    		dev_err(&(rmi4_data->input_dev->dev),
    				"%s: Failed to change report mode\n",
    				__func__);
    		rmi4_data->sensor_sleep = true;
    		return;
    	}

        report_mode &= 0xF8;

    	retval = synaptics_rmi4_i2c_write(rmi4_data,
    			rmi4_data->f01_ctrl_base_addr + 5,
    			&report_mode,
    			sizeof(report_mode));
    	if (retval < 0) {
    		dev_err(&(rmi4_data->input_dev->dev),
    				"%s: Failed to change report mode\n",
    				__func__);
    		rmi4_data->sensor_sleep = true;
    		return;
    	} else {
    		rmi4_data->sensor_sleep = false;
    	}
        return;
    }
    /***ZTEMT END***/

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		dev_err(&(rmi4_data->input_dev->dev),
				"%s: Failed to wake from sleep mode\n",
				__func__);
		rmi4_data->sensor_sleep = true;
		return;
	}

	device_ctrl = (device_ctrl & ~MASK_3BIT);
	device_ctrl = (device_ctrl | NO_SLEEP_OFF | NORMAL_OPERATION);

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		dev_err(&(rmi4_data->input_dev->dev),
				"%s: Failed to wake from sleep mode\n",
				__func__);
		rmi4_data->sensor_sleep = true;
		return;
	} else {
		rmi4_data->sensor_sleep = false;
	}

	return;
}

/*** ZTEMT Added by luochangyang, 2013/11/30 ***/
#if defined(CONFIG_FB) || defined(CONFIG_HAS_EARLYSUSPEND)
static int synaptics_rmi4_ztemt_sleep(struct synaptics_rmi4_data *rmi4_data)
{
	rmi4_data->touch_stopped = true;
	wake_up(&rmi4_data->wait);
    
    /*** ZTEMT Added by luochangyang, 2013/11/19 ***/
    if (rmi4_data->wakeup_gesture) {
    	synaptics_rmi4_sensor_sleep(rmi4_data);
        return 0;
    }
    /***ZTEMT END***/
    
	synaptics_rmi4_irq_enable(rmi4_data, false);
	synaptics_rmi4_sensor_sleep(rmi4_data);

	if (rmi4_data->full_pm_cycle)
		synaptics_rmi4_suspend(&(rmi4_data->input_dev->dev));
    
    return 0;
}

static int synaptics_rmi4_ztemt_wake(struct synaptics_rmi4_data *rmi4_data)
{
    /*** ZTEMT Added by luochangyang, 2013/11/19 ***/
    if (rmi4_data->wakeup_gesture) {
		synaptics_rmi4_sensor_wake(rmi4_data);
		rmi4_data->touch_stopped = false;
        return 0;
    }
    /***ZTEMT END***/

	if (rmi4_data->full_pm_cycle)
		synaptics_rmi4_resume(&(rmi4_data->input_dev->dev));

	if (rmi4_data->sensor_sleep == true) {
		synaptics_rmi4_sensor_wake(rmi4_data);
		rmi4_data->touch_stopped = false;
		synaptics_rmi4_irq_enable(rmi4_data, true);
	}
    return 0;
}
#endif

#if defined(CONFIG_FB)
static int synaptics_rmi4_fb_notifier_callback(struct notifier_block *self,
				 unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;
	struct synaptics_rmi4_data *rmi4_data =
		container_of(self, struct synaptics_rmi4_data, fb_notif);

	if (evdata && evdata->data && rmi4_data && 
        rmi4_data->input_dev && (event == FB_EVENT_BLANK)) {

		blank = evdata->data;
		if (*blank == FB_BLANK_UNBLANK) {
            synaptics_rmi4_ztemt_wake(rmi4_data);
            dev_info(&(rmi4_data->input_dev->dev), "ztemt %s: Wake!\n", __func__);
		}
		else if (*blank == FB_BLANK_POWERDOWN) {
            synaptics_rmi4_ztemt_sleep(rmi4_data);
            dev_info(&(rmi4_data->input_dev->dev), "ztemt %s: Sleep!\n", __func__);
		}
	}

	return 0;
}
#elif defined(CONFIG_HAS_EARLYSUSPEND)
 /***ZTEMT END***/

 /**
 * synaptics_rmi4_early_suspend()
 *
 * Called by the kernel during the early suspend phase when the system
 * enters suspend.
 *
 * This function calls synaptics_rmi4_sensor_sleep() to stop finger
 * data acquisition and put the sensor to sleep.
 */
static void synaptics_rmi4_early_suspend(struct early_suspend *h)
{
	struct synaptics_rmi4_data *rmi4_data =
			container_of(h, struct synaptics_rmi4_data,
			early_suspend);

    synaptics_rmi4_ztemt_sleep(rmi4_data);
    dev_info(&(rmi4_data->input_dev->dev), "ztemt %s: Sleep!\n", __func__);
}

 /**
 * synaptics_rmi4_late_resume()
 *
 * Called by the kernel during the late resume phase when the system
 * wakes up from suspend.
 *
 * This function goes through the sensor wake process if the system wakes
 * up from early suspend (without going into suspend).
 */
static void synaptics_rmi4_late_resume(struct early_suspend *h)
{
	struct synaptics_rmi4_data *rmi4_data =
			container_of(h, struct synaptics_rmi4_data,
			early_suspend);
    
    synaptics_rmi4_ztemt_wake(rmi4_data);
    dev_info(&(rmi4_data->input_dev->dev), "ztemt %s: Wake!\n", __func__);

}
#endif

#if defined(CONFIG_PM_RUNTIME)
 /**
 * synaptics_rmi4_suspend()
 *
 * Called by the kernel during the suspend phase when the system
 * enters suspend.
 *
 * This function stops finger data acquisition and puts the sensor to
 * sleep (if not already done so during the early suspend phase),
 * disables the interrupt, and turns off the power to the sensor.
 */
static int synaptics_rmi4_suspend(struct device *dev)
{
	int ret = 0;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	const struct synaptics_dsx_platform_data *platform_data =
			rmi4_data->board;

	ret = pinctrl_select_state(synaptics_pctrl.pinctrl,
			synaptics_pctrl.gpio_state_suspend);
	if (ret)
		dev_err(dev, "%s:%d cannot set pin to suspend state",
			__func__, __LINE__);

	if (!rmi4_data->sensor_sleep) {
		rmi4_data->touch_stopped = true;
		wake_up(&rmi4_data->wait);
		synaptics_rmi4_irq_enable(rmi4_data, false);
		synaptics_rmi4_sensor_sleep(rmi4_data);
	}

	if ((!rmi4_data->wakeup_gesture) && platform_data->regulator_en)
		regulator_disable(rmi4_data->regulator);

	return 0;
}

 /**
 * synaptics_rmi4_resume()
 *
 * Called by the kernel during the resume phase when the system
 * wakes up from suspend.
 *
 * This function turns on the power to the sensor, wakes the sensor
 * from sleep, enables the interrupt, and starts finger data
 * acquisition.
 */
static int synaptics_rmi4_resume(struct device *dev)
{
	int ret = 0;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	const struct synaptics_dsx_platform_data *platform_data =
			rmi4_data->board;

	ret = pinctrl_select_state(synaptics_pctrl.pinctrl,
			synaptics_pctrl.gpio_state_active);
	if (ret)
		dev_err(dev, "%s:%d cannot set pin to active state",
			__func__, __LINE__);

	if ((!rmi4_data->wakeup_gesture) && platform_data->regulator_en) {
		ret = regulator_enable(rmi4_data->regulator);
		if (ret) {
		    dev_err(dev, "Failed to enable vin:%d\n", ret);
	    }
	}

	synaptics_rmi4_sensor_wake(rmi4_data);
	rmi4_data->touch_stopped = false;
	synaptics_rmi4_irq_enable(rmi4_data, true);

	return 0;
}
#endif

/*** ZTEMT Added by luochangyang, 2013/11/19 ***/
#if defined(CONFIG_PM_SLEEP)
static int synaptics_wakeup_gesture_suspend(struct device *dev)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	if (!(rmi4_data->wakeup_gesture))
		return 0;

	/*
	 * This will not prevent resume
	 * Required to prevent interrupts before i2c awake
	 */
	disable_irq(rmi4_data->irq);

	if (device_may_wakeup(dev)) {
		dev_dbg(dev, "%s Device MAY wakeup\n", __func__);
		if (!enable_irq_wake(rmi4_data->irq))
			rmi4_data->irq_wake = 1;
	} else {
		dev_dbg(dev, "%s Device may NOT wakeup\n", __func__);
	}

	return 0;
}

static int synaptics_wakeup_gesture_resume(struct device *dev)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	if (!(rmi4_data->wakeup_gesture))
		return 0;
    
    enable_irq(rmi4_data->irq);

	if (device_may_wakeup(dev)) {
		dev_dbg(dev, "%s Device MAY wakeup\n", __func__);
		if (rmi4_data->irq_wake) {
			disable_irq_wake(rmi4_data->irq);
			rmi4_data->irq_wake = 0;
		}
	} else {
		dev_dbg(dev, "%s Device may NOT wakeup\n", __func__);
	}

	return 0;
}
#endif
/***ZTEMT END***/

static const struct dev_pm_ops synaptics_rmi4_dev_pm_ops = {
/*** ZTEMT Modify by luochangyang, 2013/11/19 ***/
#if SYNAPTICS_WAKEUP_GESTURE
    SET_SYSTEM_SLEEP_PM_OPS(synaptics_wakeup_gesture_suspend, synaptics_wakeup_gesture_resume)
    SET_RUNTIME_PM_OPS(synaptics_rmi4_suspend, synaptics_rmi4_resume, NULL)
#else
	.suspend = synaptics_rmi4_suspend,
	.resume  = synaptics_rmi4_resume,
#endif
/***ZTEMT END***/
};
#endif

/*** ZTEMT Added by luochangyang, 2013/11/26 ***/
static struct of_device_id synaptics_rmi4_i2c_of_match[] = {
	{ .compatible = "synaptics_dsx_i2c,rmi4", },
	{ }
};
MODULE_DEVICE_TABLE(of, synaptics_rmi4_i2c_of_match);
/***ZTEMT END***/

static const struct i2c_device_id synaptics_rmi4_id_table[] = {
	{SYNAPTICS_DRIVER_NAME, 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, synaptics_rmi4_id_table);

static struct i2c_driver synaptics_rmi4_driver = {
	.probe = synaptics_rmi4_probe,
	.remove = synaptics_rmi4_remove,
	.id_table = synaptics_rmi4_id_table,
	.driver = {
		.name = SYNAPTICS_DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = synaptics_rmi4_i2c_of_match,
		.pm = &synaptics_rmi4_dev_pm_ops,
	},
};

 /**
 * synaptics_rmi4_init()
 *
 * Called by the kernel during do_initcalls (if built-in)
 * or when the driver is loaded (if a module).
 *
 * This function registers the driver to the I2C subsystem.
 *
 */
static int synaptics_rmi4_init(void)
{
    pr_debug("%s:Start...\n", __func__);
	return i2c_add_driver(&synaptics_rmi4_driver);
}

 /**
 * synaptics_rmi4_exit()
 *
 * Called by the kernel when the driver is unloaded.
 *
 * This funtion unregisters the driver from the I2C subsystem.
 *
 */
static void synaptics_rmi4_exit(void)
{
	i2c_del_driver(&synaptics_rmi4_driver);
}

module_init(synaptics_rmi4_init);
module_exit(synaptics_rmi4_exit);

MODULE_AUTHOR("Synaptics, Inc.");
MODULE_DESCRIPTION("Synaptics DSX I2C Touch Driver");
MODULE_LICENSE("GPL v2");
