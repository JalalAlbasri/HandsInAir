// worker.cpp : Defines the entry point for the console application.
//

#include "StdAfx.h"
#include <ws2tcpip.h>
#include <winsock2.h>

//OpenCV
#include <cv.h>
#include <cxcore.h>
#include <highgui.h>
#include <math.h>
#include <cvaux.h>

//Common
#include <iostream>
#include <stdio.h>
#include <string.h>
#include <sstream>
#include <stdlib.h>
#include <vector>
#include <errno.h>
#include <conio.h>
#include <stdint.h>
/*
//Intel IPP
#include <ipp.h>
#include <ippcore.h>
#include <ippdefs.h>
#include <ippj.h>

//Intel IJG-IPP
extern "C" {
#include <jpeglib.h>
}*/
#include <jpeglib.h>

static const int BUFFER_SIZE = 64000;

/***********************
 
 JPEG HELPER FUNCTIONS
 
 
 *************************/

const static size_t JPEG_BUF_SIZE = 16384;
//const static size_t JPEG_BUF_SIZE = 64000;

struct my_destination_mgr {
    struct jpeg_destination_mgr pub; /* public fields */
    std::ostream* os; /* target stream */
	JOCTET * buffer;	   /* start of buffer */
};

struct my_source_mgr {
    struct jpeg_source_mgr pub;
    std::istream* is;
    JOCTET*       buffer;
};

static void my_init_source(j_decompress_ptr cinfo) {
}

static boolean my_fill_input_buffer(j_decompress_ptr cinfo) {
    my_source_mgr* src = (my_source_mgr*)cinfo->src;
	
    src->is->read((char*)src->buffer, JPEG_BUF_SIZE);
    size_t bytes = src->is->gcount();
    if (bytes == 0) {
		/* Insert a fake EOI marker */
		src->buffer[0] = (JOCTET) 0xFF;
		src->buffer[1] = (JOCTET) JPEG_EOI;
		bytes = 2;
    }
    src->pub.next_input_byte = src->buffer;
    src->pub.bytes_in_buffer = bytes;
    return TRUE;
}

static void my_skip_input_data(j_decompress_ptr cinfo, long num_bytes) {
    my_source_mgr* src = (my_source_mgr*)cinfo->src;
    if (num_bytes > 0) {
		while (num_bytes > (long)src->pub.bytes_in_buffer) {
			num_bytes -= (long)src->pub.bytes_in_buffer;
			my_fill_input_buffer(cinfo);
		}
		src->pub.next_input_byte += num_bytes;
		src->pub.bytes_in_buffer -= num_bytes;
    }
}

static void my_term_source(j_decompress_ptr cinfo) {
    // must seek backward so that future reads will start at correct place.
    my_source_mgr* src = (my_source_mgr*)cinfo->src;
    src->is->clear();
    src->is->seekg( src->is->tellg() - (std::streampos)src->pub.bytes_in_buffer );
}

void my_init_destination (j_compress_ptr cinfo) {
    my_destination_mgr* dest = (my_destination_mgr*) cinfo->dest;
    dest->buffer = (JOCTET *)(*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_IMAGE,
                                                         JPEG_BUF_SIZE * sizeof(JOCTET));
    dest->pub.next_output_byte = dest->buffer;
    dest->pub.free_in_buffer = JPEG_BUF_SIZE;
}

boolean my_empty_output_buffer(j_compress_ptr cinfo) {
    my_destination_mgr* dest = (my_destination_mgr*)cinfo->dest;
    
    dest->os->write((const char*)dest->buffer, JPEG_BUF_SIZE);
    if (dest->os->fail()) {
		//   LOG_FATAL("Couldn't write entire jpeg buffer to stream.");
    }
    dest->pub.next_output_byte = dest->buffer;
    dest->pub.free_in_buffer = JPEG_BUF_SIZE;
    return TRUE;
}

void my_term_destination (j_compress_ptr cinfo) {
    my_destination_mgr* dest = (my_destination_mgr*) cinfo->dest;
    size_t datacount = JPEG_BUF_SIZE - dest->pub.free_in_buffer;
    
    /* Write any data remaining in the buffer */
    if (datacount > 0) {
		dest->os->write((const char*)dest->buffer, datacount);
		if (dest->os->fail()) {
			//  LOG_FATAL("Couldn't write remaining jpeg data to stream.");
		}
    }
    dest->os->flush();

}

SOCKET openServerSocket(void);
SOCKET openClientSocket(void);

void writeJpeg(std::ostream& os, const IplImage* img_);
IplImage* readJpeg(std::istream& is);

using namespace std;

static const int JPEG_QUALITY = 75;
const CvSize capture_size = cvSize(640, 480);

static CvAdaptiveSkinDetector asd = CvAdaptiveSkinDetector(1, 1);

static const char* LOCAL_ADDR = NULL;
static const char* REMOTE_ADDR = NULL;

static const char* LOCAL_PORT = "49000\0";
static const char* REMOTE_PORT = "50000\0";

