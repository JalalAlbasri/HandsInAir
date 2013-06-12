#include "StdAfx.h"
#include "worker.h"

CEvent* QUIT;
CEvent* SEND;
CEvent* PROCESS;

/* FUNCTION PROTOTYPES */
UINT w_Camera(LPVOID pParam);
UINT w_Send(LPVOID pParam);
UINT w_Server(LPVOID pPARAM);
UINT w_Client(LPVOID pParam);
UINT w_Receive(LPVOID pParam);
UINT w_Display(LPVOID pParam);

using namespace std;

CvAdaptiveSkinDetector asd = CvAdaptiveSkinDetector(1, 0);

//const char* LOCAL_ADDR = NULL;
//const char* REMOTE_ADDR = NULL;
const char* LOCAL_ADDR = "192.168.1.137";
const char* REMOTE_ADDR = "192.168.1.138";

const char* LOCAL_PORT_1 = "50000\0";
const char* LOCAL_PORT_2 = "50001\0";
const char* LOCAL_PORT_3 = "50002\0";
const char* REMOTE_PORT_1 = "49000\0";
const char* REMOTE_PORT_2 = "49001\0";
const char* REMOTE_PORT_3 = "49002\0";

struct jpeg_compress_struct cinfo;
struct jpeg_decompress_struct dinfo;
struct jpeg_error_mgr cjerr;
struct jpeg_error_mgr djerr;

my_destination_mgr* dest;
my_source_mgr* src;

int r;
time_t before, before_cv, total_cv = 0; int reps = 0;
SharedBuffer *SendBuffer, *ReceiveBuffer;

