/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"

/* Forward declarations */
int aesd_open(struct inode *inode, struct file *filp);
int aesd_release(struct inode *inode, struct file *filp);
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
int aesd_init_module(void);
void aesd_cleanup_module(void);

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Fusen He"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    struct aesd_dev *dev;

    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */
    size_t total_size = 0;
    uint8_t index;
    struct aesd_buffer_entry *entry;
    size_t bytes_to_copy;
    char *user_buf_pos = buf;
    size_t bytes_remaining = count;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    /* Calculate total size of all entries in circular buffer */
    if (dev->circular_buffer.full) {
        /* Buffer is full, iterate through all entries starting from out_offs */
        uint8_t current_idx = dev->circular_buffer.out_offs;
        for (index = 0; index < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; index++) {
            total_size += dev->circular_buffer.entry[current_idx].size;
            current_idx = (current_idx + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        }
    } else {
        /* Buffer not full, only count entries up to in_offs */
        for (index = 0; index < dev->circular_buffer.in_offs; index++) {
            total_size += dev->circular_buffer.entry[index].size;
        }
    }

    /* Return 0 if we've already read past all data or buffer is empty */
    if (*f_pos >= total_size) {
        goto out;
    }

    /* Copy data from circular buffer, handling offset */
    size_t current_offset = 0;
    uint8_t start_idx = dev->circular_buffer.full ? dev->circular_buffer.out_offs : 0;
    uint8_t num_entries = dev->circular_buffer.full ? AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED : dev->circular_buffer.in_offs;
    uint8_t current_idx = start_idx;

    for (index = 0; index < num_entries && bytes_remaining > 0; index++) {
        entry = &dev->circular_buffer.entry[current_idx];

        if (current_offset + entry->size > *f_pos) {
            /* This entry contains data we need to read */
            size_t entry_start_offset = (*f_pos > current_offset) ? (*f_pos - current_offset) : 0;
            bytes_to_copy = min(bytes_remaining, (size_t)(entry->size - entry_start_offset));

            if (copy_to_user(user_buf_pos, entry->buffptr + entry_start_offset, bytes_to_copy)) {
                retval = -EFAULT;
                goto out;
            }

            user_buf_pos += bytes_to_copy;
            bytes_remaining -= bytes_to_copy;
            *f_pos += bytes_to_copy;
            retval += bytes_to_copy;
        }

        current_offset += entry->size;
        current_idx = (current_idx + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

out:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */
    char *kernel_buf;
    char *newline_pos;
    size_t bytes_before_newline;
    struct aesd_buffer_entry *old_entry;

    /* Copy data from user space */
    kernel_buf = kmalloc(count, GFP_KERNEL);
    if (!kernel_buf) {
        return -ENOMEM;
    }

    if (copy_from_user(kernel_buf, buf, count)) {
        kfree(kernel_buf);
        return -EFAULT;
    }

    if (mutex_lock_interruptible(&dev->lock)) {
        kfree(kernel_buf);
        return -ERESTARTSYS;
    }

    /* Check if this write contains a newline */
    newline_pos = memchr(kernel_buf, '\n', count);

    if (newline_pos == NULL) {
        /* No newline - append to partial buffer */
        size_t new_size = dev->partial_write_size + count;
        char *new_buffer = krealloc(dev->partial_write_buffer, new_size, GFP_KERNEL);
        if (!new_buffer) {
            kfree(kernel_buf);
            mutex_unlock(&dev->lock);
            return -ENOMEM;
        }
        memcpy(new_buffer + dev->partial_write_size, kernel_buf, count);
        dev->partial_write_buffer = new_buffer;
        dev->partial_write_size = new_size;
        kfree(kernel_buf);
        mutex_unlock(&dev->lock);
        return count;
    }

    /* Newline found - complete the write */
    bytes_before_newline = newline_pos - kernel_buf + 1; /* Include the newline */

    if (dev->partial_write_size > 0) {
        /* Combine partial buffer with current write up to newline */
        size_t total_size = dev->partial_write_size + bytes_before_newline;
        char *combined_buf = kmalloc(total_size, GFP_KERNEL);
        if (!combined_buf) {
            kfree(kernel_buf);
            mutex_unlock(&dev->lock);
            return -ENOMEM;
        }
        memcpy(combined_buf, dev->partial_write_buffer, dev->partial_write_size);
        memcpy(combined_buf + dev->partial_write_size, kernel_buf, bytes_before_newline);

        /* Free old partial buffer */
        kfree(dev->partial_write_buffer);
        dev->partial_write_buffer = NULL;
        dev->partial_write_size = 0;
        dev->partial_write_capacity = 0;

        /* Free the oldest entry if buffer is full (will be overwritten) */
        if (dev->circular_buffer.full) {
            old_entry = &dev->circular_buffer.entry[dev->circular_buffer.out_offs];
            if (old_entry->buffptr != NULL) {
                kfree((char *)old_entry->buffptr);
                old_entry->buffptr = NULL;
                old_entry->size = 0;
            }
        }

        /* Add combined entry to circular buffer */
        struct aesd_buffer_entry entry;
        entry.buffptr = combined_buf;
        entry.size = total_size;
        aesd_circular_buffer_add_entry(&dev->circular_buffer, &entry);
        kfree(kernel_buf);
    } else {
        /* No partial buffer, just add this write up to newline */
        char *entry_buf = kmalloc(bytes_before_newline, GFP_KERNEL);
        if (!entry_buf) {
            kfree(kernel_buf);
            mutex_unlock(&dev->lock);
            return -ENOMEM;
        }
        memcpy(entry_buf, kernel_buf, bytes_before_newline);

        /* Free the oldest entry if buffer is full (will be overwritten) */
        if (dev->circular_buffer.full) {
            old_entry = &dev->circular_buffer.entry[dev->circular_buffer.out_offs];
            if (old_entry->buffptr != NULL) {
                kfree((char *)old_entry->buffptr);
                old_entry->buffptr = NULL;
                old_entry->size = 0;
            }
        }

        struct aesd_buffer_entry entry;
        entry.buffptr = entry_buf;
        entry.size = bytes_before_newline;
        aesd_circular_buffer_add_entry(&dev->circular_buffer, &entry);
        kfree(kernel_buf);
    }

    mutex_unlock(&dev->lock);
    retval = count;
    return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    aesd_circular_buffer_init(&aesd_device.circular_buffer);
    mutex_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific portions here as necessary
     */
    uint8_t index;
    struct aesd_buffer_entry *entry;

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.circular_buffer, index) {
        if (entry->buffptr != NULL) {
            kfree((char *)entry->buffptr);
            entry->buffptr = NULL;
        }
    }

    /* Free partial write buffer if it exists */
    if (aesd_device.partial_write_buffer != NULL) {
        kfree(aesd_device.partial_write_buffer);
        aesd_device.partial_write_buffer = NULL;
    }

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
