/* 
 * Encoder device driver (kernel module)
 *
 * Copyright (C) 2012 Google Finland Oy.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
--------------------------------------------------------------------------------
--
--  Abstract : 6280/7280/8270/8290 Encoder device driver (kernel module)
--
------------------------------------------------------------------------------*/

#include <linux/kernel.h>
#include <linux/module.h>
/* needed for __init,__exit directives */
#include <linux/init.h>
/* needed for remap_page_range 
	SetPageReserved
	ClearPageReserved
*/
#include <linux/mm.h>
/* obviously, for kmalloc */
#include <linux/slab.h>
/* for struct file_operations, register_chrdev() */
#include <linux/fs.h>
/* standard error codes */
#include <linux/errno.h>

#include <linux/moduleparam.h>
/* request_irq(), free_irq() */
#include <linux/interrupt.h>

/* needed for virt_to_phys() */
#include <asm/io.h>
#include <linux/pci.h>
#include <asm/uaccess.h>
#include <linux/ioport.h>

#include <asm/irq.h>

#include <linux/version.h>

#include <linux/signal.h> /* SDD added for kernel 2.6.32*/

/* our own stuff */
#include "hx280enc.h"

/* module description */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Google Finland Oy");
MODULE_DESCRIPTION("Hantro 6280/7280/8270/8290 Encoder driver");

/* this is ARM Integrator specific stuff */
#define INTEGRATOR_LOGIC_MODULE0_BASE   0xEBC00000 /*SPEAr1340*/
/*
#define INTEGRATOR_LOGIC_MODULE1_BASE   0xD0000000
#define INTEGRATOR_LOGIC_MODULE2_BASE   0xE0000000
#define INTEGRATOR_LOGIC_MODULE3_BASE   0xF0000000
*/

#define VP_PB_INT_LT                    114 /*SPEAr1340*/
/*
#define INT_EXPINT1                     10
#define INT_EXPINT2                     11
#define INT_EXPINT3                     12
*/
/* these could be module params in the future */

#define ENC_IO_BASE                 INTEGRATOR_LOGIC_MODULE0_BASE
#define ENC_IO_SIZE                 (96 * 4)    /* bytes */

#define ENC_HW_ID1                  0x62800000
#define ENC_HW_ID2                  0x72800000
#define ENC_HW_ID3                  0x82700000
#define ENC_HW_ID4                  0x82900000

#define HX280ENC_BUF_SIZE           0

unsigned long base_port = INTEGRATOR_LOGIC_MODULE0_BASE;
int irq = VP_PB_INT_LT;

/* module_param(name, type, perm) */
module_param(base_port, ulong, 0);
module_param(irq, int, 0);

/* and this is our MAJOR; use 0 for dynamic allocation (recommended)*/
static int hx280enc_major = 0;

/* here's all the must remember stuff */
typedef struct
{
    char *buffer;
    unsigned int buffsize;
    unsigned long iobaseaddr;
    unsigned int iosize;
    volatile u8 *hwregs;
    unsigned int irq;
    struct fasync_struct *async_queue;
} hx280enc_t;

/* dynamic allocation? */
static hx280enc_t hx280enc_data;

static int ReserveIO(void);
static void ReleaseIO(void);
static void ResetAsic(hx280enc_t * dev);

#ifdef HX280ENC_DEBUG
static void dump_regs(unsigned long data);
#endif

/* IRQ handler */
#if(LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
static irqreturn_t hx280enc_isr(int irq, void *dev_id, struct pt_regs *regs);
#else
static irqreturn_t hx280enc_isr(int irq, void *dev_id);
#endif

/* VM operations */
#if(LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28))
static struct page *hx280enc_vm_nopage(struct vm_area_struct *vma,
                                       unsigned long address, int *type)
{
    PDEBUG("hx280enc_vm_nopage: problem with mem access\n");
    return NOPAGE_SIGBUS;   /* send a SIGBUS */
}
#else
static int hx280enc_vm_fault(struct vm_area_struct *vma,
                                      struct vm_fault *vmf)
{
    PDEBUG("hx280enc_vm_fault: problem with mem access\n");
    return VM_FAULT_SIGBUS; /* send a SIGBUS */
}
#endif

static void hx280enc_vm_open(struct vm_area_struct *vma)
{
    PDEBUG("hx280enc_vm_open:\n");
}