UINT w_ProcessImage(LPVOID pParam)
{	
	
	// Create the Display Window
	cvNamedWindow("Worker", CV_WINDOW_NORMAL );

	//Initialize Images and Buffers
	char jpeg_buffer[BUFFER_SIZE];
	char recv_buffer[BUFFER_SIZE];
	CvSize reduced_size = cvSize(capture_size.width*HAND_REDUCTION, capture_size.height*HAND_REDUCTION);
	IplImage* camera_frame = cvCreateImage(capture_size, IPL_DEPTH_8U, 3);
	IplImage* camera_frame640 = cvCreateImage(cvSize(640, 480), IPL_DEPTH_8U, 3);
	IplImage* recv_frame = cvCreateImage(capture_size, IPL_DEPTH_8U, 3);
	IplImage* recv_frame_reduced = cvCreateImage(reduced_size, IPL_DEPTH_8U, 3);
	IplImage* hue_image = cvCreateImage(reduced_size, IPL_DEPTH_8U, 3);
	IplImage* hue_mask = cvCreateImage(reduced_size, IPL_DEPTH_8U, 1);
	IplImage* rgb = cvCreateImage(capture_size, IPL_DEPTH_8U, 3);
	CvScalar zero = cvScalar(0, 0, 0, 0);

	/* LOCAL_TESTING
	//Check that Camera is Operational
	CvCapture* capture;
	capture = cvCreateCameraCapture(0);
	if(!cvQueryFrame(capture))
	{
		printf("W_PROCESS: Checking Camera Operational....FAILED!\n");
		return 1;
	}
	*/

	//Set Up JPEG Compression Structure
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

	//Set Up JPEG Decompression Structure
	dinfo.err = jpeg_std_error(&djerr); 
    jpeg_create_decompress(&dinfo);

	dinfo.src = (struct jpeg_source_mgr *)(*dinfo.mem->alloc_small)
    ((j_common_ptr) &dinfo, JPOOL_PERMANENT, sizeof(my_source_mgr));
		
	src = (my_source_mgr*) dinfo.src;
	src->buffer = (JOCTET *)(*dinfo.mem->alloc_small)
    ((j_common_ptr) &dinfo, JPOOL_PERMANENT, JPEG_BUF_SIZE*sizeof(JOCTET));
	before = time(NULL);
	while(1) 
	{
		WaitForSingleObject(PROCESS->m_hObject, INFINITE);
		if(ReceiveBuffer->hasImage() != 0)
		{
			stringstream jpeg_destination_buffer, jpeg_source_buffer;

			//Read frames out of the ReceiveBuffer
			ReceiveBuffer->read(recv_buffer);

			//Uncompress them to IplImage format
			jpeg_source_buffer.write(recv_buffer, BUFFER_SIZE);
		
			src->is = &jpeg_source_buffer;
			src->pub.init_source = my_init_source;
			src->pub.fill_input_buffer = my_fill_input_buffer;
			src->pub.skip_input_data = my_skip_input_data;
			src->pub.resync_to_restart = jpeg_resync_to_restart; /* use default method */
			src->pub.term_source = my_term_source;
			src->pub.bytes_in_buffer = 0;
			src->pub.next_input_byte = 0;

			r = jpeg_read_header(&dinfo, TRUE);

			if (r != JPEG_HEADER_OK) 
			{
				printf("W_PROCESS: Decompressing Image....FAILED! Failed to read JPEG header\n");
			} 
			else if (cinfo.num_components != 3 && cinfo.num_components != 1)
			{
				printf("W_PROCESS: Decompressing Image....FAILED! Unsupported number of color components: %d\n", cinfo.num_components);
			}
			else
			{
				jpeg_start_decompress(&dinfo);
				JSAMPARRAY imageBuffer = (*dinfo.mem->alloc_sarray)((j_common_ptr)&dinfo, JPOOL_IMAGE, 
																	dinfo.output_width*dinfo.output_components, 1);
				for (int y = 0; y < dinfo.output_height; y++)
				{
					jpeg_read_scanlines(&dinfo, imageBuffer, 1);
					uint8_t* dstRow = (uint8_t*)recv_frame->imageData + recv_frame->widthStep*y;
					memcpy(dstRow, imageBuffer[0], dinfo.output_width*dinfo.output_components);
				}	
				jpeg_finish_decompress(&dinfo);
			}
			jpeg_source_buffer.flush();
			

			//Perform Hand Detection on recv_frame
			//asd.process(recv_frame, hue_mask);

			/* LOCAL_TESTING
			//Grab a frame from the Camera
			camera_frame640 = cvQueryFrame(capture);
			cvResize(camera_frame640, camera_frame);
			*/
			cvZero(camera_frame);
			before_cv = time(NULL);
			
			//Glove Detection
			cvResize(recv_frame, recv_frame_reduced, CV_INTER_LINEAR); 
			cvCvtColor(recv_frame_reduced, hue_image, CV_BGR2HSV);
			cvInRangeS(hue_image, cvScalar(90, 20, 50), cvScalar(115, 255, 255), hue_mask);

			//Combine camera_frame and recv_frame based on hand_mask
			
			int reduced_width = camera_frame->width*HAND_REDUCTION;
			int reduced_height = camera_frame->height*HAND_REDUCTION;
			CvPoint gesture_corner = cvPoint((camera_frame->width - reduced_width)/2, (camera_frame->height - reduced_height)/2);
			CvRect gesture_area = cvRect(gesture_corner.x, gesture_corner.y, reduced_width, reduced_height);
			cvSetImageROI(camera_frame, gesture_area);
			cvAddS(recv_frame_reduced, zero, camera_frame, hue_mask);
			//cvAddWeighted(camera_frame, 1.0 - HAND_TRANSPARENCY, hue_mask, HAND_TRANSPARENCY, 0.0, camera_frame);
			/*
			CvScalar S = cvScalar(HAND_TRANSPARENCY, HAND_TRANSPARENCY, HAND_TRANSPARENCY, HAND_TRANSPARENCY);
			CvScalar D = cvScalar(1-HAND_TRANSPARENCY, 1-HAND_TRANSPARENCY, 1-HAND_TRANSPARENCY, 1-HAND_TRANSPARENCY);
			for(int x=0; x < gesture_area.width; x++)
			{
				for(int y=0; y < gesture_area.height ;y++)
				{
					CvScalar source = cvGet2D(camera_frame, y, x);
					CvScalar overlay = cvGet2D(recv_frame_reduced, y, x);
					CvScalar merged;
					for(int i=0;i<4;i++)
						merged.val[i] = (S.val[i]*source.val[i]+D.val[i]*overlay.val[i]);
					cvSet2D(camera_frame, y, x, merged);
				}
			}
			*/

			cvResetImageROI(camera_frame);
			total_cv += difftime(time(NULL), before_cv);
			//Display frame
			if(camera_frame != NULL)
			{
				cvShowImage("Worker", camera_frame);
				if (cvWaitKey(WAIT_TIME) == 27)
					QUIT->SetEvent();
			}
			
			//Compress the combined frame
			dest->os = &jpeg_destination_buffer;
			jpeg_start_compress(&cinfo, true);

			JSAMPROW row;
			while (cinfo.next_scanline < cinfo.image_height)
			{
				row = (JSAMPROW)((char*)camera_frame->imageData + cinfo.next_scanline*camera_frame->widthStep);
				jpeg_write_scanlines(&cinfo, &row, 1);
			}

			jpeg_finish_compress(&cinfo);
			jpeg_destination_buffer.flush();

			//Write JPEG to SendBuffer
			cout << jpeg_destination_buffer.str().length() << endl;
			if (jpeg_destination_buffer.str().length() < BUFFER_SIZE)
			{
				reps++;
				jpeg_destination_buffer.read(jpeg_buffer, jpeg_destination_buffer.str().length());
				SendBuffer->write(jpeg_buffer);
				SEND->SetEvent();
				PROCESS->ResetEvent();
			}
			else
			{
				printf("W_PROCESS: Save to SendBuffer....FAILED! Jpeg > Buffer, Frame Discarded\n");
			}	
		}
	}
	return 0;
}

