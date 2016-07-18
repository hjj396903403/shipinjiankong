#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <pthread.h>
#include <linux/fb.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <dirent.h>
#include <stdlib.h>
#include <tslib.h>
#include <fcntl.h>
#include <poll.h>
#include <linux/videodev2.h>
#include <sys/time.h>
#include<assert.h>
#include <stdio.h>
#include <time.h>
#include <signal.h>

int fd_key;
unsigned char key;
static int key_flag;
static char *src_bmp;	//存放rgb信息的地址，传给save_bmp函数

void sighandler(int sig)
{//	printf("捕获的信号: %d\n",sig);	
	ioctl(fd_key,888,&key);
	printf("0x%x\n",key);
}




//保存为BMP格式的文件
void save_bmp(char *src);


unsigned char *frame_buffer; //保存图像的首地址
//读出的视频格式
static int SpFmt[] = {V4L2_PIX_FMT_YUYV, V4L2_PIX_FMT_MJPEG,V4L2_PIX_FMT_RGB565};
static char *ShowFmt[] = {"YUV", "MJPEG", "RGB565"};


/* 图片的象素数据 */
typedef struct PixelDatas {
	int iWidth;   /* 宽度: 一行有多少个象素 */
	int iHeight;  /* 高度: 一列有多少个象素 */
	int iBpp;     /* 一个象素用多少位来表示 */
	int iLineBytes;  /* 一行数据有多少字节 */
	int iTotalBytes; /* 所有字节数 */ 
	unsigned char *aucPixelDatas;  /* 象素数据存储的地方 */
}T_PixelDatas,*PT_PixelDatas;


//**********************lcd********************//

static struct fb_var_screeninfo var;
static struct fb_fix_screeninfo fix;
static unsigned char *fbmem = NULL; //LCD的映射的首地址
unsigned int line_width;
int fb;

//**********************camera********************//
struct v4l2_buffer tV4l2Buf;
static int iFd;
static int ListNum;

T_PixelDatas ptVideoBufOut;/*向LCD输出的数据--转化后*/
T_PixelDatas ptVideoBufIn;/*存放摄像头输入的数据-转化前*/

unsigned char* pucVideBuf[4]; // 视频BUFF空间地址
PT_PixelDatas video_buff;

static unsigned char *hzkmem = NULL;
struct stat t_stat;
struct tsdev *TsDev;//ts_open的返回值需要用到
unsigned char *bmpmem = NULL;

FILE *filp;

void show_pixel(int x,int y,int color)//画点函数
{
	unsigned char *bbp8 =NULL;
	unsigned short *bbp16 = NULL;
	unsigned int *bbp32 = NULL;
	unsigned int r,g,b;
	r = color>>16&0xff;
	g = color>>8&0xff;
	b = color&0xff;
	//定位到当前位置
	bbp8 = fbmem+var.xres*var.bits_per_pixel/8*y+x*var.bits_per_pixel/8;
	bbp16 = (unsigned short *)bbp8;
	bbp32 = (unsigned int *)bbp8;
	switch(var.bits_per_pixel)
	{
		case 8:
			*bbp8 = color;break;
		case 16:
			*bbp16 = ((r>>3)<<11)|((g>>2)<<5)|((b>>3));break;
		case 32:
			*bbp32 = color;break;
		default:break;			
	}
}


//初始化frambuufer
static int LcdOpen(unsigned char *fbname)
{
	fb = open(fbname,2);//打开lcd
	if(fb<0)
	{
		printf("open fbdev is error!!!\n");
		return -1;
	}
	ioctl(fb,FBIOGET_VSCREENINFO,&var);//获取固定参数结构体放在var结构体中
	ioctl(fb,FBIOGET_FSCREENINFO,&fix);//获取固定参数，存放在fix结构体中

	// 显示一行需要的字节数量=800*32/ 8
 	line_width  = var.xres * var.bits_per_pixel / 8;

	/*
	将LCD屏的地址映射到DDR内存空间
	返回值LCD屏的首地址
	*/
	fbmem = (unsigned char *)mmap(NULL,fix.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,fb,0);//映射内存
	if(fbmem == (unsigned char *)-1)//映射失败
	{
		printf("fbmmap is error!!!\n");
		munmap(fbmem,fix.smem_len);
		return -1;
	}
	memset(fbmem,0x00,fix.smem_len);//清屏函数往映射的地址填充fix.sem_len大小的0xff颜色进去
	return 0;
}


