#include "driver.h"
#include "uapi.h"

/*
 * QEMU GPGPU 的 guest 侧最小 PCI 驱动。
 *
 * 驱动负责把 QEMU 暴露出的 PCI 设备接入 guest Linux：
 *   - probe/remove 管理 PCI 设备生命周期；
 *   - /dev/gpgpu0 提供用户态入口；
 *   - ioctl 作为控制通道；
 *   - mmap 将 BAR2/VRAM 暴露给 runtime 作为数据通道。
 */

#include <asm-generic/errno-base.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/types.h>


#define GPGPU_DRV_NAME "gpgpu"

static dev_t gpgpu_devt;
static struct class *gpgpu_class;

/* BAR0 控制寄存器访问封装。 */
static inline u32 gpgpu_readl(struct gpgpu_dev *g, u32 off){
    return ioread32(g->bar0 + off);
}

static inline void gpgpu_writel(struct gpgpu_dev *g, u32 off, u32 val){
    return iowrite32(val, g->bar0 + off);
}

/*
 * 将一次 runtime launch 请求翻译成设备寄存器写入。
 *
 * kernel_addr/args_addr 是 VRAM 偏移。写完 grid/block 配置后，
 * 最后写 DISPATCH，QEMU 设备模型会在该写入处触发 kernel 执行。
 */
static int gpgpu_launch(struct gpgpu_dev *g,
                        const struct gpgpu_kernel_params *p)
{
    gpgpu_writel(g, GPGPU_REG_GLOBAL_CTRL, GPGPU_CTRL_ENABLE);

    gpgpu_writel(g, GPGPU_REG_KERNEL_ADDR_LO, lower_32_bits(p->kernel_addr));
    gpgpu_writel(g, GPGPU_REG_KERNEL_ADDR_HI, upper_32_bits(p->kernel_addr));

    gpgpu_writel(g, GPGPU_REG_KERNEL_ARGS_LO, lower_32_bits(p->args_addr));
    gpgpu_writel(g, GPGPU_REG_KERNEL_ARGS_HI, upper_32_bits(p->args_addr));

    gpgpu_writel(g, GPGPU_REG_GRID_DIM_X, p->grid_dim[0]);
    gpgpu_writel(g, GPGPU_REG_GRID_DIM_Y, p->grid_dim[1]);
    gpgpu_writel(g, GPGPU_REG_GRID_DIM_Z, p->grid_dim[2]);

    gpgpu_writel(g, GPGPU_REG_BLOCK_DIM_X, p->block_dim[0]);
    gpgpu_writel(g, GPGPU_REG_BLOCK_DIM_Y, p->block_dim[1]);
    gpgpu_writel(g, GPGPU_REG_BLOCK_DIM_Z, p->block_dim[2]);

    gpgpu_writel(g, GPGPU_REG_SHARED_MEM_SIZE, p->shared_mem);

    gpgpu_writel(g, GPGPU_REG_DISPATCH, 1);

    return 0;
}

static const struct file_operations gpgpu_fops;

/*
 * PCI probe 入口。
 *
 * Linux PCI 子系统在发现 0x1234:0x1337 并匹配本驱动后调用这里。
 * probe 负责启用设备、申请 BAR、映射 BAR0、记录 BAR2，并创建
 * /dev/gpgpu0 供 runtime 打开。
 */
static int gpgpu_probe(struct pci_dev *pdev,
                       const struct pci_device_id *id){
    struct gpgpu_dev *g;
    int ret;

    g = devm_kzalloc(&pdev->dev, sizeof(*g), GFP_KERNEL);
    if(!g){
        return -ENOMEM;
    }

    ret = pci_enable_device(pdev);
    if(ret){
        return ret;
    }

    ret = pci_request_regions(pdev, GPGPU_DRV_NAME);
    if (ret) {
        pci_disable_device(pdev);
        return ret;
    }

    g->bar0 = pci_iomap(pdev, GPGPU_BAR_CTRL, 0);
    if (!g->bar0) {
        ret = -ENOMEM;
        goto err_regions;
    }

    g->pdev = pdev;
    g->vram_phys = pci_resource_start(pdev, GPGPU_BAR_VRAM);
    g->vram_len = pci_resource_len(pdev, GPGPU_BAR_VRAM);
    g->devt = gpgpu_devt;

    cdev_init(&g->cdev, &gpgpu_fops);
    g->cdev.owner = THIS_MODULE;

    ret = cdev_add(&g->cdev, g->devt, 1);
    if (ret) {
        goto err_iounmap;
    }

    g->device = device_create(gpgpu_class, &pdev->dev,
                              g->devt, g, "gpgpu0");
    if (IS_ERR(g->device)) {
        ret = PTR_ERR(g->device);
        goto err_cdev;
    }

    pci_set_drvdata(pdev, g);

    dev_info(&pdev->dev, "gpgpu ready: vram=%pa size=%pa\n",
             &g->vram_phys, &g->vram_len);

    return 0;

err_cdev:
    cdev_del(&g->cdev);
err_iounmap:
    pci_iounmap(pdev, g->bar0);
err_regions:
    pci_release_regions(pdev);
    pci_disable_device(pdev);
    return ret;
}