UINT w_Send(LPVOID pParam)
{
	char* local_port = (char*) pParam;

	//Set up Listining Socket
	SOCKET listen_socket, send_socket;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	
	//Use Address NULL for local
	if ((rv = getaddrinfo(LOCAL_ADDR, local_port, &hints, &servinfo)) != 0) {
		printf("W_SEND: Setting up Listen Socket....FAILED! getaddrinfo, WSA Error: %d\n", WSAGetLastError());
		return 1;
	}
	
	//Loop through all the results and make a socket
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((listen_socket = WSASocket(p->ai_family, p->ai_socktype, p->ai_protocol, NULL, 0, WSA_FLAG_OVERLAPPED)) == -1) {
			printf("W_SEND: Setting up Listen Socket....FAILED! WSASocket, Error: %d\n", WSAGetLastError());
			continue;
		}
		if (bind(listen_socket, p->ai_addr, p->ai_addrlen) == -1) {
			closesocket(listen_socket);
			printf("W_SEND: Setting up Listen Socket....FAILED! bind, WSA Error: %d\n", WSAGetLastError());
			continue;
		}
		break;
	}
	
	if (p == NULL) {
		printf("W_SEND: Setting up Listen Socket....FAILED! WSA Error: %d\n", WSAGetLastError());
		return 1;
	}
	
	printf("W_SEND: Waiting for Client Connection....\n");
	
	//Listin to socket for an incoming connection and accept if one comes in.
	if (listen(listen_socket, 1) == -1) {
		printf("W_SEND: Waiting for Client Connection....FAILED! listen, WSA Error: %d\n", WSAGetLastError());
		return 1;
	}
	else
	{
		struct sockaddr_storage their_addr;
		socklen_t addr_size = sizeof their_addr;
		if((send_socket = WSAAccept(listen_socket, (struct sockaddr *)&their_addr, &addr_size, NULL, NULL)) == -1) {
			printf("W_SEND: Waiting for Client Connection....FAILED! WSAAccept, WSA Error: %d\n", WSAGetLastError());
			return 1;
		}
	}


	WSAOVERLAPPED send_overlapped;
	WSABUF wsa_send_buffer;
	DWORD send_bytes = 0, flags = 0;
	char send_buffer[BUFFER_SIZE];
	int err = 0;
	
	wsa_send_buffer.len = BUFFER_SIZE;
	wsa_send_buffer.buf = send_buffer;

	//Create Event Handle For Overlapped Structure.
	send_overlapped.hEvent = WSACreateEvent();
	if (send_overlapped.hEvent == NULL)
	{
		printf("W_SEND: Setting up Sockets and Buffers....FAILED! WSA Error: %d\n", WSAGetLastError());
		return 1;
	}
	
	while(1)
	{
		WaitForSingleObject(SEND->m_hObject, INFINITE);
		if (SendBuffer->hasImage() != 0)
		{
			//Read from the SendBuffer
			SendBuffer->read(send_buffer);
			
			//Send the jpeg
			if ((WSASend(send_socket, &wsa_send_buffer, 1, &send_bytes, 0, &send_overlapped, NULL)
				== SOCKET_ERROR) && (WSA_IO_PENDING != (err = WSAGetLastError())))
			{
				printf("W_SEND: Sending Image....FAILED! WSASend, WSA Error: %d\n", WSAGetLastError());
				if (WSAGetLastError() == 10054)
					QUIT->SetEvent();
				return 1;
			}

			if (WSAWaitForMultipleEvents(1, &send_overlapped.hEvent, TRUE, INFINITE, TRUE) == WSA_WAIT_FAILED)
			{
				printf("W_SEND: Sending Image....FAILED! WSASend, WSA Error: %d\n", WSAGetLastError());
				if (WSAGetLastError() == 10054)
					QUIT->SetEvent();
				return 1;
			}

			if (WSAGetOverlappedResult(send_socket, &send_overlapped, &send_bytes, FALSE, &flags) == FALSE)
			{
				printf("W_SEND: Sending Image....FAILED! WSASend, WSA Error: %d\n", WSAGetLastError());
				if (WSAGetLastError() == 10054)
					QUIT->SetEvent();
				return 1;
			}

			WSAResetEvent(send_overlapped.hEvent);
			
		}
		SEND->ResetEvent();
	}
	return 0;
}

