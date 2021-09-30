// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/input/mt.h>

// Driver was tested on following device:
//      7inch Capacitive Touch Screen LCD (B), 800×480, HDMI, Low Power
//      SKU: 10829
//      Part Number: 7inch HDMI LCD (B)
//      Brand: Waveshare 

struct wsh7_device {
        struct hid_device *hdev;
        struct input_dev  *dev;
        int was_pressed;
};

#define USB_VENDOR_ID_WSH7INCH          0x0eef
#define USB_PRODUCT_ID_WSH7INCH         0x0005

#define WSH7INCH_WIDTH                  800
#define WSH7INCH_HEIGHT                 480

#define WSH7INCH_MSG_LENGTH             25

#define WSH7INCH_IS_PRESSED_OFFSET      1
#define WSH7INCH_FIRST_POINT_OFFSET     2
#define WSH7INCH_PRESSED_BITS_OFFSET    7
#define WSH7INCH_SECOND_POINT_OFFSET    8
#define WSH7INCH_THIRD_POINT_OFFSET     12
#define WSH7INCH_FOURTH_POINT_OFFSET    16
#define WSH7INCH_FIFTH_POINT_OFFSET     20
#define WSH7INCH_FINGER_CNT             5

static inline u16 wsh7_read_point(const u8 *buffer, int offset)
{
        return (u16)(   (((u16)buffer[offset    ]) << 8)
                       | ((u16)buffer[offset + 1])     );
}

static void wsh7_slot_filled(struct input_dev *dev, int slot_num, u16 x, u16 y) {
        input_mt_slot(dev, slot_num);
        if (x || y) {
                input_mt_report_slot_state(dev, MT_TOOL_FINGER, true);
                input_report_abs(dev, ABS_MT_POSITION_X, x);
                input_report_abs(dev, ABS_MT_POSITION_Y, y);
        } else
                input_mt_report_slot_state(dev, MT_TOOL_FINGER, false);
}

static inline void wsh7_point_direct(struct input_dev *dev, int slot_num, const u8 *buf, int offset)
{
        u16 x = wsh7_read_point(buf, offset);
        u16 y = wsh7_read_point(buf, offset + 2);
        wsh7_slot_filled(dev, slot_num, x, y);
}

static void wsh7_point_reverse(struct input_dev *dev, int slot_num, const u8 *buf, int offset)
{
        u16 x = wsh7_read_point(buf, offset + 2);
        u16 y = wsh7_read_point(buf, offset);
        wsh7_slot_filled(dev, slot_num, x, y);
}

static void wsh7_empty_slot(struct input_dev *dev, int slot) {
        input_mt_slot(dev, slot);
        input_mt_report_slot_state(dev, MT_TOOL_FINGER, false);
}

static int wsh7_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *buffer, int size)
{
        struct wsh7_device *wsh7 = hid_get_drvdata(hdev);
        struct input_dev   *dev  = wsh7->dev;
        int pressed_bits;

        if (size == WSH7INCH_MSG_LENGTH) {
                pressed_bits = buffer[WSH7INCH_PRESSED_BITS_OFFSET];

                if (pressed_bits || wsh7->was_pressed) {
                        wsh7->was_pressed = pressed_bits & 0x1F;

                        if (pressed_bits & 0x01) 
                                wsh7_point_direct (dev, 0, buffer, WSH7INCH_FIRST_POINT_OFFSET);
                        else
                                wsh7_empty_slot   (dev, 0);
                        if (pressed_bits & 0x02)
                                wsh7_point_reverse(dev, 1, buffer, WSH7INCH_SECOND_POINT_OFFSET);
                        else
                                wsh7_empty_slot   (dev, 1);
                        if (pressed_bits & 0x04)
                                wsh7_point_reverse(dev, 2, buffer, WSH7INCH_THIRD_POINT_OFFSET);
                        else
                                wsh7_empty_slot   (dev, 2);
                        if (pressed_bits & 0x08)
                                wsh7_point_reverse(dev, 3, buffer, WSH7INCH_FOURTH_POINT_OFFSET);
                        else
                                wsh7_empty_slot   (dev, 3);
                        if (pressed_bits & 0x10)
                                wsh7_point_reverse(dev, 4, buffer, WSH7INCH_FIFTH_POINT_OFFSET);
                        else
                                wsh7_empty_slot   (dev, 4);

                        input_mt_sync_frame(dev);
                        input_sync(dev);
                }
                return 1;
        }
        return 0;
}

