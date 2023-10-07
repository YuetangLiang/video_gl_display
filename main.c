#include <stdio.h>

#include <GL/glew.h>
#include <GL/glut.h>

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <fcntl.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>

#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

//Select one of the Texture mode (Set '1'):
#define TEXTURE_DEFAULT   0
//Rotate the texture
#define TEXTURE_ROTATE    0
//Show half of the Texture
#define TEXTURE_HALF      0

int screen_w = 1280, screen_h = 720;
int pixel_w = 1280, pixel_h = 720;
int frame_len = 0;
unsigned char *frame_buf = NULL;
unsigned char *plane[3];
int format = 0; //0 yuv_420p, 1 yuv_420sp, 2 yuv_uyvy

GLuint p;                
GLuint id_y, id_u, id_v; // Texture id
GLuint textureUniformY, textureUniformU,textureUniformV;

static pthread_cond_t gpu_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t gpu_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t save_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t save_mutex = PTHREAD_MUTEX_INITIALIZER;

static AVFrame *frame = NULL;
static AVCodecContext *context = NULL;
static AVCodec *codec = NULL;
static AVPacket packet = {0};

static int s_save = 0;
static int s_exit = 0;
static char *s_save_file = "./test.h264";
static int s_save_frames = 0;
static int s_channel = -1;

static char *s_save_buf = NULL;
static int s_save_len = 0;

#define IMAGE_COUNT 3
char *images[IMAGE_COUNT] = {0};
int image_id = 0;

#define ATTRIB_VERTEX 3
#define ATTRIB_TEXTURE 4
#define CHANNEL_NUM_MAX 4
#define AVTP_STREAMID_BASE   0x3350

int avtp_channel(int streamId)
{
    return (streamId - AVTP_STREAMID_BASE - 1) >> 1; 
    //streamId: (x-1)/2 : 0x3351=>0, 0x3353=>1, 0x3355=>2, 0x3357=>3
}

int h264_dec(AVCodecContext *context, AVFrame *frame, AVPacket *packet, char *yuv420p)
{
    int i = 0;
    int offset = 0;
    int finished = 0;
    int uv_width = context->width / 2;
    int uv_height = context->height / 2;

    avcodec_decode_video2(context, frame, &finished, packet);

    if (finished)
    {
        //Y
	    for (i = 0; i < context->height; i++)
	    {
	        memcpy(yuv420p + offset, frame->data[0] + i * frame->linesize[0], context->width);
	        offset += context->width;
	    }

	    //U
	    for (i = 0; i < uv_height; i++)
	    {
	        memcpy(yuv420p + offset, frame->data[1] + i * frame->linesize[1], uv_width);
	        offset += uv_width;
	    }

	    //V
	    for (i = 0; i < uv_height; i++)
	    {
	        memcpy(yuv420p + offset, frame->data[2] + i * frame->linesize[2], uv_width);
	        offset += uv_width;
	    }
    }

    return 0;
}

void display(void)
{
	//Clear
	glClearColor(0.0,255,0.0,0.0);
	glClear(GL_COLOR_BUFFER_BIT);
	
	//Y
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, id_y);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, pixel_w, pixel_h, 0, GL_RED, GL_UNSIGNED_BYTE, plane[0]); 
	glUniform1i(textureUniformY, 0); 
	   
	//U
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, id_u);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, pixel_w/2, pixel_h/2, 0, GL_RED, GL_UNSIGNED_BYTE, plane[1]);       
    glUniform1i(textureUniformU, 1);
    
	//V
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, id_v);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, pixel_w/2, pixel_h/2, 0, GL_RED, GL_UNSIGNED_BYTE, plane[2]);    
    glUniform1i(textureUniformV, 2);   

    // Draw
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
	// Show
    glutSwapBuffers();
}

#define AVTP_HEADER_LENGTH (14 + 36 + 4) // Mac Header (14byte) + AVTP Header (36Byte) + CRC (4Byte)

int set_promisc(const char *interface, int sd)
{
    struct ifreq ethreq;
    strncpy(ethreq.ifr_name, interface, IFNAMSIZ);
    
    if (ioctl(sd, SIOCGIFFLAGS, &ethreq) == -1)
    {
	printf("ioctl SIOCGIFFLAGS error: %s\n", strerror(errno));
	return -1;
    }

    ethreq.ifr_flags |= IFF_PROMISC;
    if (ioctl(sd, SIOCSIFFLAGS, &ethreq) == -1)
    {
	printf("ioctl SIOCSIFFLAGS error: %s\n", strerror(errno));
	return -1;
    }

    return 0;
}

void* save_thread(void *arg)
{
    int fd = -1;
    
    if ((fd = open(s_save_file, O_WRONLY | O_CREAT, 0666)) <= 0)
	{
	    printf("open %s error: %s\n", s_save_file, strerror(errno));
	    printf("save_thread exit\n");
	    return 0;
	}
	
    while (!s_exit)
    {
        if (!s_save_buf)
        {
            pthread_mutex_lock(&save_mutex);
	        pthread_cond_wait(&save_cond, &save_mutex);
	        pthread_mutex_unlock(&save_mutex);
	    }
        
        if (s_save_buf)
        {
	        write(fd, s_save_buf, s_save_len);
	    }
	    
	    s_save_buf = NULL;
    }
    
    close(fd);
}

