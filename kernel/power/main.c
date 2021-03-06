/*
 * kernel/power/main.c - PM subsystem core functionality.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 * 
 * This file is released under the GPLv2
 *
 */

#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/resume-trace.h>
#include <linux/ftrace.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/cpufreq.h>

#include "power.h"

DEFINE_MUTEX(pm_mutex);

unsigned int pm_flags;
EXPORT_SYMBOL(pm_flags);
#if 1	// added by peres to show valid current state
suspend_state_t global_state;
#endif

#ifndef FEATURE_FTM_SLEEP
#define FEATURE_FTM_SLEEP
#endif
#if 1 
unsigned char ftm_sleep = 0;
EXPORT_SYMBOL(ftm_sleep);

void (*ftm_enable_usb_sw)(int mode);
EXPORT_SYMBOL(ftm_enable_usb_sw);

extern void wakelock_force_suspend(void);

static struct wake_lock ftm_wake_lock;
#endif

#ifdef CONFIG_PM_SLEEP

/* Routines for PM-transition notifications */

static BLOCKING_NOTIFIER_HEAD(pm_chain_head);

int register_pm_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&pm_chain_head, nb);
}
EXPORT_SYMBOL_GPL(register_pm_notifier);

int unregister_pm_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&pm_chain_head, nb);
}
EXPORT_SYMBOL_GPL(unregister_pm_notifier);

int pm_notifier_call_chain(unsigned long val)
{
	return (blocking_notifier_call_chain(&pm_chain_head, val, NULL)
			== NOTIFY_BAD) ? -EINVAL : 0;
}

#ifdef CONFIG_PM_DEBUG
int pm_test_level = TEST_NONE;

static const char * const pm_tests[__TEST_AFTER_LAST] = {
	[TEST_NONE] = "none",
	[TEST_CORE] = "core",
	[TEST_CPUS] = "processors",
	[TEST_PLATFORM] = "platform",
	[TEST_DEVICES] = "devices",
	[TEST_FREEZER] = "freezer",
};

static ssize_t pm_test_show(struct kobject *kobj, struct kobj_attribute *attr,
				char *buf)
{
	char *s = buf;
	int level;

	for (level = TEST_FIRST; level <= TEST_MAX; level++)
		if (pm_tests[level]) {
			if (level == pm_test_level)
				s += sprintf(s, "[%s] ", pm_tests[level]);
			else
				s += sprintf(s, "%s ", pm_tests[level]);
		}

	if (s != buf)
		/* convert the last space to a newline */
		*(s-1) = '\n';

	return (s - buf);
}

static ssize_t pm_test_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *buf, size_t n)
{
	const char * const *s;
	int level;
	char *p;
	int len;
	int error = -EINVAL;

	p = memchr(buf, '\n', n);
	len = p ? p - buf : n;

	mutex_lock(&pm_mutex);

	level = TEST_FIRST;
	for (s = &pm_tests[level]; level <= TEST_MAX; s++, level++)
		if (*s && len == strlen(*s) && !strncmp(buf, *s, len)) {
			pm_test_level = level;
			error = 0;
			break;
		}

	mutex_unlock(&pm_mutex);

	return error ? error : n;
}

power_attr(pm_test);
#endif /* CONFIG_PM_DEBUG */

#endif /* CONFIG_PM_SLEEP */

struct kobject *power_kobj;

/**
 *	state - control system power state.
 *
 *	show() returns what states are supported, which is hard-coded to
 *	'standby' (Power-On Suspend), 'mem' (Suspend-to-RAM), and
 *	'disk' (Suspend-to-Disk).
 *
 *	store() accepts one of those strings, translates it into the 
 *	proper enumerated value, and initiates a suspend transition.
 */
static ssize_t state_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	char *s = buf;
#ifdef CONFIG_SUSPEND
	int i;

	for (i = 0; i < PM_SUSPEND_MAX; i++) {
		if (pm_states[i] && valid_state(i))
			s += sprintf(s,"%s ", pm_states[i]);
	}
#endif
#ifdef CONFIG_HIBERNATION
	s += sprintf(s, "%s\n", "disk");
#else
	if (s != buf)
		/* convert the last space to a newline */
		*(s-1) = '\n';
#endif
	return (s - buf);
}