//显示一行数据
void show_line(unsigned char *src,unsigned char* dst,unsigned int width)
{
	unsigned char r,g,b;
	unsigned int color,i;
	unsigned short* dst16  = (unsigned short *)dst;
	unsigned int* dst32 = (unsigned int *)dst;	
	for(i=0;i<width;i++)
	{
		b = src[0];
		g = src[1];
		r = src[2];
		color = r<<16|g<<8|b; // RGB888
		*dst32++ = color;
		src+=3;
	}
}

void freebmpaddr(unsigned char *bmpmem)
{
	munmap(bmpmem,t_stat.st_size);
	fclose(filp);
}


//判断读出的图片数据格式
int isSpFmt(int FmtPix)
{
	int i=0;
	for(i=0;i<sizeof(SpFmt)/sizeof(SpFmt[0]);i++)
	{
		if(SpFmt[i] == FmtPix)
		{
			printf("Fomat is :%s\n",ShowFmt[i]);
			return 0;
		}
	}
	return -1;
}

//YUV转RGB实现
static unsigned int
Pyuv422torgb32(unsigned char x,unsigned y ,unsigned char * input_ptr, unsigned char * output_ptr, unsigned int image_width, unsigned int image_height)
{
	unsigned int i, size,j;
	unsigned char Y, Y1, U, V;
	unsigned char *buff = input_ptr; //保存地址-源数据
	unsigned char * output_pt=output_ptr;
	unsigned char *src = NULL;
	unsigned char * dst = NULL;
 	unsigned char r, g, b;
 	unsigned int color;
	
	size = image_width * image_height /2;
	for (i = size; i > 0; i--) 
	{
		/* bgr instead rgb ?? */
		Y = buff[0];
		U = buff[1];
		Y1= buff[2];
		V = buff[3];
		buff += 4;
		r = R_FROMYV(Y,V);
		g = G_FROMYUV(Y,U,V); //b
		b = B_FROMYU(Y,U); //v
		*output_pt++ = b;
		*output_pt++ = g;
		*output_pt++ = r;
		r = R_FROMYV(Y1,V);
		g = G_FROMYUV(Y1,U,V); //b
		b = B_FROMYU(Y1,U); //v
		*output_pt++ = b;
		*output_pt++ = g;
		*output_pt++ = r;
	}

	//在LCD屏显示采集的数据图像起始地址
	src = output_ptr; //RGB数据
//	save_bmp(output_ptr); //保存BMP图片
	src_bmp=src;
	//计算显示的起始地址
	dst = fbmem+var.xres*var.bits_per_pixel/8*y+x*var.bits_per_pixel/8;

	for(j=0;j<image_height;j++)
	{
		show_line(src,dst,image_width);
		src+=(image_width*3);
		dst+=line_width;
	}
	return 0;
	
} 

/* 参考luvcview */


//YUV转RGB
static int Yuv2RgbConvert(unsigned char x,unsigned char y,PT_PixelDatas ptVideoBufIn, PT_PixelDatas ptVideoBufOut)
{
	//声明两个指针
	PT_PixelDatas ptPixelDatasIn  = ptVideoBufIn;
	PT_PixelDatas ptPixelDatasOut = ptVideoBufOut;

	/*进行了赋值操作*/
	video_buff = ptVideoBufIn;
	ptPixelDatasOut->iWidth  = ptPixelDatasIn->iWidth;
	ptPixelDatasOut->iHeight = ptPixelDatasIn->iHeight;

	ptPixelDatasOut->iBpp = 32; /*像素位数*/
	//一行的字节数
	ptPixelDatasOut->iLineBytes  = ptPixelDatasOut->iWidth * ptPixelDatasOut->iBpp / 8;
	//总共的字节数
	ptPixelDatasOut->iTotalBytes = ptPixelDatasOut->iLineBytes * ptPixelDatasOut->iHeight;

	//申请一帧数据的空间
	if (!ptPixelDatasOut->aucPixelDatas) //判断存放像素地址是否为空 
	{
		ptPixelDatasOut->aucPixelDatas = malloc(ptPixelDatasOut->iTotalBytes); //申请空间
	}

	//YUV422转RGB
	Pyuv422torgb32(x,y,ptPixelDatasIn->aucPixelDatas, ptPixelDatasOut->aucPixelDatas, ptPixelDatasOut->iWidth, ptPixelDatasOut->iHeight);
	return 0;
}


