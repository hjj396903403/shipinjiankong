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
static char *src_bmp;	//���rgb��Ϣ�ĵ�ַ������save_bmp����

void sighandler(int sig)
{//	printf("������ź�: %d\n",sig);	
	ioctl(fd_key,888,&key);
	printf("0x%x\n",key);
}




//����ΪBMP��ʽ���ļ�
void save_bmp(char *src);


unsigned char *frame_buffer; //����ͼ����׵�ַ
//��������Ƶ��ʽ
static int SpFmt[] = {V4L2_PIX_FMT_YUYV, V4L2_PIX_FMT_MJPEG,V4L2_PIX_FMT_RGB565};
static char *ShowFmt[] = {"YUV", "MJPEG", "RGB565"};


/* ͼƬ���������� */
typedef struct PixelDatas {
	int iWidth;   /* ���: һ���ж��ٸ����� */
	int iHeight;  /* �߶�: һ���ж��ٸ����� */
	int iBpp;     /* һ�������ö���λ����ʾ */
	int iLineBytes;  /* һ�������ж����ֽ� */
	int iTotalBytes; /* �����ֽ��� */ 
	unsigned char *aucPixelDatas;  /* �������ݴ洢�ĵط� */
}T_PixelDatas,*PT_PixelDatas;


//**********************lcd********************//

static struct fb_var_screeninfo var;
static struct fb_fix_screeninfo fix;
static unsigned char *fbmem = NULL; //LCD��ӳ����׵�ַ
unsigned int line_width;
int fb;

//**********************camera********************//
struct v4l2_buffer tV4l2Buf;
static int iFd;
static int ListNum;

T_PixelDatas ptVideoBufOut;/*��LCD���������--ת����*/
T_PixelDatas ptVideoBufIn;/*�������ͷ���������-ת��ǰ*/

unsigned char* pucVideBuf[4]; // ��ƵBUFF�ռ��ַ
PT_PixelDatas video_buff;

static unsigned char *hzkmem = NULL;
struct stat t_stat;
struct tsdev *TsDev;//ts_open�ķ���ֵ��Ҫ�õ�
unsigned char *bmpmem = NULL;

FILE *filp;