void* avb_thread(void *arg)
{
    int sd = 0;
    ssize_t size = 0;
    unsigned char buf[2048] = {0};
    unsigned char *h264 = NULL;
    struct sockaddr src_addr;
    socklen_t addrlen = 0;
    int fd;
    bool start = false;

    int frames = 0;
    int h264_len = 0;
    int type = 0;
    int sn = -1;
    int sn_old = -1;
    int offset = 0;
    int count = 0;
    int streamId = 0;
    int channel = -1;

    av_log_set_level(AV_LOG_QUIET);

    int i = 0;
    for (i = 0; i < IMAGE_COUNT; i++)
    {
	    images[i] = (char*)malloc(pixel_w * pixel_h * 3 / 2);
    }

    if ((sd = socket(PF_PACKET, SOCK_RAW, htons(0x22F0))) < 0)
    {
	    printf("socket error: %s\n", strerror(errno));
	    return NULL;
    }

    set_promisc("enp7s0", sd);

    do
    {
	    memset(&src_addr, 0, sizeof(struct sockaddr));
	    memset(buf, 0, 2048);
        size = recvfrom(sd, buf, 2048, 0, &src_addr, &addrlen);

	    if (size < AVTP_HEADER_LENGTH)
	    {
	        printf("recvfrom error: %s\n", strerror(errno));
	        break;
	    }
	    
	    streamId = buf[24] << 8;
	    streamId += buf[25];

	    channel = avtp_channel(streamId);
	    if (channel >= CHANNEL_NUM_MAX || channel < 0)
	    {
	        printf("channnel = %d, not support\n", channel);
	        continue;
	    }
	
	    if (s_channel >= 0 && s_channel != channel)
	    {
	        continue; 
	    }

	    h264_len = buf[34] << 8;
	    h264_len += buf[35];

	    if (size < AVTP_HEADER_LENGTH + h264_len)
	    {
            printf("avb data error, size=%ld, h264_len=%d\n", size, h264_len);
	        break;
	    }

	    sn = buf[16];
	    if (1 != sn - sn_old && 255 != sn_old - sn && sn_old != -1)
	    {
	        printf("error: sequence number = %d, pre = %d\n", sn, sn_old);
	        offset = 0;
	    }
	    sn_old = sn;

	    h264 = &buf[AVTP_HEADER_LENGTH];

	    if (h264[0] == 0x0 && h264[1] == 0x0 && h264[2] == 0x0 && h264[3]== 0x1 && (h264[4] & 0x1F) == 0x7)
	    {
	        start = true;
	        if (offset > 0)
	        {
		        //decode
                packet.data = images[image_id];
                packet.size = offset;
                		
		        pthread_mutex_lock(&gpu_mutex);
		        pthread_cond_signal(&gpu_cond);
		        pthread_mutex_unlock(&gpu_mutex);
		
		        if (s_save)
		        {
		            s_save_buf = images[image_id];
		            s_save_len = offset;
		    
		            pthread_mutex_lock(&save_mutex);
		            pthread_cond_signal(&save_cond);
		            pthread_mutex_unlock(&save_mutex);
		        }

		        image_id++;
		        image_id = image_id % IMAGE_COUNT;

	            offset = 0;
	        }
	    }

	    if (start)
	    {
	        memcpy(images[image_id] + offset, h264, h264_len);
	        offset += h264_len;
	    }
    } while (!s_exit);

    pthread_mutex_lock(&gpu_mutex);
    pthread_cond_signal(&gpu_cond);
    pthread_mutex_unlock(&gpu_mutex);
    
    pthread_mutex_lock(&save_mutex);
    pthread_cond_signal(&save_cond);
    pthread_mutex_unlock(&save_mutex);

    close(sd);

    return NULL;
}

void timeFunc(int value)
{
    while (!s_exit)
    {
        pthread_mutex_lock(&gpu_mutex);
	    pthread_cond_wait(&gpu_cond, &gpu_mutex);
	    pthread_mutex_unlock(&gpu_mutex);
        
	    h264_dec(context, frame, &packet, frame_buf);
	    
	    display();
    }
}

char *textFileRead(char * filename)
{
    char *s = (char *)malloc(8000);
    memset(s, 0, 8000);
    FILE *infile = fopen(filename, "rb");
    int len = fread(s, 1, 8000, infile);
    fclose(infile);
    s[len] = 0;
    
    return s;
}