static ssize_t state_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t n)
{
#if 1	// added by peres to show valid current state
	const char *b = buf;
#endif
#ifdef CONFIG_SUSPEND
#ifdef CONFIG_EARLYSUSPEND
	suspend_state_t state = PM_SUSPEND_ON;
#else
	suspend_state_t state = PM_SUSPEND_STANDBY;
#endif
	const char * const *s;
#endif
	char *p;
	int len;
	int error = -EINVAL;

	p = memchr(buf, '\n', n);
	len = p ? p - buf : n;

	/* First, check if we are requested to hibernate */
	if (len == 4 && !strncmp(buf, "disk", len)) {
		error = hibernate();
  goto Exit;
	}

#ifdef CONFIG_SUSPEND
	for (s = &pm_states[state]; state < PM_SUSPEND_MAX; s++, state++) {
		if (*s && len == strlen(*s) && !strncmp(buf, *s, len))
			break;
	}
	if (state < PM_SUSPEND_MAX && *s) {
		printk(KERN_ERR "%s: state:%d (%s)\n", __func__, state, *s);
#ifdef CONFIG_EARLYSUSPEND
		if (state == PM_SUSPEND_ON || valid_state(state)) {
#if 1	// added by peres to show valid current state
			if(state == PM_SUSPEND_ON) {
				if (ftm_sleep == 1) {
					pr_info("%s: wake lock for FTM\n", __func__);
					ftm_sleep = 0;
					wake_lock_timeout(&ftm_wake_lock, 60 * HZ);
					if (ftm_enable_usb_sw)
						ftm_enable_usb_sw(1);
				}
				sprintf(b,"%s ", pm_states[PM_SUSPEND_ON]);
				global_state = PM_SUSPEND_ON;
			} else {
				if (ftm_sleep == 1) { // when ftm sleep cmd 
					if (ftm_enable_usb_sw)
						ftm_enable_usb_sw(0);
				}
				sprintf(b,"%s ", pm_states[PM_SUSPEND_MEM]);
				global_state = PM_SUSPEND_MEM;
			}
#endif
			error = 0;
			request_suspend_state(state);
#ifdef FEATURE_FTM_SLEEP
			if (ftm_sleep && global_state == PM_SUSPEND_MEM) {
				wakelock_force_suspend();
			}
#endif /* FEATURE_FTM_SLEEP */
		}
#else
		error = enter_state(state);
#endif
	}
#endif

 Exit:
	return error ? error : n;
}

power_attr(state);
#ifdef FEATURE_FTM_SLEEP /* for Factory Sleep cmd check */


#define ftm_attr(_name) \
static struct kobj_attribute _name##_attr = {	\
	.attr	= {				\
		.name = __stringify(_name),	\
		.mode = 0777,			\
	},					\
	.show	= _name##_show,			\
	.store	= _name##_store,		\
}


static ssize_t ftm_sleep_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	char *s = buf;
#ifdef CONFIG_SUSPEND
	switch(ftm_sleep) {
		case 0 :
			s += sprintf(s,"%d ", 0);
			break;
		case 1 :
			s += sprintf(s,"%d ", 1);
			break;
	}
#endif

	if (s != buf)
		/* convert the last space to a newline */
		*(s-1) = '\n';

	return (s - buf);
}


static ssize_t ftm_sleep_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t n)
{
	ssize_t ret = -EINVAL;
	char *after;
	unsigned char state = (unsigned char) simple_strtoul(buf, &after, 10);

	ftm_sleep = state;
        
	return ret;

}

ftm_attr(ftm_sleep);
#endif /* FEATURE_FTM_SLEEP */

#ifdef CONFIG_PM_TRACE
int pm_trace_enabled;

static ssize_t pm_trace_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	return sprintf(buf, "%d\n", pm_trace_enabled);
}

static ssize_t
pm_trace_store(struct kobject *kobj, struct kobj_attribute *attr,
	       const char *buf, size_t n)
{
	int val;

	if (sscanf(buf, "%d", &val) == 1) {
		pm_trace_enabled = !!val;
		return n;
	}
	return -EINVAL;
}

power_attr(pm_trace);
#endif /* CONFIG_PM_TRACE */

#ifdef CONFIG_USER_WAKELOCK
power_attr(wake_lock);
power_attr(wake_unlock);
#endif

static struct attribute * g[] = {
	&state_attr.attr,
#ifdef CONFIG_PM_TRACE
	&pm_trace_attr.attr,
#endif
#if defined(CONFIG_PM_SLEEP) && defined(CONFIG_PM_DEBUG)
	&pm_test_attr.attr,
#endif
#ifdef CONFIG_USER_WAKELOCK
	&wake_lock_attr.attr,
	&wake_unlock_attr.attr,
#endif
	&ftm_sleep_attr.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = g,
};

#ifdef CONFIG_PM_RUNTIME
struct workqueue_struct *pm_wq;

static int __init pm_start_workqueue(void)
{
	pm_wq = create_freezeable_workqueue("pm");

	return pm_wq ? 0 : -ENOMEM;
}
#else
static inline int pm_start_workqueue(void) { return 0; }
#endif

static int __init pm_init(void)
{
	int error = pm_start_workqueue();
	if (error)
		return error;
	power_kobj = kobject_create_and_add("power", NULL);
	if (!power_kobj)
		return -ENOMEM;
	wake_lock_init(&ftm_wake_lock, WAKE_LOCK_SUSPEND, "ftm_wake_lock");
	return sysfs_create_group(power_kobj, &attr_group);
}

core_initcall(pm_init);