UINT w_Receive(LPVOID pParam)
{
	//Set up the connect socket
	char* remote_port = (char*) pParam;

	printf("W_RECEIVE: Setting up Connect Socket....\n");
	SOCKET recv_socket;
	int rv;
	struct addrinfo hints, *servinfo, *p;
	
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET; // set to AF_INET to force IPv4
	hints.ai_socktype = SOCK_STREAM;

	//use NULL to connect to local, use IP address to connect to remote.
	if ((rv = getaddrinfo(REMOTE_ADDR, remote_port, &hints, &servinfo)) != 0) {
		printf("W_RECEIVE: Setting up Connect Socket....FAILED! getaddrinfo, WSA Error: %d\n", WSAGetLastError());
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((recv_socket = WSASocket(p->ai_family, p->ai_socktype, p->ai_protocol, NULL, 0, WSA_FLAG_OVERLAPPED)) == -1) {
			printf("W_RECEIVE: Setting up Connect Socket....FAILED! WSASocket, WSA Error: %d\n", WSAGetLastError());
			continue;
		}
		break;
	}
	
	if (p == NULL) {
		printf("W_RECEIVE: Setting up Connect Socket....FAILED! WSA Error: %d\n", WSAGetLastError());
		return 1;
	}
	
	//Connect to the server, 
	printf("W_RECEIVE: Attempting to Connect to Server....\n");
	while (WSAConnect(recv_socket, p->ai_addr, p->ai_addrlen, NULL, NULL, NULL, NULL) == -1) {	
		
	}
	
	//Set up buffer and structures used for WSARecv
	WSAOVERLAPPED recv_overlapped;
	WSABUF wsa_recv_buffer;
	DWORD bytes_recv = 0, flags = 0;
	char recv_buffer[BUFFER_SIZE];
	int err = 0;

	recv_overlapped.hEvent = WSACreateEvent();
	if (recv_overlapped.hEvent == NULL)
	{
		printf("W_RECEIVE: Setting up Receive Socket and Buffers....FAILED! WSA Error: %d\n", WSAGetLastError());
		return 1;
	}
	
	//Continuous Receive Loop
	while(1)
	{
		for (int i = 0; i < BUFFER_SIZE; i+= bytes_recv)
		{
	
			wsa_recv_buffer.len = BUFFER_SIZE-i;
			wsa_recv_buffer.buf = &recv_buffer[i];

			if (
				(WSARecv(recv_socket, &wsa_recv_buffer, 1, &bytes_recv, &flags, &recv_overlapped, NULL) == SOCKET_ERROR)
				&& (WSA_IO_PENDING != (err = WSAGetLastError()))
				)
			{
				printf("W_RECEIVE: Receiving Image....FAILED! WSARecv, WSA Error: %d\n", WSAGetLastError());
				if (WSAGetLastError() == 10054)
					QUIT->SetEvent();
				return 1;
			}
			
			if (WSAWaitForMultipleEvents(1, &recv_overlapped.hEvent, TRUE, INFINITE, TRUE) == WSA_WAIT_FAILED)
			{
				printf("W_RECEIVE: Receiving Image....FAILED! WSAWaitForMultipleEvents, WSA Error: %d\n", WSAGetLastError());
				if (WSAGetLastError() == 10054)
					QUIT->SetEvent();
				return 1;
			}
			
			if (WSAGetOverlappedResult(recv_socket, &recv_overlapped, &bytes_recv, FALSE, &flags) == FALSE)
			{
				printf("W_RECEIVE: Receiving Image....FAILED! WSAGetOverlappedResults, WSA Error: %d\n", WSAGetLastError());
				if (WSAGetLastError() == 10054)
					QUIT->SetEvent();
				return 1;
			}
			WSAResetEvent(recv_overlapped.hEvent);
			
		}

		//Write Image to Display Buffer
		ReceiveBuffer->write(recv_buffer);
		PROCESS->SetEvent();
	}
	return 0;
}

