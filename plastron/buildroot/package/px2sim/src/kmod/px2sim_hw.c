// SPDX-License-Identifier: GPL-2.0
/*
 * px2sim_hw: the GKD Pixel 2's battery, charger, backlight and side lights,
 * simulated, for plastron's QEMU image (configs/qemu_defconfig).
 *
 * The same sysfs devices the Pixel 2's kernel makes, by the same names, so
 * ZyperOS, Diatom and gauge run against them unchanged:
 *
 *   /sys/class/power_supply/battery         (the RK817's gauge)
 *   /sys/class/power_supply/rk817-charger   (its charger: online = the cable)
 *   /sys/class/backlight/backlight          (0-255, bl_power)
 *   /sys/class/leds/battery0..battery4      (the side lights, on or off)
 *
 * What the battery and the cable say is set from userspace, through the
 * module's parameters (/sys/module/px2sim_hw/parameters/capacity, cable,
 * kind, current_ma, charge_full, charge_full_design); a change sends the
 * power supplies' uevents, as a real one does.
 *
 * The charger as the Pixel 2 has it (measured 2026-11-05 and 11-06): what is
 * at the cable's end is the USB PHY's BC1.2 answer, an extcon with DCP for a
 * wall charger (2 A in) and SDP for a computer (1.5 A), here as kind 1 or 2
 * (the rk817 driver's own usb_type says DCP whatever it is, so nothing reads
 * that); the battery takes its charging current until about 85%, then less
 * and less down to the 150 mA the RK817 ends a charge at, as a battery's CV
 * phase does. The extcon needs CONFIG_EXTCON (board/qemu/linux-qemu.fragment);
 * without it there is none, as on a kernel that has no PHY to ask. What the programs set (the backlight, the
 * lights) is read from /dev/px2sim, one line of state per change, by
 * px2sim's daemon, which hands it to the simulator window on the Mac.
 */
#include <linux/atomic.h>
#include <linux/backlight.h>
#include <linux/bitops.h>
#include <linux/extcon-provider.h>
#include <linux/fs.h>
#include <linux/leds.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#define LIGHTS 5

static int capacity = 100;
static bool cable = true;
static int kind = 1;		/* 1 a wall charger (DCP), 2 a computer (SDP) */
static int current_ma;		/* 0: the model below; else the charging current */
static int charge_full = 1900000;		/* uAh */
static int charge_full_design = 2000000;	/* uAh */

static atomic_t generation = ATOMIC_INIT(1);
static DECLARE_WAIT_QUEUE_HEAD(waitq);
static unsigned long lights_on;
static struct power_supply *battery, *charger;
static struct extcon_dev *edev;
static const unsigned int edev_cables[] = {
	EXTCON_USB, EXTCON_CHG_USB_SDP, EXTCON_CHG_USB_DCP, EXTCON_NONE,
};
static struct backlight_device *backlight;
static struct led_classdev lights[LIGHTS];
static char light_names[LIGHTS][12];

static void changed(void)
{
	atomic_inc(&generation);
	wake_up_interruptible(&waitq);
}

/* ---- the battery and the charger ---------------------------------------- */

/* Into the battery, in uA: on the cable the charger's current until 85% (CC),
 * then falling with what is left down to the RK817's 150 mA end (CV), and
 * nothing once full; off it, what a lit shelf draws, below zero as the RK817
 * reports it. The charging current: current_ma, else 1.5 A from a wall
 * charger and 0.9 A from a computer (their 2 A and 1.5 A in, less what the
 * handheld uses). */
static int battery_ua(void)
{
	int cap = READ_ONCE(capacity), cc;

	if (!READ_ONCE(cable))
		return -400000;
	if (cap >= 100)
		return 0;
	cc = READ_ONCE(current_ma) > 0 ? READ_ONCE(current_ma) : READ_ONCE(kind) == 2 ? 900 : 1500;
	if (cap >= 85)
		cc = max(150, cc * (100 - cap) / 15);
	return cc * 1000;
}

/* The extcon's three states for the cable and the kind */
static void edev_sync(void)
{
	bool on = READ_ONCE(cable);

	if (!edev)
		return;
	extcon_set_state_sync(edev, EXTCON_USB, on);
	extcon_set_state_sync(edev, EXTCON_CHG_USB_SDP, on && READ_ONCE(kind) == 2);
	extcon_set_state_sync(edev, EXTCON_CHG_USB_DCP, on && READ_ONCE(kind) != 2);
}