static void hx280enc_vm_close(struct vm_area_struct *vma)
{
    PDEBUG("hx280enc_vm_close:\n");
}

static struct vm_operations_struct hx280enc_vm_ops = {
  open:hx280enc_vm_open,
  close:hx280enc_vm_close,
#if(LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28))
  nopage:hx280enc_vm_nopage,
#else
  fault:hx280enc_vm_fault,
#endif
};

/* the device's mmap method. The VFS has kindly prepared the process's
 * vm_area_struct for us, so we examine this to see what was requested.
 */

static int hx280enc_mmap(struct file *filp, struct vm_area_struct *vma)
{
    int result = -EINVAL;

    result = -EINVAL;

    vma->vm_ops = &hx280enc_vm_ops;

    return result;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36))
static int hx280enc_ioctl(struct inode *inode, struct file *filp,
                          unsigned int cmd, unsigned long arg)
#else
/* From Linux 2.6.36 the locked ioctl was removed in favor of unlocked one */
static long hx280enc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
#endif
{
    int err = 0;

    PDEBUG("ioctl cmd 0x%08ux\n", cmd);
    /*
     * extract the type and number bitfields, and don't encode
     * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
     */
    if(_IOC_TYPE(cmd) != HX280ENC_IOC_MAGIC)
        return -ENOTTY;
    if(_IOC_NR(cmd) > HX280ENC_IOC_MAXNR)
        return -ENOTTY;

    /*
     * the direction is a bitmask, and VERIFY_WRITE catches R/W
     * transfers. `Type' is user-oriented, while
     * access_ok is kernel-oriented, so the concept of "read" and
     * "write" is reversed
     */
    if(_IOC_DIR(cmd) & _IOC_READ)
        err = !access_ok(VERIFY_WRITE, (void *) arg, _IOC_SIZE(cmd));
    else if(_IOC_DIR(cmd) & _IOC_WRITE)
        err = !access_ok(VERIFY_READ, (void *) arg, _IOC_SIZE(cmd));
    if(err)
        return -EFAULT;

    switch (cmd)
    {
    case HX280ENC_IOCGHWOFFSET:
        __put_user(hx280enc_data.iobaseaddr, (unsigned long *) arg);
        break;

    case HX280ENC_IOCGHWIOSIZE:
        __put_user(hx280enc_data.iosize, (unsigned int *) arg);
        break;
    }
    return 0;
}

static int hx280enc_open(struct inode *inode, struct file *filp)
{
    int result = 0;
    hx280enc_t *dev = &hx280enc_data;

    filp->private_data = (void *) dev;

    PDEBUG("dev opened\n");
    return result;
}

static int hx280enc_fasync(int fd, struct file *filp, int mode)
{
    hx280enc_t *dev = (hx280enc_t *) filp->private_data;

    PDEBUG("fasync called\n");

    return fasync_helper(fd, filp, mode, &dev->async_queue);
}

static int hx280enc_release(struct inode *inode, struct file *filp)
{
#ifdef HX280ENC_DEBUG
    hx280enc_t *dev = (hx280enc_t *) filp->private_data;

    dump_regs((unsigned long) dev); /* dump the regs */
#endif

    /* remove this filp from the asynchronusly notified filp's */
    hx280enc_fasync(-1, filp, 0);

    PDEBUG("dev closed\n");
    return 0;
}

/* VFS methods */
static struct file_operations hx280enc_fops = {
  mmap:hx280enc_mmap,
  open:hx280enc_open,
  release:hx280enc_release,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36))
  ioctl:hx280enc_ioctl,
#else
  /* From Linux 2.6.36 the locked ioctl was removed */
  unlocked_ioctl:hx280enc_ioctl,
#endif
  fasync:hx280enc_fasync,
};



