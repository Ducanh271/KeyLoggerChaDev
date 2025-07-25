#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

#ifdef CONFIG_UNWINDER_ORC
#include <asm/orc_header.h>
ORC_HEADER;
#endif

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xb51d50, "cdev_add" },
	{ 0x33c47e0e, "class_create" },
	{ 0xf641a686, "device_create" },
	{ 0x96554810, "register_keyboard_notifier" },
	{ 0x122c3a7e, "_printk" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0xc5442e07, "cdev_del" },
	{ 0x82ee90dc, "timer_delete_sync" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0xc21bdf9, "device_destroy" },
	{ 0xfa5cdaeb, "class_destroy" },
	{ 0x9ed554b3, "unregister_keyboard_notifier" },
	{ 0x2f2c95c4, "flush_work" },
	{ 0x34db050b, "_raw_spin_lock_irqsave" },
	{ 0xd35cce70, "_raw_spin_unlock_irqrestore" },
	{ 0xe2964344, "__wake_up" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0xe2c17b5d, "__SCT__might_resched" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0x6b10bee1, "_copy_to_user" },
	{ 0xfe487975, "init_wait_entry" },
	{ 0x1000e51, "schedule" },
	{ 0x8c26d495, "prepare_to_wait_event" },
	{ 0x92540fbf, "finish_wait" },
	{ 0x7682ba4e, "__copy_overflow" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x2d3385d3, "system_wq" },
	{ 0xc5b6f236, "queue_work_on" },
	{ 0x54b1fac6, "__ubsan_handle_load_invalid_value" },
	{ 0x9ec6ca96, "ktime_get_real_ts64" },
	{ 0xfff5afc, "time64_to_tm" },
	{ 0x656e4a6e, "snprintf" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0xc6f46339, "init_timer_key" },
	{ 0x15ba50a6, "jiffies" },
	{ 0xc38c83b8, "mod_timer" },
	{ 0xe3ec2f2b, "alloc_chrdev_region" },
	{ 0xa304a8e0, "cdev_init" },
	{ 0xb2b23fc2, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "962005C6E3AF7E6A20E53E6");