static enum power_supply_property battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
};

static int battery_get(struct power_supply *psy, enum power_supply_property psp,
		       union power_supply_propval *val)
{
	int cap = READ_ONCE(capacity);
	bool on = READ_ONCE(cable);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = !on ? POWER_SUPPLY_STATUS_DISCHARGING :
			      cap >= 100 ? POWER_SUPPLY_STATUS_FULL : POWER_SUPPLY_STATUS_CHARGING;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LIPO;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = cap;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		/* 3.4 V empty to 4.2 V full, near enough a LiPo's curve; charging,
		 * a little over that and held at the RK817's 4.35 V in CV */
		val->intval = !on ? 3400000 + cap * 8000 : min(4350000, 3500000 + cap * 8500);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval = battery_ua();
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = READ_ONCE(charge_full) / 100 * cap;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = READ_ONCE(charge_full);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = READ_ONCE(charge_full_design);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static enum power_supply_property charger_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static int charger_get(struct power_supply *psy, enum power_supply_property psp,
		       union power_supply_propval *val)
{
	if (psp != POWER_SUPPLY_PROP_ONLINE)
		return -EINVAL;
	val->intval = READ_ONCE(cable);
	return 0;
}

static const struct power_supply_desc battery_desc = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = battery_props,
	.num_properties = ARRAY_SIZE(battery_props),
	.get_property = battery_get,
};

static const struct power_supply_desc charger_desc = {
	.name = "rk817-charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = charger_props,
	.num_properties = ARRAY_SIZE(charger_props),
	.get_property = charger_get,
};

/* A parameter written from userspace: the supplies' uevents, then the state
 * line, so gauge, ZyperOS and the simulator window all hear of it */
static int set_and_tell(const char *val, const struct kernel_param *kp,
			int (*set)(const char *, const struct kernel_param *))
{
	int ret = set(val, kp);

	if (ret)
		return ret;
	WRITE_ONCE(capacity, clamp(READ_ONCE(capacity), 0, 100));
	WRITE_ONCE(kind, READ_ONCE(kind) == 2 ? 2 : 1);
	WRITE_ONCE(current_ma, clamp(READ_ONCE(current_ma), 0, 3000));
	edev_sync();
	if (battery)
		power_supply_changed(battery);
	if (charger)
		power_supply_changed(charger);
	changed();
	return 0;
}

static int set_int(const char *val, const struct kernel_param *kp)
{
	return set_and_tell(val, kp, param_set_int);
}

static int set_bool(const char *val, const struct kernel_param *kp)
{
	return set_and_tell(val, kp, param_set_bool);
}

static const struct kernel_param_ops int_ops = { .set = set_int, .get = param_get_int };
static const struct kernel_param_ops bool_ops = { .set = set_bool, .get = param_get_bool };

module_param_cb(capacity, &int_ops, &capacity, 0644);
MODULE_PARM_DESC(capacity, "Battery level, 0-100");
module_param_cb(cable, &bool_ops, &cable, 0644);
MODULE_PARM_DESC(cable, "The USB cable in: charging");
module_param_cb(kind, &int_ops, &kind, 0644);
MODULE_PARM_DESC(kind, "What the cable is in: 1 a wall charger (DCP), 2 a computer (SDP)");
module_param_cb(current_ma, &int_ops, &current_ma, 0644);
MODULE_PARM_DESC(current_ma, "Charging current in mA, 0 for the CC/CV model");
module_param_cb(charge_full, &int_ops, &charge_full, 0644);
MODULE_PARM_DESC(charge_full, "Full charge in uAh (About's battery health)");
module_param_cb(charge_full_design, &int_ops, &charge_full_design, 0644);
MODULE_PARM_DESC(charge_full_design, "Design charge in uAh");

/* ---- the backlight and the lights ----------------------------------------- */

static int backlight_update(struct backlight_device *bd)
{
	changed();
	return 0;
}

static const struct backlight_ops backlight_ops = {
	.update_status = backlight_update,
};

static void light_set(struct led_classdev *cdev, enum led_brightness value)
{
	int i = cdev - lights;

	if (value)
		set_bit(i, &lights_on);
	else
		clear_bit(i, &lights_on);
	changed();
}

