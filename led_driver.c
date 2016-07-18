#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>  //�����豸
#include <linux/fs.h>          //�ļ�����
#include <linux/uaccess.h>     // copy_
#include <linux/io.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <mach/gpio.h>
#include <plat/gpio-cfg.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/poll.h>
#include <asm/siginfo.h>
#include <linux/signal.h>

static unsigned int major;      // ������豸��
static struct class *cls;      // ������
static dev_t DEV_NUM;           //�豸��
static struct cdev *dev;        //cdev

struct fasync_struct *fapp;
static char current_key;


struct interrupt
{
	int gpio;
	int key_value;
	char name[50];
	int irq;
};

struct interrupt ir_list[4]={
{EXYNOS4_GPX3(2),0x01,"key1"},
{EXYNOS4_GPX3(3),0x02,"key2"},
{EXYNOS4_GPX3(4),0x03,"key3"},
{EXYNOS4_GPX3(5),0x04,"key4"}
};

irqreturn_t irq_handler(int irq, void *dev)
{
	struct interrupt *p=(struct interrupt *)dev;
	printk("irq=%d\n",p->irq);
	int i;
	
	if(!gpio_get_value(p->gpio))
		{
			
			current_key=p->key_value;
		//	printk("%d��������״̬\n",p->key_value);
		}
	else
		{
			current_key=p->key_value|0x80;
		//	printk("%d�����ɿ�״̬\n",p->key_value);
		}
	
	
	kill_fasync(&fapp,SIGIO,POLL_IN);



	return IRQ_HANDLED;
}

int tiny4412_open (struct inode *my_inode, struct file *my_file)
{
	printk("open�ɹ�����\n");
	return 0;
}
int tiny4412_release (struct inode *my_inode, struct file *my_file)
{
	printk("release�ɹ�����\n");
//	fasync_helper(0,my_file,0,&fapp);
	return 0;
}


static long tiny4412_unlocked_ioctl(struct file *my_file, unsigned int cmd, unsigned long argv) // 2.6֮�������
{


	//printk("ioctl�ɹ�����\n");
    copy_to_user(argv,&current_key,1);
	return 0;
}

unsigned int tiny4412_poll(struct file *my_file, struct poll_table_struct *my_table)
{
/*
	poll_wait(my_file,&my_wait,my_table);
	if(condition==0)
	{
		return 0;		
	}
	condition=0;
	return POLLIN;
*/
}

int tiny4412_fasync(int fd, struct file *my_file, int on)
{
	return fasync_helper(fd,my_file,on,&fapp);
}


//_IOC
//�����ļ���������
static struct file_operations tiny4412_ops=
{
	.open=tiny4412_open,
	.release=tiny4412_release,
	.unlocked_ioctl=tiny4412_unlocked_ioctl,
	.poll=tiny4412_poll,
	.fasync=tiny4412_fasync
};


static int __init tiny4412_hello_module_init(void)
{
	/*1.��ȡ�жϱ��*/
	int i;
	for(i=0;i<4;i++)
	{
		ir_list[i].irq=gpio_to_irq(ir_list[i].gpio);
		request_irq(ir_list[i].irq,irq_handler,IRQ_TYPE_EDGE_BOTH,ir_list[i].name,&ir_list[i]);
	}
	/*2.ע���ж�*/
	
	//��1. �Զ������豸��--ע���豸��
    alloc_chrdev_region(&DEV_NUM,0, 5,"led");  
   // 250:0  250:1  250:2  250:3  250:4

   // �ڶ���ע��:  250:5 ....
  
	printk("���豸��: %d\n",MAJOR(DEV_NUM));
	printk("���豸��: %d\n",MINOR(DEV_NUM));

	/*2. ��̬����cdev�ṹ��*/
	dev=cdev_alloc(); 
	
    /*3. ��ʼ��cdev�ṹ��*/
    cdev_init(dev,&tiny4412_ops);

    /*4. ����豸���ں�---ע�Ჽ��*/
    cdev_add(dev,DEV_NUM,1);

	/*������*/
 	cls=class_create(THIS_MODULE,"BEEP");

	for(i=0;i<1;i++)
	{
		/*�����豸�ڵ�*/
		device_create(cls,NULL,DEV_NUM+i,NULL,"test_led_%d",i);	
	}
    return 0;
}

static void __exit tiny4412_hello_module_cleanup(void)
{
	int i;
	for(i=0;i<4;i++)
	{
		free_irq(ir_list[i].irq,&ir_list[i]);
	}

	
	for(i=0;i<1;i++)
	{
		//ע���豸�ڵ�
	 	device_destroy(cls,DEV_NUM+i);
	}
	 //ע����
	 class_destroy(cls);
     //����cdev�ṹ��	
	 cdev_del(dev);
	 //�ͷſռ�	
     kfree((void*)dev);
     //ע���豸��
     unregister_chrdev_region(DEV_NUM,1);
}

module_init(tiny4412_hello_module_init);
module_exit(tiny4412_hello_module_cleanup);

MODULE_LICENSE("GPL");