//Init Shader
void InitShaders()
{
    GLint vertCompiled, fragCompiled, linked;
    
    GLint v, f;
    const char *vs,*fs;
    
	//Shader: step1
    v = glCreateShader(GL_VERTEX_SHADER);
    f = glCreateShader(GL_FRAGMENT_SHADER);
    
	//Get source code
    vs = textFileRead("./shader.vert");
    fs = textFileRead("./shader.frag");
    
	//Shader: step2
    glShaderSource(v, 1, &vs, NULL);
    glShaderSource(f, 1, &fs, NULL);
    
	//Shader: step3
    glCompileShader(v);
	//Debug
    glGetShaderiv(v, GL_COMPILE_STATUS, &vertCompiled);
    glCompileShader(f);
    glGetShaderiv(f, GL_COMPILE_STATUS, &fragCompiled);

	//Program: Step1
    p = glCreateProgram(); 
	//Program: Step2
    glAttachShader(p,v);
    glAttachShader(p,f); 

    glBindAttribLocation(p, ATTRIB_VERTEX, "vertexIn");
    glBindAttribLocation(p, ATTRIB_TEXTURE, "textureIn");
	//Program: Step3
    glLinkProgram(p);
	//Debug
    glGetProgramiv(p, GL_LINK_STATUS, &linked);  
	//Program: Step4
    glUseProgram(p);


	//Get Uniform Variables Location
	textureUniformY = glGetUniformLocation(p, "tex_y");
	textureUniformU = glGetUniformLocation(p, "tex_u");
	textureUniformV = glGetUniformLocation(p, "tex_v"); 

#if TEXTURE_ROTATE
    static const GLfloat vertexVertices[] = {
        -1.0f, -0.5f,
         0.5f, -1.0f,
        -0.5f,  1.0f,
         1.0f,  0.5f,
    };    
#else
	static const GLfloat vertexVertices[] = {
		-1.0f, -1.0f,
		1.0f, -1.0f,
		-1.0f,  1.0f,
		1.0f,  1.0f,
	};    
#endif

#if TEXTURE_HALF
	static const GLfloat textureVertices[] = {
		0.0f,  1.0f,
		0.5f,  1.0f,
		0.0f,  0.0f,
		0.5f,  0.0f,
	}; 
#else
	static const GLfloat textureVertices[] = {
		0.0f,  1.0f,
		1.0f,  1.0f,
		0.0f,  0.0f,
		1.0f,  0.0f,
	}; 
#endif

	//Set Arrays
    glVertexAttribPointer(ATTRIB_VERTEX, 2, GL_FLOAT, 0, 0, vertexVertices);
	//Enable it
    glEnableVertexAttribArray(ATTRIB_VERTEX);    
    glVertexAttribPointer(ATTRIB_TEXTURE, 2, GL_FLOAT, 0, 0, textureVertices);
    glEnableVertexAttribArray(ATTRIB_TEXTURE);


	//Init Texture
    glGenTextures(1, &id_y); 
    glBindTexture(GL_TEXTURE_2D, id_y);    
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glGenTextures(1, &id_u);
    glBindTexture(GL_TEXTURE_2D, id_u);   
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glGenTextures(1, &id_v); 
    glBindTexture(GL_TEXTURE_2D, id_v);    
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void signal_fun(int signo)
{
    s_exit = 1;
}

int main(int argc, char* argv[])
{
    char title[256] = {0};
    pthread_t pid_avb = 0;
    pthread_t pid_save = 0;
    
    printf("usage: %s [channel file]\n", argv[0]);
    
    if (argc > 2)
    {
	    s_save = 1;
	    s_channel = atoi(argv[1]) - 1;
	    s_save_file = argv[2];
	    
    }
	
    signal(SIGINT, signal_fun);
    
    frame_len = pixel_w * pixel_h * 3 / 2;
    frame_buf = (char*)malloc(frame_len);
    
    avcodec_register_all();
    codec = avcodec_find_decoder(CODEC_ID_H264);
    context = avcodec_alloc_context3(codec);
    context->time_base.num = 1;
    context->frame_number = 1;
    context->codec_type = AVMEDIA_TYPE_VIDEO;
    context->bit_rate = 0;
    context->time_base.den = 30;//30fps
    context->width = pixel_w;
    context->height = pixel_h;

    avcodec_open2(context, codec, NULL);
    frame = av_frame_alloc();
    
	//YUV Data
    plane[0] = frame_buf;
    plane[1] = plane[0] + pixel_w * pixel_h;
    plane[2] = plane[1] + pixel_w * pixel_h / 4;

    //Init GLUT
    glutInit(&argc, argv);  
	//GLUT_DOUBLE
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_STENCIL | GLUT_DEPTH);
    glutInitWindowPosition(0, 0);
    glutInitWindowSize(screen_w, screen_h);

    sprintf(title, "YUV Video Player %dx%d", pixel_w, pixel_h);
    glutCreateWindow(title);
    printf("Version: %s\n", glGetString(GL_VERSION)); 
    
    GLenum l = glewInit();

    glutDisplayFunc(&display);
    glutTimerFunc(30, timeFunc, 0); 

    InitShaders();
    
    pthread_create(&pid_avb, NULL, avb_thread, NULL);
    
    pthread_create(&pid_save, NULL, save_thread, NULL);

    // Begin!
    glutMainLoop();
    
    pthread_join(pid_avb, 0);
    pthread_join(pid_save, 0);

    return 0;
}