/* remove 是 probe 的反向路径，在 rmmod 或设备解绑时释放资源。 */
static void gpgpu_remove(struct pci_dev *pdev){
    struct gpgpu_dev *g = pci_get_drvdata(pdev);

    device_destroy(gpgpu_class, g->devt);
    cdev_del(&g->cdev);
    pci_iounmap(pdev, g->bar0);
    pci_release_regions(pdev);
    pci_disable_device(pdev);
}



/*
 * 字符设备 open 入口。
 *
 * 通过 inode->i_cdev 找回外层 gpgpu_dev，并保存到 filp->private_data。
 * 后续 ioctl/mmap 都从 private_data 取得设备上下文。
 */
static int gpgpu_open(struct inode *inode, struct file *filp){
    struct gpgpu_dev *g;

    g = container_of(inode->i_cdev, struct gpgpu_dev, cdev);
    filp->private_data = g;

    return 0;
}


/*
 * 控制通道入口。
 *
 * GET_INFO 读取 BAR0 中的只读信息寄存器并复制回用户态；
 * LAUNCH 从用户态复制 launch 描述符，并交给 gpgpu_launch() 写寄存器。
 */
static long gpgpu_ioctl(struct file *filp,
                        unsigned int cmd,
                        unsigned long arg){
    struct gpgpu_dev *g = filp->private_data;

    switch (cmd) {
        case GPGPU_IOCTL_GET_INFO:{
            struct gpgpu_info info;

            info.dev_id = gpgpu_readl(g, GPGPU_REG_DEV_ID);
            info.version = gpgpu_readl(g, GPGPU_REG_DEV_VERSION);
            info.vram_size = ((u64)gpgpu_readl(g, GPGPU_REG_VRAM_SIZE_HI) << 32) | gpgpu_readl(g, GPGPU_REG_VRAM_SIZE_LO);

            if(copy_to_user((void __user *)arg, &info, sizeof(info))){
                return  -EFAULT;
                }
                return 0;
            }

        case GPGPU_IOCTL_LAUNCH:{
            struct gpgpu_kernel_params params;

            if(copy_from_user(&params, (void __user *)arg, sizeof(params))){
                return -EFAULT;
            }

            return gpgpu_launch(g, &params);
            }

        default:
            return  -ENOTTY;
    }
    

}

/*
 * 数据通道入口。
 *
 * runtime 对 /dev/gpgpu0 调用 mmap 时，驱动将 BAR2/VRAM 的 guest
 * 物理地址区间映射到用户进程。用户态之后通过普通 load/store 或
 * memcpy 访问 dev->vram，就等价于访问设备 VRAM。
 */
static int gpgpu_mmap(struct file *filp, struct vm_area_struct *vma)
{
    struct gpgpu_dev *g = filp->private_data;
    unsigned long size = vma->vm_end - vma->vm_start;
    unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
    resource_size_t phys;

    if (offset + size > g->vram_len) {
        return -EINVAL;
    }

    phys = g->vram_phys + offset;

    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

    return io_remap_pfn_range(vma,
                              vma->vm_start,
                              phys >> PAGE_SHIFT,
                              size,
                              vma->vm_page_prot);
}

/* /dev/gpgpu0 支持的用户态操作集合。 */
static const struct file_operations gpgpu_fops = {
    .owner = THIS_MODULE,
    .open = gpgpu_open,
    .unlocked_ioctl = gpgpu_ioctl,
    .mmap = gpgpu_mmap,
#ifdef CONFIG_COMPAT
    .compat_ioctl = gpgpu_ioctl,
#endif
};

/* PCI ID 表：声明本驱动绑定 QEMU GPGPU 虚拟设备。 */
static const struct pci_device_id gpgpu_ids[] = {
    { PCI_DEVICE(GPGPU_PCI_VENDOR_ID, GPGPU_PCI_DEVICE_ID) },
    { 0, }
};

MODULE_DEVICE_TABLE(pci, gpgpu_ids);

/* 注册给 Linux PCI 子系统的驱动对象。 */
static struct pci_driver gpgpu_pci_driver = {
    .name = GPGPU_DRV_NAME,
    .id_table = gpgpu_ids,
    .probe = gpgpu_probe,
    .remove = gpgpu_remove,
};

/* 模块加载入口：先创建字符设备号和 class，再注册 PCI driver。 */
static int __init gpgpu_init(void)
{
    int ret;

    ret = alloc_chrdev_region(&gpgpu_devt, 0, 1, GPGPU_DRV_NAME);
    if (ret) {
        return ret;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    gpgpu_class = class_create(GPGPU_DRV_NAME);
#else
    gpgpu_class = class_create(THIS_MODULE, GPGPU_DRV_NAME);
#endif

    if (IS_ERR(gpgpu_class)) {
        ret = PTR_ERR(gpgpu_class);
        unregister_chrdev_region(gpgpu_devt, 1);
        return ret;
    }

    ret = pci_register_driver(&gpgpu_pci_driver);
    if (ret) {
        class_destroy(gpgpu_class);
        unregister_chrdev_region(gpgpu_devt, 1);
        return ret;
    }

    return 0;
}

/* 模块卸载入口：注销顺序与 init 相反。 */
static void __exit gpgpu_exit(void)
{
    pci_unregister_driver(&gpgpu_pci_driver);
    class_destroy(gpgpu_class);
    unregister_chrdev_region(gpgpu_devt, 1);
}

module_init(gpgpu_init);
module_exit(gpgpu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Old");
MODULE_DESCRIPTION("Simple GPGPU PCI driver");