struct jpeg_compress_struct cinfo;
struct jpeg_decompress_struct dinfo;
struct jpeg_error_mgr cjerr;
struct jpeg_error_mgr djerr;

my_destination_mgr* dest;
my_source_mgr* src;

int r;

int _tmain(int argc, _TCHAR* argv[])
{
	//Initialize Winsock
	WORD sockVersion;
	WSADATA wsaData;
	sockVersion = MAKEWORD(1, 1);
	WSAStartup(sockVersion, &wsaData);

	int bytes_recv = 0, bytes_sent = 0, i;
	IplImage* recv_frame;
	IplImage* display_frame;
	IplImage* combined;
	IplImage* hue_mask;
	IplImage* rgb;
	CvCapture* capture;
	capture = cvCreateCameraCapture(-1);
	
	CvScalar zero = cvScalar(0, 0, 0, 0);
	CvAdaptiveSkinDetector asd = CvAdaptiveSkinDetector(1, 1);

	
	SOCKET send_socket, receive_socket;

	char send_buffer[BUFFER_SIZE], receive_buffer[BUFFER_SIZE];

	cvNamedWindow("Worker", CV_WINDOW_NORMAL );

	//Check that Camera is Operational
	if(!cvQueryFrame(capture))
	{
		cout << ">< WORKER_CLIENT: Video capture failed, please check the camera." << endl;
		exit(1);
	}
	else 
		cout << "_/ WORKER_CLIENT: Video camera capture status OK" << endl;
	
	
	recv_frame = cvCreateImage(capture_size, IPL_DEPTH_8U, 3);
	hue_mask = cvCreateImage(capture_size, IPL_DEPTH_8U, 1);
	display_frame = cvCreateImage(capture_size, IPL_DEPTH_8U, 3);
	rgb = cvCreateImage(capture_size, IPL_DEPTH_8U, 3);	

	receive_socket = openClientSocket();
	send_socket = openServerSocket();	

	//set up compression structure
    cinfo.err = jpeg_std_error(&cjerr);
    jpeg_create_compress(&cinfo);
	cinfo.image_width      = capture_size.width;
    cinfo.image_height     = capture_size.height;
    cinfo.input_components = 3;
	cinfo.in_color_space   = JCS_RGB;
	jpeg_set_defaults(&cinfo);
    jpeg_set_quality (&cinfo, JPEG_QUALITY, true);

	cinfo.dest = (struct jpeg_destination_mgr *)
    (*cinfo.mem->alloc_small) ((j_common_ptr) &cinfo, JPOOL_PERMANENT, sizeof(my_destination_mgr));

    dest = (my_destination_mgr*)cinfo.dest;
    dest->pub.init_destination = my_init_destination;
    dest->pub.empty_output_buffer = my_empty_output_buffer;
    dest->pub.term_destination = my_term_destination;
	
	//setup decompression structure
	dinfo.err = jpeg_std_error(&djerr); 
    jpeg_create_decompress(&dinfo);

	dinfo.src = (struct jpeg_source_mgr *)(*dinfo.mem->alloc_small)
    ((j_common_ptr) &dinfo, JPOOL_PERMANENT, sizeof(my_source_mgr));
		
	src = (my_source_mgr*) dinfo.src;
	src->buffer = (JOCTET *)(*dinfo.mem->alloc_small)
    ((j_common_ptr) &dinfo, JPOOL_PERMANENT, JPEG_BUF_SIZE*sizeof(JOCTET));
		
	while(1) 
	{
		bytes_sent = 0;
		bytes_recv = 0;
		stringstream jsrc_buff, jdst_buff;
		
		//RECEIVE
		for (i = 0; i < BUFFER_SIZE; i += bytes_recv)
		{
			if ( (bytes_recv = recv(receive_socket, receive_buffer+i, BUFFER_SIZE-i, 0)) == -1)
			{
				perror("recv");
				cout << WSAGetLastError() << endl;
				exit(1);
			}
			cout << "i: " << i << endl;
		}
		cout << "BYTES RECEIVED: " << bytes_recv << endl;
		
		jsrc_buff.write(receive_buffer, BUFFER_SIZE);
		
		src->is = &jsrc_buff;
		src->pub.init_source = my_init_source;
		src->pub.fill_input_buffer = my_fill_input_buffer;
		src->pub.skip_input_data = my_skip_input_data;
		src->pub.resync_to_restart = jpeg_resync_to_restart; /* use default method */
		src->pub.term_source = my_term_source;
		src->pub.bytes_in_buffer = 0;
		src->pub.next_input_byte = 0;

		//DECOMPRESS INTO IPLIMAGE
		// read info from header.
		r = jpeg_read_header(&dinfo, TRUE);

		if (r != JPEG_HEADER_OK) 
		{
			//  LOG_FATAL("Failed to read JPEG header.");
		} 
		else if (cinfo.num_components != 3 && cinfo.num_components != 1)
		{
			//  LOG_FATAL("Unsupported number of color components: " 
			//                << cinfo.num_components);
		}
		else
		{
			jpeg_start_decompress(&dinfo);
			// resize storage if necessary

			JSAMPARRAY imageBuffer = (*dinfo.mem->alloc_sarray)((j_common_ptr)&dinfo, JPOOL_IMAGE, 
																dinfo.output_width*dinfo.output_components, 1);
			for (int y = 0; y < dinfo.output_height; y++) {
				jpeg_read_scanlines(&dinfo, imageBuffer, 1);
				uint8_t* dstRow = (uint8_t*)recv_frame->imageData + recv_frame->widthStep*y;
				memcpy(dstRow, imageBuffer[0], dinfo.output_width*dinfo.output_components);
			}
			// for rgb images, reverse octets
			if (cinfo.num_components == 3) {
				cvCvtColor(recv_frame, recv_frame, CV_RGB2BGR);
			}
		
			jpeg_finish_decompress(&dinfo);
		}

		jsrc_buff.flush();

		asd.process(recv_frame, hue_mask);
		
//		display_frame = cvQueryFrame(capture);

		cvAddS(recv_frame, zero, display_frame, hue_mask);
		
		cvCvtColor(display_frame, rgb, CV_BGR2RGB);
		display_frame = rgb;
		
		dest->os = &jdst_buff;
		jpeg_start_compress(&cinfo, true);

		JSAMPROW row;
		while (cinfo.next_scanline < cinfo.image_height) {
			row = (JSAMPROW)((char*)display_frame->imageData + cinfo.next_scanline*display_frame->widthStep);
			jpeg_write_scanlines(&cinfo, &row, 1);
		}
		jpeg_finish_compress(&cinfo);
		jdst_buff.flush();

		cout << "JPEG SIZE: " << jdst_buff.str().length() << endl;
		if (jdst_buff.str().length() < BUFFER_SIZE) 
		{
			//copy from ss into sending buffer
			jdst_buff.read(send_buffer, jdst_buff.str().length());
		
			if ((bytes_sent = send(send_socket, send_buffer, BUFFER_SIZE, 0)) == -1)
			{
				perror("SERVER: send");
				cout << WSAGetLastError() << endl;
				exit(1);
			}
		}
		cout << "BYTES SENT: " << bytes_sent << endl;
		
		cvShowImage("Worker", display_frame);
		//cvWaitKey(5);
		if (cvWaitKey(1) == 27)
		break;
	}
	
	cvWaitKey();
	closesocket(receive_socket);
	closesocket(send_socket);
	WSACleanup();
	cvDestroyAllWindows();
	jpeg_destroy_compress(&cinfo);
	jpeg_destroy_decompress(&dinfo);
	return 0;
}