int _tmain(int argc, _TCHAR* argv[])
{
	printf("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n");
	printf("XX                                                          XX\n");
	printf("XX             HANDS_IN_AIR_V1.2  WORKER STATION            XX\n");
	printf("XX                                                          XX\n");
	printf("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n");

	//Initialize Shared Buffers
	SendBuffer = new SharedBuffer();
	ReceiveBuffer = new SharedBuffer();

	// Initialize Winsock
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
	if (iResult != 0)
	{
		printf("WSAStartup failed: %d\n", iResult);
		return 1;
	}

	//Event Signalling End Program Command
	QUIT = new CEvent(FALSE, TRUE);
	QUIT->ResetEvent();

	SEND = new CEvent(FALSE, TRUE);
	SEND->ResetEvent();

	PROCESS = new CEvent(FALSE, TRUE);
	PROCESS->ResetEvent();

	AfxBeginThread(w_Send, (LPVOID)LOCAL_PORT_1);
	AfxBeginThread(w_Send, (LPVOID)LOCAL_PORT_2);
	//AfxBeginThread(w_Send, (LPVOID)LOCAL_PORT_3);

	AfxBeginThread(w_Receive, (LPVOID)REMOTE_PORT_1);
	AfxBeginThread(w_Receive, (LPVOID)REMOTE_PORT_2);
	//AfxBeginThread(w_Receive, (LPVOID)REMOTE_PORT_3);
	
	AfxBeginThread(w_ProcessImage, NULL);

	WaitForSingleObject(QUIT->m_hObject, INFINITE);
	printf("MAIN: End Process Signaled, Program Exiting....\n");

	//Exit Cleanly
	printf("HandsInAir Exiting\n");
	time_t elapsed = difftime(time(NULL), before);
	cout << "Frames Displayed: " << reps << endl;
	cout << "Time: " << elapsed << endl;
	cout << "Frame Rate: " << reps/elapsed << endl;
	cout << "Total CV Time: " << total_cv << endl;
	cout << "CV TIME PER FRAME: " << total_cv/reps << endl;
	WSACleanup();
	cvDestroyAllWindows();
	jpeg_destroy_compress(&cinfo);
	jpeg_destroy_decompress(&dinfo);
	return 0;
}