void show_pixel(int x,int y,int color)//���㺯��
{
	unsigned char *bbp8 =NULL;
	unsigned short *bbp16 = NULL;
	unsigned int *bbp32 = NULL;
	unsigned int r,g,b;
	r = color>>16&0xff;
	g = color>>8&0xff;
	b = color&0xff;
	//��λ����ǰλ��
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


//��ʼ��frambuufer
static int LcdOpen(unsigned char *fbname)
{
	fb = open(fbname,2);//��lcd
	if(fb<0)
	{
		printf("open fbdev is error!!!\n");
		return -1;
	}
	ioctl(fb,FBIOGET_VSCREENINFO,&var);//��ȡ�̶������ṹ�����var�ṹ����
	ioctl(fb,FBIOGET_FSCREENINFO,&fix);//��ȡ�̶������������fix�ṹ����

	// ��ʾһ����Ҫ���ֽ�����=800*32/ 8
 	line_width  = var.xres * var.bits_per_pixel / 8;

	/*
	��LCD���ĵ�ַӳ�䵽DDR�ڴ�ռ�
	����ֵLCD�����׵�ַ
	*/
	fbmem = (unsigned char *)mmap(NULL,fix.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,fb,0);//ӳ���ڴ�
	if(fbmem == (unsigned char *)-1)//ӳ��ʧ��
	{
		printf("fbmmap is error!!!\n");
		munmap(fbmem,fix.smem_len);
		return -1;
	}
	memset(fbmem,0x00,fix.smem_len);//����������ӳ��ĵ�ַ���fix.sem_len��С��0xff��ɫ��ȥ
	return 0;
}


//��ʾһ������
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


//�ж϶�����ͼƬ���ݸ�ʽ
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

//YUVתRGBʵ��
static unsigned int
Pyuv422torgb32(unsigned char x,unsigned y ,unsigned char * input_ptr, unsigned char * output_ptr, unsigned int image_width, unsigned int image_height)
{
	unsigned int i, size,j;
	unsigned char Y, Y1, U, V;
	unsigned char *buff = input_ptr; //�����ַ-Դ����
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

	//��LCD����ʾ�ɼ�������ͼ����ʼ��ַ
	src = output_ptr; //RGB����
//	save_bmp(output_ptr); //����BMPͼƬ
	src_bmp=src;
	//������ʾ����ʼ��ַ
	dst = fbmem+var.xres*var.bits_per_pixel/8*y+x*var.bits_per_pixel/8;

	for(j=0;j<image_height;j++)
	{
		show_line(src,dst,image_width);
		src+=(image_width*3);
		dst+=line_width;
	}
	return 0;
	
} 

/* �ο�luvcview */


//YUVתRGB
static int Yuv2RgbConvert(unsigned char x,unsigned char y,PT_PixelDatas ptVideoBufIn, PT_PixelDatas ptVideoBufOut)
{
	//��������ָ��
	PT_PixelDatas ptPixelDatasIn  = ptVideoBufIn;
	PT_PixelDatas ptPixelDatasOut = ptVideoBufOut;

	/*�����˸�ֵ����*/
	video_buff = ptVideoBufIn;
	ptPixelDatasOut->iWidth  = ptPixelDatasIn->iWidth;
	ptPixelDatasOut->iHeight = ptPixelDatasIn->iHeight;

	ptPixelDatasOut->iBpp = 32; /*����λ��*/
	//һ�е��ֽ���
	ptPixelDatasOut->iLineBytes  = ptPixelDatasOut->iWidth * ptPixelDatasOut->iBpp / 8;
	//�ܹ����ֽ���
	ptPixelDatasOut->iTotalBytes = ptPixelDatasOut->iLineBytes * ptPixelDatasOut->iHeight;

	//����һ֡���ݵĿռ�
	if (!ptPixelDatasOut->aucPixelDatas) //�жϴ�����ص�ַ�Ƿ�Ϊ�� 
	{
		ptPixelDatasOut->aucPixelDatas = malloc(ptPixelDatasOut->iTotalBytes); //����ռ�
	}

	//YUV422תRGB
	Pyuv422torgb32(x,y,ptPixelDatasIn->aucPixelDatas, ptPixelDatasOut->aucPixelDatas, ptPixelDatasOut->iWidth, ptPixelDatasOut->iHeight);
	return 0;
}


//����ͷ�豸�ĳ�ʼ��
static int camera_init(void)
{

	int i=0;
	int cnt=0;
	int error;

	//��������ͷ������BUF�Ĺ��ܲ�����Ƶ
	int iType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	
	/* 1������Ƶ�豸 */
	iFd = open("/dev/video15",O_RDWR);
	if(iFd < 0)
	{
		printf("can't open /dev/video\n");
		return 0;
	}

	struct v4l2_capability tV4L2Cap; //��ȡ����ͷ����
	/* 2��VIDIOC_QUERYCAP ȷ�����Ƿ���Ƶ��׽�豸,֧�����ֽӿ�(streaming/read,write) */
	error = ioctl(iFd,VIDIOC_QUERYCAP,&tV4L2Cap);
	if(error)
	{
		printf("no this video device\n");
		return -1;
	}
	
	/* 2.1������Ƿ���ƵCAPTURE�豸 */
	if (!(tV4L2Cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
    	printf("not a video capture device\n");
        return -1;
    }
	
	/* 2.2��֧�����ֽӿ�:mmap read/write */
	if (tV4L2Cap.capabilities & V4L2_CAP_STREAMING) 
	{
		 printf("supports streaming i/o\n");
	}

    //�ж��Ƿ�֧����ͨ�Ķ�дIO
	if (tV4L2Cap.capabilities & V4L2_CAP_READWRITE) 
	{
	    printf("supports read i/o\n");
	}

	struct v4l2_fmtdesc tV4L2FmtDesc; //��������ͷ֧�ֵĸ�ʽ
	/* 3��VIDIOC_ENUM_FMT ��ѯ֧�����ָ�ʽ */
	memset(&tV4L2FmtDesc, 0, sizeof(tV4L2FmtDesc));
	tV4L2FmtDesc.index = 0; //������ţ������±�
	tV4L2FmtDesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; //֧����Ƶ����

	//�������豸֧�ֵ�ͼ���ʽ
	while ((error = ioctl(iFd,VIDIOC_ENUM_FMT, &tV4L2FmtDesc)) == 0) 
	{
		printf("ok %d\n",++cnt);
		
		//�ж϶�����ͼƬ���ݸ�ʽ
        if (!isSpFmt(tV4L2FmtDesc.pixelformat))
        {
            printf("Support :%d\n",tV4L2FmtDesc.pixelformat);
            break;
        }
		tV4L2FmtDesc.index++;
	}

	struct v4l2_format  tV4l2Fmt;
	/* 4�� VIDIOC_S_FMT ��������ͷʹ�����ָ�ʽ */
	memset(&tV4l2Fmt, 0, sizeof(struct v4l2_format));
	tV4l2Fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; //��Ƶ����

	//��������ͷ�����ͼ���ʽ

	if(tV4L2FmtDesc.pixelformat!=V4L2_PIX_FMT_YUYV)
	{
		tV4l2Fmt.fmt.pix.pixelformat=V4L2_PIX_FMT_YUYV;
	}else
	{
		tV4l2Fmt.fmt.pix.pixelformat = tV4L2FmtDesc.pixelformat;
	}
	
	/*�޸���ʾ�ĳߴ�---��LCD����ʾ��λ��*/
	tV4l2Fmt.fmt.pix.width       = 480;
	tV4l2Fmt.fmt.pix.height      = 800;
	tV4l2Fmt.fmt.pix.field       = V4L2_FIELD_ANY;
	
    /* ��������������޷�ĳЩ����(����ֱ���),
     * ���������Щ����, ���ҷ��ظ�Ӧ�ó���
     */
    //VIDIOC_S_FMT ��������ͷ���������
    error = ioctl(iFd, VIDIOC_S_FMT, &tV4l2Fmt); 
    if (error) 
    {
    	printf("Unable to set format\n");
       return -1;   
    }

	//��ӡ����ͷʵ�ʵ��������
	printf("Support Format:%d\n",tV4l2Fmt.fmt.pix.pixelformat);
	printf("Support width:%d\n",tV4l2Fmt.fmt.pix.width);
	printf("Support height:%d\n",tV4l2Fmt.fmt.pix.height);


	/* ��ʼ��ptVideoBufIn�ṹ�壬Ϊת����׼�� */
	ptVideoBufIn.iBpp = (tV4l2Fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV) ? 24 : \
                                        (tV4l2Fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG) ? 0 :  \
                                        (tV4l2Fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB565) ? 16 :  \
                                        0;
	//�߶� �Ϳ�ȵĸ�ֵ
	ptVideoBufIn.iHeight = tV4l2Fmt.fmt.pix.height;
	ptVideoBufIn.iWidth = tV4l2Fmt.fmt.pix.width;

	//һ������Ҫ���ֽ���
	ptVideoBufIn.iLineBytes = ptVideoBufIn.iWidth*ptVideoBufIn.iBpp/8;
    //һ֡ͼ����ֽ���
	ptVideoBufIn.iTotalBytes = ptVideoBufIn.iLineBytes * ptVideoBufIn.iHeight;
	printf("ptVideoBufIn.iBpp = %d\n",ptVideoBufIn.iBpp);

	
	//v412��������
	struct v4l2_requestbuffers tV4l2ReqBuffs;
	
	/* 5��VIDIOC_REQBUFS  ����buffer */
	memset(&tV4l2ReqBuffs, 0, sizeof(struct v4l2_requestbuffers));

	/* ����4��buffer:ʵ������VIDIOC_REQBUFS��ȡ������Ϣ������ */
	tV4l2ReqBuffs.count   = 4;
	/*֧����Ƶ������*/
	tV4l2ReqBuffs.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	/* ��ʾ����Ļ�����֧��MMAP */
	tV4l2ReqBuffs.memory  = V4L2_MEMORY_MMAP;
	/* Ϊ����buffer��׼�� */
	error = ioctl(iFd, VIDIOC_REQBUFS, &tV4l2ReqBuffs);
	if (error) 
	{
		printf("Unable to allocate buffers.\n");
	    return -1;     
	}
	
	/* �ж��Ƿ�֧��mmap */
	if (tV4L2Cap.capabilities & V4L2_CAP_STREAMING)
	{
		 /* map the buffers */
        for (i = 0; i < tV4l2ReqBuffs.count; i++) 
        {
        	memset(&tV4l2Buf, 0, sizeof(struct v4l2_buffer));
        	tV4l2Buf.index = i;
        	tV4l2Buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        	tV4l2Buf.memory = V4L2_MEMORY_MMAP;

			/* 6��VIDIOC_QUERYBUF ȷ��ÿһ��buffer����Ϣ ���� mmap */
        	error = ioctl(iFd, VIDIOC_QUERYBUF, &tV4l2Buf);
		    if (error) 
				{
					    printf("Unable to query buffer.\n");
					   return -1;
				}

		 //��ӡ������
          printf("length = %d\n",tV4l2Buf.length);

			//ӳ��ռ��ַ
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


	/* 7��VIDIOC_QBUF  ������� */
    for (i = 0; i <tV4l2ReqBuffs.count; i++) 
    {
    	memset(&tV4l2Buf, 0, sizeof(struct v4l2_buffer));
    	tV4l2Buf.index = i;
    	tV4l2Buf.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    	tV4l2Buf.memory = V4L2_MEMORY_MMAP;

		//��BUF�������-
    	error = ioctl(iFd, VIDIOC_QBUF, &tV4l2Buf);
    	if (error)
        {
    	    printf("Unable to queue buffer.\n");
    	  	 return -1;
    	}
    }
	printf("ready to read data\n");

	
	/* 8����������ͷ��ʼ������
	VIDIOC_STREAMON :����
	*/
    error = ioctl(iFd, VIDIOC_STREAMON, &iType);
    if (error) 
    {
    	printf("Unable to start capture.\n");
    	return -1;
    }
	return 0;
}



/*��ȡ��ʾ��Ƶ���߳�*/
void *camera_pthread(void * arg)
{
	int error;
	int cnt=0;
	int i=0;
	int ListNum;
	
	struct pollfd fds[1];
	
	/* 8.1��ʹ��poll���ȴ��Ƿ������� */
	fds[0].fd = iFd;
	fds[0].events=POLLIN;
	
	/* YUV��ʽ������<------>��LCD����ʾ:rgb888 */
	initLut();
	ptVideoBufOut.aucPixelDatas=NULL; /*��ʼ��ַָ���*/

	while(1)
	{
		//printf("wait data....-->\n");
		error = poll(fds, 1, -1);
		memset(&tV4l2Buf, 0, sizeof(struct v4l2_buffer));
		tV4l2Buf.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE; //����
		tV4l2Buf.memory  = V4L2_MEMORY_MMAP; //�洢�ռ�����

		/* 9��VIDIOC_DQBUF    �Ӷ�����ȡ�� */
		//printf("wait ioctl data....-->\n");
		error = ioctl(iFd, VIDIOC_DQBUF, &tV4l2Buf); //ȡ��һ֡����
		ListNum = tV4l2Buf.index; //�������
		//printf("listnum1:%d\n",ListNum); //��ӡ�������

		/*��ַ��ֵ
		pucVideBuf[ListNum]: �������ͷ���������
		*/
		ptVideoBufIn.aucPixelDatas = pucVideBuf[ListNum];

		//��LCD������ʾת��������
		Yuv2RgbConvert(0,0,&ptVideoBufIn,&ptVideoBufOut);

		memset(&tV4l2Buf, 0, sizeof(struct v4l2_buffer));
		tV4l2Buf.index  = ListNum;
		tV4l2Buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		tV4l2Buf.memory = V4L2_MEMORY_MMAP;
		error = ioctl(iFd, VIDIOC_QBUF, &tV4l2Buf);
		//printf("listnum2:%d\n",tV4l2Buf.index); //��ӡ�������
		//Yuv2RgbConvert(0,0,&ptVideoBufIn,&ptVideoBufOut);

	}
}


int main(int argc ,char *argv[])//./a.out /dev/fb0 
{
	pthread_t camerathread; //����̵߳�ID
	
	LcdOpen("/dev/fb0");//��lcd   
	
	if(camera_init()) //������ͷ�豸
	{
		close(iFd);
		printf("camera init fail\n");
	}	

	fd_key=open("/dev/test_led_0",2);
		/*1. ����Ҫ������ź� */ 
	signal(SIGIO,sighandler);
	/*2. ����pid*/	
	int f_flags;
	fcntl(fd_key,F_SETOWN,getpid());//�������ļ���������ֵ�����̵�PID��?	/*3. ���������ļ�֧���첽IO*/
	f_flags = fcntl(fd_key,F_GETFL);  //��ȡ��ǰ���ļ�����,����ֵ���ǻ�ȡ������
	fcntl(fd_key,F_SETFL,f_flags|FASYNC);   //���õ�ǰ����֧���첽IO����
	key_flag=1;
	
	pthread_create(&camerathread,NULL,camera_pthread,NULL); //����ͷ�߳�
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
								����ΪBMP��ʽ��ͼƬ����
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

//��ɫ��
typedef struct tagRGBQUAD{
     BYTE	rgbBlue;
     BYTE	rgbGreen;
     BYTE	rgbRed;
     BYTE	rgbReserved;
}RGBQUAD;


//����ΪBMP��ʽ���ļ�
void save_bmp(char *src)
{
	
	/*-----------------------------------------------------------
					��ȡʱ�������������ͼƬ����
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
					��ȡͼƬ���ݣ���������ΪBMPͼƬ��ʽ
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
	
		//Set BITMAPINFOHEADER ����BMP��Ϣͷ
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