SOCKET openServerSocket(void) {
	SOCKET sockfd, new_fd;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	
	//if ((rv = getaddrinfo("169.254.42.235", LOCAL_PORT, &hints, &servinfo)) != 0) { //node name NULL for local
	if ((rv = getaddrinfo(NULL, LOCAL_PORT, &hints, &servinfo)) != 0) { //node name NULL for local
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		cout << "OPEN SERVER SOCKET: getaddrinfo" << endl;
		cout << WSAGetLastError() << endl;
		return 1;
	}
	
	// loop through all the results and make a socket
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("SERVER: socket");
			continue;
		}
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			closesocket(sockfd);
			perror("SERVER: bind");
			continue;
		}
		break;
	}
	
	if (p == NULL) {
		fprintf(stderr, "SERVER: failed to bind socket\n");
		exit(1);
	}
	
	//Listin to socket for an incoming connection and accept if one comes in.
	if (listen(sockfd, 1) == -1) {
		perror(">< OPEN_SERVER_SOCKET:> listen");
		//cout << WSAGetLastError() << endl;
		exit(1);
	}
	else {
		struct sockaddr_storage their_addr;
		socklen_t addr_size = sizeof their_addr;
		if((new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size)) == -1) {
			perror(">< OPEN_SERVER_SOCKET:> accept");
			//cout << WSAGetLastError() << endl;
		}
		else {
			cout << "_/ OPEN SERVER SOCKET: Socket Successfully Opened" << endl;
			return new_fd;
		}
	}
	return -1;
}

SOCKET openClientSocket(void) {

	int sockfd, rv;
	struct addrinfo hints, *servinfo, *p;
	//struct sockaddr_storage their_addr;
	//socklen_t addr_len;
	
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET; // set to AF_INET to force IPv4
	hints.ai_socktype = SOCK_STREAM;
	//hints.ai_flags = AI_PASSIVE; // use my IP
	
	//For getaddrinfo, use NULL to connect to local, use IP address to connect to remote.
	if ((rv = getaddrinfo(NULL, REMOTE_PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}
	
	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("CLIENT: socket");
			continue;
		}
		break;
	}
	
	if (p == NULL) {
		fprintf(stderr, "CLIENT: failed to bind socket\n");
		return 2;
	}
		
	//Connect to the server, 

	while (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {	
		cout << "Attempting to connect to server..." << endl;
		perror("CLIENT: connect");
		cout << WSAGetLastError() << endl;
	}
	
	return sockfd;
}
