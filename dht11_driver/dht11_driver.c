#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/interrupt.h>

#define DEVICE_NAME "dht11"
#define GPIO_PIN 538  // Dùng GPIO 26 (Pin 37) để tránh xung đột SPI/I2C

static int major_number;
static struct class *dht11_class = NULL;
static struct device *dht11_device = NULL;

// --- HÀM HỖ TRỢ (Mô phỏng logic của BBB) ---

// Chờ chân GPIO chuyển sang trạng thái mong muốn (expected)
// Trả về 0 nếu OK, -1 nếu timeout
static int wait_for_state(int expected, int timeout_us) {
    int waited = 0;
    while (gpio_get_value(GPIO_PIN) != expected) {
        udelay(1);
        waited++;
        if (waited > timeout_us) return -1;
    }
    return 0;
}

// Hàm đọc dữ liệu chính
static int read_dht11_data(u8 *h_int, u8 *h_dec, u8 *t_int, u8 *t_dec) {
    u8 bits[5] = {0};
    int i, j, ret;
    unsigned long flags;

    // 1. Gửi tín hiệu Start (Host kéo thấp 20ms)
    gpio_direction_output(GPIO_PIN, 0);
    mdelay(20);
    gpio_set_value(GPIO_PIN, 1);
    udelay(30);
    gpio_direction_input(GPIO_PIN);

    // --- BẮT ĐẦU ĐOẠN QUAN TRỌNG (Tắt ngắt hệ thống) ---
    // Raspberry Pi chạy Linux đa nhiệm, nếu không tắt ngắt,
    // hệ điều hành sẽ chen ngang làm sai lệch thời gian đọc micro giây.
    local_irq_save(flags);

    // 2. Chờ Sensor phản hồi (Start sequence)
    // Sensor kéo thấp 80us
    if ((ret = wait_for_state(0, 100)) < 0) {
        local_irq_restore(flags);
        return -1; // Timeout wait start low
    }
    // Sensor kéo cao 80us
    if ((ret = wait_for_state(1, 100)) < 0) {
        local_irq_restore(flags);
        return -2; // Timeout wait start high
    }
    // Sensor bắt đầu gửi bit (kéo thấp 50us)
    if ((ret = wait_for_state(0, 100)) < 0) {
        local_irq_restore(flags);
        return -3; // Timeout wait first bit
    }

    // 3. Đọc 40 bits (5 bytes)
    for (j = 0; j < 5; j++) {
        for (i = 0; i < 8; i++) {
            // Chờ cạnh lên (bắt đầu bit data)
            if ((ret = wait_for_state(1, 100)) < 0) {
                local_irq_restore(flags);
                return -4;
            }
            
            // Logic phân biệt 0 và 1:
            // Bit 0: High ~26-28us
            // Bit 1: High ~70us
            // Ta đợi 40us rồi kiểm tra. Nếu vẫn High -> Là bit 1.
            udelay(40);
            
            if (gpio_get_value(GPIO_PIN)) {
                bits[j] |= (1 << (7 - i));
                // Chờ cho chân xuống Low trở lại để đón bit tiếp theo
                if ((ret = wait_for_state(0, 100)) < 0) {
                    local_irq_restore(flags);
                    return -5;
                }
            }
            // Nếu gpio == 0 thì là bit 0, vòng lặp tự quay lại chờ cạnh lên tiếp theo
        }
    }

    local_irq_restore(flags);
    // --- KẾT THÚC ĐOẠN QUAN TRỌNG ---

    // 4. Kiểm tra Checksum
    if ((bits[0] + bits[1] + bits[2] + bits[3]) == bits[4]) {
        *h_int = bits[0];
        *h_dec = bits[1]; 
        *t_int = bits[2]; 
        *t_dec = bits[3];
        return 0; // Success
    }
    
    return -10; // Checksum error
}

static ssize_t dht11_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos) {
    u8 hi = 0, hd = 0, ti = 0, td = 0;
    char result_buf[64];
    int ret, len;

    if (*ppos > 0) return 0;

    ret = read_dht11_data(&hi, &hd, &ti, &td);
    
    if (ret == 0) {
        len = sprintf(result_buf, "Temp: %d.%d C, Hum: %d.%d %%\n", ti, td, hi, hd);
    } else {
        len = sprintf(result_buf, "Error reading DHT11: Code %d\n", ret);
    }

    if (copy_to_user(user_buf, result_buf, len)) return -EFAULT;
    *ppos += len;
    return len;
}

static int dht11_open(struct inode *inode, struct file *file) { return 0; }
static int dht11_release(struct inode *inode, struct file *file) { return 0; }

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = dht11_read,
    .open = dht11_open,
    .release = dht11_release,
};

static int __init dht11_init(void) {
    int ret;
    
    // 1. Request GPIO - Bước quan trọng để tránh lỗi Busy
    if (!gpio_is_valid(GPIO_PIN)) {
        printk(KERN_ERR "DHT11: Invalid GPIO %d\n", GPIO_PIN);
        return -ENODEV;
    }
    
    // Request GPIO và set luôn giá trị mặc định là INPUT
    ret = gpio_request_one(GPIO_PIN, GPIOF_IN, "DHT11_Sensor");
    if (ret) {
        printk(KERN_ERR "DHT11: Cannot request GPIO %d. Error: %d\n", GPIO_PIN, ret);
        return ret; // Trả về lỗi để insmod thất bại (đúng chuẩn)
    }

    // 2. Register Device
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        gpio_free(GPIO_PIN);
        return major_number;
    }

    dht11_class = class_create("dht11_class");
    if (IS_ERR(dht11_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        gpio_free(GPIO_PIN);
        return PTR_ERR(dht11_class);
    }

    dht11_device = device_create(dht11_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(dht11_device)) {
        class_destroy(dht11_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        gpio_free(GPIO_PIN);
        return PTR_ERR(dht11_device);
    }

    printk(KERN_INFO "DHT11: Driver loaded on GPIO %d (Standard Logic)\n", GPIO_PIN);
    return 0;
}

static void __exit dht11_exit(void) {
    device_destroy(dht11_class, MKDEV(major_number, 0));
    class_destroy(dht11_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    
    // QUAN TRỌNG: Giải phóng GPIO để lần sau nạp không bị báo Busy
    gpio_free(GPIO_PIN);
    printk(KERN_INFO "DHT11: Driver unloaded\n");
}

module_init(dht11_init);
module_exit(dht11_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Adapted for RPi");
