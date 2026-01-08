#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/delay.h>

#define DRIVER_NAME "bh1750_driver"
#define BH1750_ADDR 0x23 // Địa chỉ I2C mặc định (nếu chân ADDR nối đất)
#define I2C_BUS_NUM 1    // Raspberry Pi 4 dùng I2C-1 (Pin 3, 5)

// BH1750 Instructions
#define POWER_ON 0x01
#define RESET 0x07
#define CONTINUOUS_HIGH_RES_MODE 0x10

static struct i2c_adapter *bh1750_adapter = NULL;
static struct i2c_client *bh1750_client = NULL;

static int major_number;
static struct class *bh1750_class = NULL;
static struct device *bh1750_device = NULL;

// Hàm gửi lệnh xuống cảm biến
static int bh1750_write_cmd(struct i2c_client *client, u8 cmd) {
    return i2c_master_send(client, &cmd, 1);
}

// Hàm đọc Lux từ cảm biến
static int bh1750_read_lux(u16 *raw_val) {
    u8 buf[2];
    int ret;
    
    if (!bh1750_client) return -ENODEV;

    // Đọc 2 byte dữ liệu
    ret = i2c_master_recv(bh1750_client, buf, 2);
    if (ret < 0) return ret;
    
    // Công thức: Lux = (High_Byte << 8 | Low_Byte) / 1.2
    *raw_val = ((buf[0] << 8) | buf[1]); 
    return 0;
}

// Hàm được gọi khi user cat /dev/bh1750
static ssize_t dev_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos) {
    u16 raw = 0;
    u32 int_part, dec_part;
    char out_buf[32];
    int len;

    if (*ppos > 0) return 0; // EOF

    if (bh1750_read_lux(&raw) < 0) {
        return -EFAULT;
    }

    int_part = (raw * 10) / 12;
    dec_part = ((raw * 10) % 12) * 100 / 12;

    len = sprintf(out_buf, "%d.%02d\n", int_part, dec_part);
    if (copy_to_user(user_buf, out_buf, len)) return -EFAULT;

    *ppos += len;
    return len;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = dev_read,
};

static int __init bh1750_init(void) {
    int ret;
    // 1. Đăng ký Char Device
    major_number = register_chrdev(0, DRIVER_NAME, &fops);
    if (major_number < 0) return major_number;

    bh1750_class = class_create("bh1750_class");
    bh1750_device = device_create(bh1750_class, NULL, MKDEV(major_number, 0), NULL, "bh1750");

    // 2. Kết nối I2C Thủ công (Không cần Device Tree)
    // Lấy Adapter I2C số 1
    bh1750_adapter = i2c_get_adapter(I2C_BUS_NUM);
    if (!bh1750_adapter) {
        printk(KERN_ERR "BH1750: Cannot get I2C adapter %d\n", I2C_BUS_NUM);
        return -ENODEV;
    }

    // Tạo Client giả lập
    struct i2c_board_info board_info = {
        I2C_BOARD_INFO("bh1750", BH1750_ADDR)
    };
    bh1750_client = i2c_new_client_device(bh1750_adapter, &board_info);

    if (!bh1750_client) {
        printk(KERN_ERR "BH1750: Cannot create I2C client\n");
        return -ENODEV;
    }

    // 3. Khởi động cảm biến
    ret = bh1750_write_cmd(bh1750_client, POWER_ON);
    if (ret < 0) {
        printk(KERN_ERR "BH1750: Failed to send POWER_ON command. Error: %d\n", ret);
        // Dọn dẹp nếu thất bại
        i2c_unregister_device(bh1750_client);
        device_destroy(bh1750_class, MKDEV(major_number, 0));
        class_destroy(bh1750_class);
        unregister_chrdev(major_number, DRIVER_NAME);
        return ret; // Trả về lỗi để modprobe biết mà báo failed
    }
    msleep(10);

    ret = bh1750_write_cmd(bh1750_client, RESET);
    if (ret != 1) {
        printk(KERN_ERR "BH1750: Failed Reset. Ret: %d\n", ret);
        goto i2c_error;
    }
    msleep(10);

    // Gửi lệnh đo liên tục
    ret = bh1750_write_cmd(bh1750_client, CONTINUOUS_HIGH_RES_MODE);
    if (ret < 0) {
        printk(KERN_ERR "BH1750: Failed to set measure mode. Error: %d\n", ret);
        // Dọn dẹp tương tự như trên...
        return ret;
    }
    // Chờ 180ms cho lần đo đầu tiên (theo datasheet)
    msleep(180);
    printk(KERN_INFO "BH1750: Driver loaded [TEST V2] at /dev/bh1750\n");
    return 0;
i2c_error:
    // Dọn dẹp nếu thất bại
    i2c_unregister_device(bh1750_client);
    device_destroy(bh1750_class, MKDEV(major_number, 0));
    class_destroy(bh1750_class);
    unregister_chrdev(major_number, DRIVER_NAME);
    return -EIO;
}

static void __exit bh1750_exit(void) {
    if (bh1750_client) i2c_unregister_device(bh1750_client);
    if (bh1750_adapter) i2c_put_adapter(bh1750_adapter);
    
    device_destroy(bh1750_class, MKDEV(major_number, 0));
    class_destroy(bh1750_class);
    unregister_chrdev(major_number, DRIVER_NAME);
    printk(KERN_INFO "BH1750: Driver unloaded\n");
}

module_init(bh1750_init);
module_exit(bh1750_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("ThaiSon");