int __init hx280enc_init_SPEAr1340(void)
{
    int result;

    u32 * PERIP3_CLK_ENB;
    u32 * PERIP3_SW_RST;
    u32 * ENC_SSCG2;

#define MISC_BASE_ADDRESS	0xe0700000
#define PERIP3_CLK_ENB_OFFSET	0x314
#define PERIP3_SW_RST_OFFSET	0x320
#define ENC_CLK_ENB_BIT		15
#define ENC_SW_RST_BIT		15
#define ENC_SSCG2_OFFSET	0x294
#define ENC_SSCG2_RESET		0x20000
#define ENC_SSCG2_VALUE_240MHZ	0x2160
#define ENC_SSCG2_VALUE_250MHZ	0x2000
    
    printk(KERN_INFO "hx280enc: module init - base_port=0x%08lx irq=%i\n",
           base_port, irq);

    hx280enc_data.iobaseaddr = base_port;
    hx280enc_data.iosize = ENC_IO_SIZE;
    hx280enc_data.irq = irq;
    hx280enc_data.async_queue = NULL;
    hx280enc_data.hwregs = NULL;
    
    printk(KERN_INFO "hx280enc: initializing encoder SPEAr IP clocks\n");
    /*SPEAr1340: enabling decoder clock and reset*/
    PERIP3_CLK_ENB = ioremap( (MISC_BASE_ADDRESS + PERIP3_CLK_ENB_OFFSET) , 0x4);
    PERIP3_SW_RST = ioremap( (MISC_BASE_ADDRESS + PERIP3_SW_RST_OFFSET) , 0x4);
    ENC_SSCG2 = ioremap( (MISC_BASE_ADDRESS + ENC_SSCG2_OFFSET) , 0x4);
    
    *(PERIP3_CLK_ENB) |= (0x1 << ENC_CLK_ENB_BIT);
    *(PERIP3_SW_RST) &= ~(0x1 << ENC_SW_RST_BIT);
    /*asserting SSCG2 reset*/
    *(ENC_SSCG2) = ENC_SSCG2_RESET;
    /*configuring freq divisor*/
    /*in PLL_CFG select Fin=FVCO1/4*/
    /*configuring Tout (3 MSB are integer part ), output is Fout=Fin/(2*Tout) */
    *(ENC_SSCG2) |= ENC_SSCG2_VALUE_240MHZ;
//    *(ENC_SSCG2) |= ENC_SSCG2_VALUE_250MHZ;
    /*releasing reset*/
    *(ENC_SSCG2) &= ~(ENC_SSCG2_RESET);
    
    iounmap(PERIP3_CLK_ENB);
    iounmap(PERIP3_SW_RST);
    iounmap(ENC_SSCG2);
    
    
    printk(KERN_INFO "hx280enc: SPEAr IP clocks init done \n");
    
    result = register_chrdev(hx280enc_major, "hx280enc", &hx280enc_fops);
    if(result < 0)
    {
        printk(KERN_INFO "hx280enc: unable to get major <%d>\n",
               hx280enc_major);
        return result;
    }
    else if(result != 0)    /* this is for dynamic major */
    {
        hx280enc_major = result;
    }

    result = ReserveIO();
    if(result < 0)
    {
        goto err;
    }

    ResetAsic(&hx280enc_data);  /* reset hardware */

    /* get the IRQ line */
    if(irq != -1)
    {
        result = request_irq(irq, hx280enc_isr,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
                             SA_INTERRUPT | SA_SHIRQ,
#else
                             IRQF_DISABLED | IRQF_SHARED,
#endif
                             "hx280enc", (void *) &hx280enc_data);
        if(result == -EINVAL)
        {
            printk(KERN_ERR "hx280enc: Bad irq number or handler\n");
            ReleaseIO();
            goto err;
        }
        else if(result == -EBUSY)
        {
            printk(KERN_ERR "hx280enc: IRQ <%d> busy, change your config\n",
                   hx280enc_data.irq);
            ReleaseIO();
            goto err;
        }
    }
    else
    {
        printk(KERN_INFO "hx280enc: IRQ not in use!\n");
    }

    printk(KERN_INFO "hx280enc: module inserted. Major <%d>\n", hx280enc_major);

    return 0;

  err:
    unregister_chrdev(hx280enc_major, "hx280enc");
    printk(KERN_INFO "hx280enc: module not inserted\n");
    return result;
}




