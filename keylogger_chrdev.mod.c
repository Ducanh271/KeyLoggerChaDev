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
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0xfe487975, "init_wait_entry" },
	{ 0x1000e51, "schedule" },
	{ 0x8c26d495, "prepare_to_wait_event" },
	{ 0x92540fbf, "finish_wait" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0xe2964344, "__wake_up" },
	{ 0xda9f3b5, "kernel_write" },
	{ 0xecb37206, "filp_open" },
	{ 0xe3ec2f2b, "alloc_chrdev_region" },
	{ 0xa304a8e0, "cdev_init" },
	{ 0xb51d50, "cdev_add" },
	{ 0x33c47e0e, "class_create" },
	{ 0xf641a686, "device_create" },
	{ 0x92d5838e, "request_threaded_irq" },
	{ 0x122c3a7e, "_printk" },
	{ 0xfa5cdaeb, "class_destroy" },
	{ 0xc5442e07, "cdev_del" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0xc1514a3b, "free_irq" },
	{ 0x2f2c95c4, "flush_work" },
	{ 0xc21bdf9, "device_destroy" },
	{ 0xb41688c, "filp_close" },
	{ 0x2d3385d3, "system_wq" },
	{ 0xc5b6f236, "queue_work_on" },
	{ 0x54b1fac6, "__ubsan_handle_load_invalid_value" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0xe2c17b5d, "__SCT__might_resched" },
	{ 0x34db050b, "_raw_spin_lock_irqsave" },
	{ 0xc3aaf0a9, "__put_user_1" },
	{ 0xd35cce70, "_raw_spin_unlock_irqrestore" },
	{ 0xb2b23fc2, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "1B4C7A675A22A1F15208452");