//摄像头设备的初始化
static int camera_init(void)
{

	int i=0;
	int cnt=0;
	int error;

	//定义摄像头驱动的BUF的功能捕获视频
	int iType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	
	/* 1、打开视频设备 */
	iFd = open("/dev/video15",O_RDWR);
	if(iFd < 0)
	{
		printf("can't open /dev/video\n");
		return 0;
	}

	struct v4l2_capability tV4L2Cap; //获取摄像头功能
	/* 2、VIDIOC_QUERYCAP 确定它是否视频捕捉设备,支持哪种接口(streaming/read,write) */
	error = ioctl(iFd,VIDIOC_QUERYCAP,&tV4L2Cap);
	if(error)
	{
		printf("no this video device\n");
		return -1;
	}
	
	/* 2.1、检测是否视频CAPTURE设备 */
	if (!(tV4L2Cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
    	printf("not a video capture device\n");
        return -1;
    }
	
	/* 2.2、支持哪种接口:mmap read/write */
	if (tV4L2Cap.capabilities & V4L2_CAP_STREAMING) 
	{
		 printf("supports streaming i/o\n");
	}

    //判断是否支持普通的读写IO
	if (tV4L2Cap.capabilities & V4L2_CAP_READWRITE) 
	{
	    printf("supports read i/o\n");
	}

	struct v4l2_fmtdesc tV4L2FmtDesc; //保存摄像头支持的格式
	/* 3、VIDIOC_ENUM_FMT 查询支持哪种格式 */
	memset(&tV4L2FmtDesc, 0, sizeof(tV4L2FmtDesc));
	tV4L2FmtDesc.index = 0; //索引编号，数组下标
	tV4L2FmtDesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; //支持视频捕获

	//读出该设备支持的图像格式
	while ((error = ioctl(iFd,VIDIOC_ENUM_FMT, &tV4L2FmtDesc)) == 0) 
	{
		printf("ok %d\n",++cnt);
		
		//判断读出的图片数据格式
        if (!isSpFmt(tV4L2FmtDesc.pixelformat))
        {
            printf("Support :%d\n",tV4L2FmtDesc.pixelformat);
            break;
        }
		tV4L2FmtDesc.index++;
	}

	struct v4l2_format  tV4l2Fmt;
	/* 4、 VIDIOC_S_FMT 设置摄像头使用哪种格式 */
	memset(&tV4l2Fmt, 0, sizeof(struct v4l2_format));
	tV4l2Fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; //视频捕获

	//设置摄像头输出的图像格式

	if(tV4L2FmtDesc.pixelformat!=V4L2_PIX_FMT_YUYV)
	{
		tV4l2Fmt.fmt.pix.pixelformat=V4L2_PIX_FMT_YUYV;
	}else
	{
		tV4l2Fmt.fmt.pix.pixelformat = tV4L2FmtDesc.pixelformat;
	}
	
	/*修改显示的尺寸---在LCD上显示的位置*/
	tV4l2Fmt.fmt.pix.width       = 480;
	tV4l2Fmt.fmt.pix.height      = 800;
	tV4l2Fmt.fmt.pix.field       = V4L2_FIELD_ANY;
	
    /* 如果驱动程序发现无法某些参数(比如分辨率),
     * 它会调整这些参数, 并且返回给应用程序
     */
    //VIDIOC_S_FMT 设置摄像头的输出参数
    error = ioctl(iFd, VIDIOC_S_FMT, &tV4l2Fmt); 
    if (error) 
    {
    	printf("Unable to set format\n");
       return -1;   
    }

	//打印摄像头实际的输出参数
	printf("Support Format:%d\n",tV4l2Fmt.fmt.pix.pixelformat);
	printf("Support width:%d\n",tV4l2Fmt.fmt.pix.width);
	printf("Support height:%d\n",tV4l2Fmt.fmt.pix.height);


	/* 初始化ptVideoBufIn结构体，为转化做准备 */
	ptVideoBufIn.iBpp = (tV4l2Fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV) ? 24 : \
                                        (tV4l2Fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG) ? 0 :  \
                                        (tV4l2Fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB565) ? 16 :  \
                                        0;
	//高度 和宽度的赋值
	ptVideoBufIn.iHeight = tV4l2Fmt.fmt.pix.height;
	ptVideoBufIn.iWidth = tV4l2Fmt.fmt.pix.width;

	//一行所需要的字节数
	ptVideoBufIn.iLineBytes = ptVideoBufIn.iWidth*ptVideoBufIn.iBpp/8;
    //一帧图像的字节数
	ptVideoBufIn.iTotalBytes = ptVideoBufIn.iLineBytes * ptVideoBufIn.iHeight;
	printf("ptVideoBufIn.iBpp = %d\n",ptVideoBufIn.iBpp);

	
	//v412请求命令
	struct v4l2_requestbuffers tV4l2ReqBuffs;
	
	/* 5、VIDIOC_REQBUFS  申请buffer */
	memset(&tV4l2ReqBuffs, 0, sizeof(struct v4l2_requestbuffers));

	/* 分配4个buffer:实际上由VIDIOC_REQBUFS获取到的信息来决定 */
	tV4l2ReqBuffs.count   = 4;
	/*支持视频捕获功能*/
	tV4l2ReqBuffs.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	/* 表示申请的缓冲是支持MMAP */
	tV4l2ReqBuffs.memory  = V4L2_MEMORY_MMAP;
	/* 为分配buffer做准备 */
	error = ioctl(iFd, VIDIOC_REQBUFS, &tV4l2ReqBuffs);
	if (error) 
	{
		printf("Unable to allocate buffers.\n");
	    return -1;     
	}
	
	/* 判断是否支持mmap */
	if (tV4L2Cap.capabilities & V4L2_CAP_STREAMING)
	{
		 /* map the buffers */
        for (i = 0; i < tV4l2ReqBuffs.count; i++) 
        {
        	memset(&tV4l2Buf, 0, sizeof(struct v4l2_buffer));
        	tV4l2Buf.index = i;
        	tV4l2Buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        	tV4l2Buf.memory = V4L2_MEMORY_MMAP;

			/* 6、VIDIOC_QUERYBUF 确定每一个buffer的信息 并且 mmap */
        	error = ioctl(iFd, VIDIOC_QUERYBUF, &tV4l2Buf);
		    if (error) 
				{
					    printf("Unable to query buffer.\n");
					   return -1;
				}

		 //打印的索引
          printf("length = %d\n",tV4l2Buf.length);

			//映射空间地址
        	pucVideBuf[i] = mmap(0 /* start anywhere */ ,
        			  tV4l2Buf.length, PROT_READ, MAP_SHARED, iFd,
        			  tV4l2Buf.m.offset);
        	if (pucVideBuf[i] == MAP_FAILED) 
            {
        	    printf("Unable to map buffer\n");
        	   return -1;
        	}
			printf("mmap %d addr:%p\n",i,pucVideBuf[i]);
		}
	}


	/* 7、VIDIOC_QBUF  放入队列 */
    for (i = 0; i <tV4l2ReqBuffs.count; i++) 
    {
    	memset(&tV4l2Buf, 0, sizeof(struct v4l2_buffer));
    	tV4l2Buf.index = i;
    	tV4l2Buf.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    	tV4l2Buf.memory = V4L2_MEMORY_MMAP;

		//将BUF放入队列-
    	error = ioctl(iFd, VIDIOC_QBUF, &tV4l2Buf);
    	if (error)
        {
    	    printf("Unable to queue buffer.\n");
    	  	 return -1;
    	}
    }
	printf("ready to read data\n");

	
	/* 8、启动摄像头开始读数据
	VIDIOC_STREAMON :启动
	*/
    error = ioctl(iFd, VIDIOC_STREAMON, &iType);
    if (error) 
    {
    	printf("Unable to start capture.\n");
    	return -1;
    }
	return 0;
}



/*读取显示视频的线程*/
void *camera_pthread(void * arg)
{
	int error;
	int cnt=0;
	int i=0;
	int ListNum;
	
	struct pollfd fds[1];
	
	/* 8.1、使用poll来等待是否有数据 */
	fds[0].fd = iFd;
	fds[0].events=POLLIN;
	
	/* YUV格式的数据<------>在LCD上显示:rgb888 */
	initLut();
	ptVideoBufOut.aucPixelDatas=NULL; /*初始地址指向空*/

	while(1)
	{
		//printf("wait data....-->\n");
		error = poll(fds, 1, -1);
		memset(&tV4l2Buf, 0, sizeof(struct v4l2_buffer));
		tV4l2Buf.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE; //类型
		tV4l2Buf.memory  = V4L2_MEMORY_MMAP; //存储空间类型

		/* 9、VIDIOC_DQBUF    从队列中取出 */
		//printf("wait ioctl data....-->\n");
		error = ioctl(iFd, VIDIOC_DQBUF, &tV4l2Buf); //取出一帧数据
		ListNum = tV4l2Buf.index; //索引编号
		//printf("listnum1:%d\n",ListNum); //打印索引编号

		/*地址赋值
		pucVideBuf[ListNum]: 存放摄像头输出的数据
		*/
		ptVideoBufIn.aucPixelDatas = pucVideBuf[ListNum];

		//在LCD屏上显示转化的数据
		Yuv2RgbConvert(0,0,&ptVideoBufIn,&ptVideoBufOut);

		memset(&tV4l2Buf, 0, sizeof(struct v4l2_buffer));
		tV4l2Buf.index  = ListNum;
		tV4l2Buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		tV4l2Buf.memory = V4L2_MEMORY_MMAP;
		error = ioctl(iFd, VIDIOC_QBUF, &tV4l2Buf);
		//printf("listnum2:%d\n",tV4l2Buf.index); //打印索引编号
		//Yuv2RgbConvert(0,0,&ptVideoBufIn,&ptVideoBufOut);

	}
}


int main(int argc ,char *argv[])//./a.out /dev/fb0 
{
	pthread_t camerathread; //存放线程的ID
	
	LcdOpen("/dev/fb0");//打开lcd   
	
	if(camera_init()) //打开摄像头设备
	{
		close(iFd);
		printf("camera init fail\n");
	}	

	fd_key=open("/dev/test_led_0",2);
		/*1. 绑定需要捕获的信号 */ 
	signal(SIGIO,sighandler);
	/*2. 设置pid*/	
	int f_flags;
	fcntl(fd_key,F_SETOWN,getpid());//给驱动文件描述符赋值当进程的PID号?	/*3. 设置驱动文件支持异步IO*/
	f_flags = fcntl(fd_key,F_GETFL);  //获取当前的文件属性,返回值就是获取的属性
	fcntl(fd_key,F_SETFL,f_flags|FASYNC);   //设置当前驱动支持异步IO操作
	key_flag=1;
	
	pthread_create(&camerathread,NULL,camera_pthread,NULL); //摄像头线程
	while(1)
	{
		if((key_flag==1)&&(key==0x01))
		{
			save_bmp(src_bmp);
			key_flag=0;
		}
		if(key==0x81)
		{
			key_flag=1;
		}
//		printf("1234568\n");
		//sleep(5);
	}
	return 0;
}




/*-----------------------------------------------------------
								保存为BMP格式的图片代码
-------------------------------------------------------------*/

typedef unsigned char  BYTE;
typedef unsigned short	WORD;
typedef unsigned long  DWORD;

#define BMP_NAME "camera123.bmp"

#pragma pack(1)

typedef struct tagBITMAPFILEHEADER{
     WORD	bfType;                // the flag of bmp, value is "BM"
     DWORD    bfSize;                // size BMP file ,unit is bytes
     DWORD    bfReserved;            // 0
     DWORD    bfOffBits;             // must be 54

}BITMAPFILEHEADER;

 
typedef struct tagBITMAPINFOHEADER{
     DWORD    biSize;                // must be 0x28
     DWORD    biWidth;           //
     DWORD    biHeight;          //
     WORD		biPlanes;          // must be 1
     WORD		biBitCount;            //
     DWORD    biCompression;         //
     DWORD    biSizeImage;       //
     DWORD    biXPelsPerMeter;   //
     DWORD    biYPelsPerMeter;   //
     DWORD    biClrUsed;             //
     DWORD    biClrImportant;        //
}BITMAPINFOHEADER;

//颜色表
typedef struct tagRGBQUAD{
     BYTE	rgbBlue;
     BYTE	rgbGreen;
     BYTE	rgbRed;
     BYTE	rgbReserved;
}RGBQUAD;


//保存为BMP格式的文件
void save_bmp(char *src)
{
	
	/*-----------------------------------------------------------
					获取时间参数，用来给图片命名
	-------------------------------------------------------------*/
		time_t t;
    struct tm *tmp;
    char buffer1[1024] = {0}, buffer2[1024] = {0};
    t = time(NULL);
    tmp=localtime(&t);
    if (strftime(buffer1, sizeof(buffer1), "%Y_%m_%d_%H_%M_%S_picture_.bmp", tmp) == 0) 
    {
    	printf("timer error\n");
    }
    printf("%s  save ok!\n",buffer1);
	
	/*-----------------------------------------------------------
					获取图片数据，用来保存为BMP图片格式
	-------------------------------------------------------------*/
    FILE * fp;
    int i;
    BITMAPFILEHEADER   bf;
    BITMAPINFOHEADER   bi;
   
    memset(&bf ,0 ,sizeof(BITMAPFILEHEADER));
    memset(&bi ,0 ,sizeof(BITMAPINFOHEADER));

    fp = fopen(buffer1, "wb");
    if(!fp)
    {
			printf("open %s error\n",buffer1);
			return ;
    }
	
		//Set BITMAPINFOHEADER 设置BMP信息头
		bi.biSize = sizeof(BITMAPINFOHEADER);//40;
		bi.biWidth = 640;//IMAGEWIDTH;
		bi.biHeight = 480;//IMAGEHEIGHT;
		bi.biPlanes = 1;
		bi.biBitCount = 24;//8;
		bi.biCompression = 0;
		bi.biSizeImage =640*480*3; //;0
		bi.biXPelsPerMeter = 0;
		bi.biYPelsPerMeter = 0;
		bi.biClrUsed = 0;// 1<<(bi.biBitCount)
		bi.biClrImportant = 0;

      //Set BITMAPFILEHEADER
      bf.bfType = 0x4d42; //'B''M'
      bf.bfSize = 54 + bi.biSizeImage;// sizeof(BITMAPFILEHEADER);    
      bf.bfReserved = 0;
      bf.bfOffBits = 54;//(DWORD)sizeof(BITMAPFILEHEADER)+(DWORD)sizeof(BITMAPINFOHEADER)+sizeof(RGBQUAD)*256;//
     
      RGBQUAD rgbquad[256];
      
      for(i=0 ;i<256 ;i++) {
          rgbquad[i].rgbBlue=i;
          rgbquad[i].rgbGreen=i;
          rgbquad[i].rgbRed=i;
          rgbquad[i].rgbReserved=0;
          
     }
    
    printf("bf=%d,bi=%d,rgbquard=%d\n",sizeof(BITMAPFILEHEADER),sizeof(BITMAPINFOHEADER),sizeof(RGBQUAD));
    fwrite(&bf, sizeof(BITMAPFILEHEADER), 1, fp);
    fwrite(&bi, sizeof(BITMAPINFOHEADER), 1, fp);    
//    fwrite(src, 640*480*3, 1, fp);
	char *src_tmp;
	for(src_tmp=src+640*480*3;src_tmp!=src;src_tmp-=640*3)
	{
		fwrite(src_tmp-640*3, 640*3, 1, fp);
	}
    fclose(fp);
}