int __init hx280enc_init(void)
{
    int result;
    u32  *reg;
    u32 * RAS_CLK_ENB;
    u32 * RAS_SW_RST;
    
    printk(KERN_INFO "hx280enc: module init - base_port=0x%08lx irq=%i\n",
           base_port, irq);

    hx280enc_data.iobaseaddr = base_port;
    hx280enc_data.iosize = ENC_IO_SIZE;
    hx280enc_data.irq = irq;
    hx280enc_data.async_queue = NULL;
    hx280enc_data.hwregs = NULL;
    
    printk(KERN_INFO "hx280enc: initializing encoder SPEAr IP clocks\n");
    //reg = ioremap(0xE0700210, 0x4);
    /*SPEAr: enabling decoder clock and reset*/
    reg = ioremap(0xE0700210, 0x4);
    *reg &= (~(0x3<<27));/*Clock ras_synth01_sel vco1div4*/
    iounmap(reg);
    /*Remapping RAS_CLK_SYNT0*/
    reg = ioremap(0xE0700264, 0x4);
    *reg = 0x4E6E; /* 204MHz = 0x4e6e RAS_SYNTH0.To = (fin/(2*fout))*2^14 = (500MHz(VCO1div4)/(2*204MHz))*2^14 */
    //*reg = 0xA000; /* 100MHz = 0xA000 RAS_SYNTH0.To = (fin/(2*fout))*2^14 = (500MHz(VCO1div4)/(2*100MHz))*2^14 */
    //*reg = 0x9CDC; /* 102MHz = 0x9CDC RAS_SYNTH0.To = (fin/(2*fout))*2^14 = (500MHz(VCO1div4)/(2*102MHz))*2^14 */
    iounmap(reg);
    
    reg = ioremap(0xE0700280, 0x100);
    *(reg+(0x04>>2)) = *(reg+(0x04>>2)) | (1<<14);	/* RAS_CLK_ENB.synt0_clken clock enable */
    *(reg+(0x08>>2)) = *(reg+(0x08>>2)) & ~(1<<14);	/* RAS_SW_RST.synt0_swrst disable sw reset */
    *(reg+(0x04)) = *(reg+(0x04)) | (1<<14);	/* RAS_CLK_ENB.synt0_clken clock enable */
    *(reg+(0x08)) = *(reg+(0x08)) & ~(1<<14);	/* RAS_SW_RST.synt0_swrst disable sw reset */
    iounmap(reg);
    
    RAS_CLK_ENB = ioremap(0xE0700284 , 0x4); /* Enabling RAS_clk_Synth0 and RAS_aclk */
    RAS_SW_RST = ioremap(0xE0700288 , 0x4);
    *RAS_CLK_ENB |= ( (0x1<<14) + 0x1 ); 
    *RAS_SW_RST &= ~( (0x1<<14) + 0x1 );
    
    iounmap(RAS_CLK_ENB);
    iounmap(RAS_SW_RST);
    
    printk(KERN_INFO "hx170dec: SPEAr IP clocks init done \n");
    
    result = register_chrdev(hx280enc_major, "hx280enc", &hx280enc_fops);
    if(result < 0)
    {
        printk(KERN_INFO "hx280enc: unable to get major <%d>\n",
               hx280enc_major);
        return result;
    }
    else if(result != 0)    /* this is for dynamic major */
    {
        hx280enc_major = result;
    }

    result = ReserveIO();
    if(result < 0)
    {
        goto err;
    }

    ResetAsic(&hx280enc_data);  /* reset hardware */

    /* get the IRQ line */
    if(irq != -1)
    {
        result = request_irq(irq, hx280enc_isr,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
                             SA_INTERRUPT | SA_SHIRQ,
#else
                             IRQF_DISABLED | IRQF_SHARED,
#endif
                             "hx280enc", (void *) &hx280enc_data);
        if(result == -EINVAL)
        {
            printk(KERN_ERR "hx280enc: Bad irq number or handler\n");
            ReleaseIO();
            goto err;
        }
        else if(result == -EBUSY)
        {
            printk(KERN_ERR "hx280enc: IRQ <%d> busy, change your config\n",
                   hx280enc_data.irq);
            ReleaseIO();
            goto err;
        }
    }
    else
    {
        printk(KERN_INFO "hx280enc: IRQ not in use!\n");
    }

    printk(KERN_INFO "hx280enc: module inserted. Major <%d>\n", hx280enc_major);

    return 0;

  err:
    unregister_chrdev(hx280enc_major, "hx280enc");
    printk(KERN_INFO "hx280enc: module not inserted\n");
    return result;
}

void __exit hx280enc_cleanup(void)
{
    writel(0, hx280enc_data.hwregs + 0x38); /* disable HW */
    writel(0, hx280enc_data.hwregs + 0x04); /* clear enc IRQ */

    /* free the encoder IRQ */
    if(hx280enc_data.irq != -1)
    {
        free_irq(hx280enc_data.irq, (void *) &hx280enc_data);
    }

    ReleaseIO();

    unregister_chrdev(hx280enc_major, "hx280enc");

    printk(KERN_INFO "hx280enc: module removed\n");
    return;
}