static int wsh7_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
        int ret;
        struct wsh7_device *wsh7;
        struct input_dev   *input;

        wsh7 = devm_kzalloc(&hdev->dev, sizeof(struct wsh7_device), GFP_KERNEL);
        if (!wsh7)
                return -ENOMEM;

        wsh7->hdev = hdev;
        hid_set_drvdata(hdev, wsh7);

        ret = hid_parse(hdev);
        if (ret) {
                hid_err(hdev, "can't parse reports\n");
                return ret;
        }

        input = devm_input_allocate_device(&hdev->dev);
        if (!input) {
                hid_err(hdev, "can't allocate input device\n");
                return -ENOMEM;
        }

        input->name       = "Waveshare 7inch HDMI LCD (B)";
        input->phys       = hdev->phys;
        input->uniq       = hdev->uniq;
        input->id.bustype = hdev->bus;
        input->id.vendor  = hdev->vendor;
        input->id.product = hdev->product;
        input->id.version = hdev->version;
        input->dev.parent = &hdev->dev;

        input_set_abs_params(input, ABS_MT_POSITION_X, 0, WSH7INCH_WIDTH,  0, 0);
        input_set_abs_params(input, ABS_MT_POSITION_Y, 0, WSH7INCH_HEIGHT, 0, 0);

        ret = input_mt_init_slots(input, WSH7INCH_FINGER_CNT, INPUT_MT_DIRECT);
        if (ret) {
                hid_err(hdev, "Failed to init MT slots: %d\n", ret);
                goto free_device;
        }

        ret = input_register_device(input);

        if (ret) {
                hid_err(hdev, "Failed to register waveshare input device: %d\n", ret);
                goto free_slots;
        }

        wsh7->dev = input;

        ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
        if (ret) {
                hid_err(hdev, "can't start hardware\n");
                goto free_slots;
        }

        ret = hid_hw_open(hdev);
        if (ret) {
                hid_err(hdev, "can't open hardware\n");
                goto hw_stop;
        }

        return 0;

hw_stop:
        hid_hw_stop(hdev);
free_slots:
        input_mt_destroy_slots(input);
free_device:
        input_free_device(input);
        return ret;
}

static void wsh7_remove(struct hid_device *hdev)
{
        hid_hw_close(hdev);
        hid_hw_stop(hdev);
}

static int wsh7_input_mapping(struct hid_device *dev,
                              struct hid_input *input,
                              struct hid_field *field,
                              struct hid_usage *usage,
                              unsigned long **bit,
                              int *max)
{
        return -1;
}

static const struct hid_device_id wsh7_table[] = {
        { HID_USB_DEVICE(USB_VENDOR_ID_WSH7INCH, USB_PRODUCT_ID_WSH7INCH) },
        {}
};

MODULE_DEVICE_TABLE(hid, wsh7_table);

static struct hid_driver hid_wsh7 = {
        .name          = "wsh7inch",
        .id_table      = wsh7_table,
        .probe         = wsh7_probe,
        .remove        = wsh7_remove,
        .raw_event     = wsh7_raw_event,
        .input_mapping = wsh7_input_mapping
};

module_hid_driver(hid_wsh7);

MODULE_AUTHOR     ("Dmitrii Sharikhin <sharihin.dmitry93@gmail.com>");
MODULE_DESCRIPTION("7 inch Waveshare touch panel driver");
MODULE_LICENSE    ("GPL");