/* ---- /dev/px2sim: the state, a line per change ----------------------------- */

static int state_open(struct inode *inode, struct file *file)
{
	int *seen = kzalloc(sizeof(*seen), GFP_KERNEL);

	if (!seen)
		return -ENOMEM;
	file->private_data = seen;	/* 0: the first read answers at once */
	return 0;
}

static int state_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static ssize_t state_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	int *seen = file->private_data;
	char line[112];
	int gen, n;

	if (file->f_flags & O_NONBLOCK) {
		if (atomic_read(&generation) == *seen)
			return -EAGAIN;
	} else if (wait_event_interruptible(waitq, atomic_read(&generation) != *seen)) {
		return -ERESTARTSYS;
	}
	gen = atomic_read(&generation);
	n = scnprintf(line, sizeof(line),
		      "leds=%lu backlight=%d bl_power=%d battery=%d cable=%d kind=%d\n",
		      READ_ONCE(lights_on) & ((1UL << LIGHTS) - 1),
		      backlight->props.brightness, backlight->props.power,
		      READ_ONCE(capacity), READ_ONCE(cable), READ_ONCE(kind));
	if (len < n)
		return -EINVAL;
	if (copy_to_user(buf, line, n))
		return -EFAULT;
	*seen = gen;
	return n;
}

static __poll_t state_poll(struct file *file, poll_table *wait)
{
	int *seen = file->private_data;

	poll_wait(file, &waitq, wait);
	return atomic_read(&generation) != *seen ? EPOLLIN | EPOLLRDNORM : 0;
}

static const struct file_operations state_fops = {
	.owner = THIS_MODULE,
	.open = state_open,
	.release = state_release,
	.read = state_read,
	.poll = state_poll,
	.llseek = noop_llseek,
};

static struct miscdevice state_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "px2sim",
	.fops = &state_fops,
};

static int __init px2sim_init(void)
{
	struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.max_brightness = 255,
		.brightness = 128,
		.power = BACKLIGHT_POWER_ON,
	};
	struct power_supply_config cfg = { };
	int i, ret;

	backlight = backlight_device_register("backlight", NULL, NULL, &backlight_ops, &props);
	if (IS_ERR(backlight))
		return PTR_ERR(backlight);
	for (i = 0; i < LIGHTS; i++) {
		snprintf(light_names[i], sizeof(light_names[i]), "battery%d", i);
		lights[i].name = light_names[i];
		lights[i].max_brightness = 1;
		lights[i].brightness_set = light_set;
		ret = led_classdev_register(NULL, &lights[i]);
		if (ret)
			goto undo_lights;
	}
	charger = power_supply_register(NULL, &charger_desc, &cfg);
	if (IS_ERR(charger)) {
		ret = PTR_ERR(charger);
		goto undo_lights;
	}
	battery = power_supply_register(NULL, &battery_desc, &cfg);
	if (IS_ERR(battery)) {
		ret = PTR_ERR(battery);
		goto undo_charger;
	}
	ret = misc_register(&state_dev);
	if (ret)
		goto undo_battery;
	/* The PHY's answer, named for /dev/px2sim's device and gone with it;
	 * none without CONFIG_EXTCON, where the allocation says -ENOSYS */
	edev = devm_extcon_dev_allocate(state_dev.this_device, edev_cables);
	if (IS_ERR(edev) || devm_extcon_dev_register(state_dev.this_device, edev))
		edev = NULL;
	edev_sync();
	return 0;

undo_battery:
	power_supply_unregister(battery);
undo_charger:
	power_supply_unregister(charger);
undo_lights:
	while (--i >= 0)
		led_classdev_unregister(&lights[i]);
	backlight_device_unregister(backlight);
	return ret;
}

static void __exit px2sim_exit(void)
{
	int i;

	edev = NULL;	/* freed with /dev/px2sim's device, next */
	misc_deregister(&state_dev);
	power_supply_unregister(battery);
	power_supply_unregister(charger);
	for (i = 0; i < LIGHTS; i++)
		led_classdev_unregister(&lights[i]);
	backlight_device_unregister(backlight);
}

module_init(px2sim_init);
module_exit(px2sim_exit);
MODULE_DESCRIPTION("The GKD Pixel 2's battery, charger, backlight and side lights, simulated");
MODULE_LICENSE("GPL");