module_init(hx280enc_init_SPEAr1340);
module_exit(hx280enc_cleanup);

static int ReserveIO(void)
{
    long int hwid;

    if(!request_mem_region
       (hx280enc_data.iobaseaddr, hx280enc_data.iosize, "hx280enc"))
    {
        printk(KERN_INFO "hx280enc: failed to reserve HW regs\n");
        return -EBUSY;
    }

    hx280enc_data.hwregs =
        (volatile u8 *) ioremap_nocache(hx280enc_data.iobaseaddr,
                                        hx280enc_data.iosize);

    if(hx280enc_data.hwregs == NULL)
    {
        printk(KERN_INFO "hx280enc: failed to ioremap HW regs\n");
        ReleaseIO();
        return -EBUSY;
    }

    hwid = readl(hx280enc_data.hwregs);

#if 1
    /* check for encoder HW ID */
    if((((hwid >> 16) & 0xFFFF) != ((ENC_HW_ID1 >> 16) & 0xFFFF)) &&
       (((hwid >> 16) & 0xFFFF) != ((ENC_HW_ID2 >> 16) & 0xFFFF)) &&
       (((hwid >> 16) & 0xFFFF) != ((ENC_HW_ID3 >> 16) & 0xFFFF)) &&
       (((hwid >> 16) & 0xFFFF) != ((ENC_HW_ID4 >> 16) & 0xFFFF)))
    {
        printk(KERN_INFO "hx280enc: HW not found at 0x%08lx (hwid: 0x%08lx)\n",
               hx280enc_data.iobaseaddr, hwid);
#ifdef HX280ENC_DEBUG
        dump_regs((unsigned long) &hx280enc_data);
#endif
        ReleaseIO();
        return -EBUSY;
    }
#endif
    printk(KERN_INFO
           "hx280enc: HW at base <0x%08lx> with ID <0x%08lx>\n",
           hx280enc_data.iobaseaddr, hwid);

    return 0;
}

static void ReleaseIO(void)
{
    if(hx280enc_data.hwregs)
        iounmap((void *) hx280enc_data.hwregs);
    release_mem_region(hx280enc_data.iobaseaddr, hx280enc_data.iosize);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
irqreturn_t hx280enc_isr(int irq, void *dev_id, struct pt_regs *regs)
#else
irqreturn_t hx280enc_isr(int irq, void *dev_id)
#endif
{
    hx280enc_t *dev = (hx280enc_t *) dev_id;
    u32 irq_status;

    irq_status = readl(dev->hwregs + 0x04);

    if(irq_status & 0x01)
    {

        /* clear enc IRQ and slice ready interrupt bit */
        writel(irq_status & (~0x101), dev->hwregs + 0x04);

        /* Handle slice ready interrupts. The reference implementation
         * doesn't signal slice ready interrupts to EWL.
         * The EWL will poll the slices ready register value. */
        if ((irq_status & 0x1FE) == 0x100)
        {
            PDEBUG("Slice ready IRQ handled!\n");
            return IRQ_HANDLED;
        }

        /* All other interrupts will be signaled to EWL. */
        if(dev->async_queue)
            kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
        else
        {
            printk(KERN_WARNING
                   "hx280enc: IRQ received w/o anybody waiting for it!\n");
        }

        PDEBUG("IRQ handled!\n");
        return IRQ_HANDLED;
    }
    else
    {
        PDEBUG("IRQ received, but NOT handled!\n");
        return IRQ_NONE;
    }

}

void ResetAsic(hx280enc_t * dev)
{
    int i;

    writel(0, dev->hwregs + 0x38);

    for(i = 4; i < dev->iosize; i += 4)
    {
        writel(0, dev->hwregs + i);
    }
}

#ifdef HX280ENC_DEBUG
void dump_regs(unsigned long data)
{
    hx280enc_t *dev = (hx280enc_t *) data;
    int i;

    PDEBUG("Reg Dump Start\n");
    for(i = 0; i < dev->iosize; i += 4)
    {
        PDEBUG("\toffset %02X = %08X\n", i, readl(dev->hwregs + i));
    }
    PDEBUG("Reg Dump End\n");
}
#endif
