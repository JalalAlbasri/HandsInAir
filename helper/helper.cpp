#include "StdAfx.h"
#include "helper.h"

CEvent* QUIT;
CEvent* SEND;
CEvent* DISPLAY;

/* FUNCTION PROTOTYPES */
UINT h_Camera(LPVOID pParam);
UINT h_Send(LPVOID pParam);
UINT h_Server(LPVOID pPARAM);
UINT h_Client(LPVOID pParam);
UINT h_Receive(LPVOID pParam);
UINT h_Display(LPVOID pParam);

using namespace std;

static CvAdaptiveSkinDetector asd = CvAdaptiveSkinDetector(1, 1);

//Point to Point
//const char* LOCAL_ADDR = "192.168.1.138";
//const char* REMOTE_ADDR = "192.168.1.137";
const char* LOCAL_ADDR = "192.168.1.138";
const char* REMOTE_ADDR = "192.168.1.137";

const char* LOCAL_PORT_1 = "49000\0";
const char* LOCAL_PORT_2 = "49001\0";
const char* LOCAL_PORT_3 = "49002\0";
const char* REMOTE_PORT_1 = "50000\0";
const char* REMOTE_PORT_2 = "50001\0";
const char* REMOTE_PORT_3 = "50002\0";

struct jpeg_compress_struct cinfo;
struct jpeg_decompress_struct dinfo;
struct jpeg_error_mgr cjerr;
struct jpeg_error_mgr djerr;

my_destination_mgr* dest;
my_source_mgr* src;

int r;
int reps = 0; time_t before;
SharedBuffer *SendBuffer, *ReceiveBuffer;

//Camera Frame Grabber Thread.
UINT h_Camera(LPVOID pParam)
{  
	char jpeg_buffer[BUFFER_SIZE];
	IplImage* camera_frame = cvCreateImage(capture_size, IPL_DEPTH_8U, 3);
	IplImage* camera_frame640 = cvCreateImage(cvSize(640, 320), IPL_DEPTH_8U, 3);

	//Check that Camera is Operational
	CvCapture* capture;
	capture = cvCreateCameraCapture(0);
	if(!cvQueryFrame(capture))
	{
		printf("H_CAMERA: Checking Camera Operational....FAILED!\n");
		return 1;
	}
	else 
		cout << "H_CAMERA: Checking Camera Operational.....OK!\n" << endl;
	
	//Set up JPEG Compression Structures
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
	before = time(NULL);
	while(1)
	{
		stringstream jpeg_destination_buffer;

		//Grab Camera Frame as IPLImage
		camera_frame640 = cvQueryFrame(capture);
		cvResize(camera_frame640, camera_frame);

		//Write IPLImage to SendBuffer as JPEG
		dest->os = &jpeg_destination_buffer;
		jpeg_start_compress(&cinfo, true);

		JSAMPROW row;
		while (cinfo.next_scanline < cinfo.image_height) {
			row = (JSAMPROW)((char*)camera_frame->imageData + cinfo.next_scanline*camera_frame->widthStep);
			jpeg_write_scanlines(&cinfo, &row, 1);
		}
		jpeg_finish_compress(&cinfo);
		jpeg_destination_buffer.flush();

		//Write JPEG to SendBuffer
		if (jpeg_destination_buffer.str().length() < BUFFER_SIZE)
		{
			jpeg_destination_buffer.read(jpeg_buffer, jpeg_destination_buffer.str().length());
			SendBuffer->write(jpeg_buffer);
			SEND->SetEvent();
			reps++;
		}
		else
		{
			printf("H_CAMERA: Saving to SendBuffer....FAILED! Jpeg > Buffer, Frame Discarded\n");
		}
	}
	return 0;
}

UINT h_Send(LPVOID pParam)
{
	char* local_port = (char*) pParam;

	//Set up Listen Socket
	SOCKET listen_socket, send_socket;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	
	//Use Address NULL for local
	if ((rv = getaddrinfo(LOCAL_ADDR, local_port, &hints, &servinfo)) != 0) {
		printf("H_SEND: Setting up Listen Socket....FAILED! getaddrinfo, WSA Error: %d\n", WSAGetLastError());
		return 1;
	}
	
	//Loop through all the results and make a socket
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((listen_socket = WSASocket(p->ai_family, p->ai_socktype, p->ai_protocol, NULL, 0, WSA_FLAG_OVERLAPPED)) == -1) {
			printf("H_SEND: Setting up Listen Socket....FAILED! WSASocket, Error: %d\n", WSAGetLastError());
			continue;
		}
		if (bind(listen_socket, p->ai_addr, p->ai_addrlen) == -1) {
			closesocket(listen_socket);
			printf("H_SEND: Setting up Listen Socket....FAILED! bind, WSA Error: %d\n", WSAGetLastError());
			continue;
		}
		break;
	}
	
	if (p == NULL) {
		printf("H_SEND: Setting up Listen Socket....FAILED! WSA Error: %d\n", WSAGetLastError());
		return 1;
	}
	
	printf("H_SEND: Waiting for Client Connection....\n");
	
	//Listin to socket for an incoming connection and accept if one comes in.
	if (listen(listen_socket, 1) == -1) {
		printf("H_SEND: Waiting for Client Connection....FAILED! listen, WSA Error: %d\n", WSAGetLastError());
		return 1;
	}
	else
	{
		struct sockaddr_storage their_addr;
		socklen_t addr_size = sizeof their_addr;
		if((send_socket = WSAAccept(listen_socket, (struct sockaddr *)&their_addr, &addr_size, NULL, NULL)) == -1) {
			printf("H_SEND: Waiting for Client Connection....FAILED! WSAAccept, WSA Error: %d\n", WSAGetLastError());
			return 1;
		}
	}
	
	//Set up Sockets and Buffers
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
		printf("H_SEND: Setting up Sockets and Buffers....FAILED! WSA Error: %d\n", WSAGetLastError());
		return 1;
	}
	
	while(1)
	{
		WaitForSingleObject(SEND->m_hObject, INFINITE);
		if (SendBuffer->hasImage() != 0)
		{
			//Read from the SendBuffer
			SendBuffer->read(send_buffer);

			//Send the Jpeg
			if ((WSASend(send_socket, &wsa_send_buffer, 1, &send_bytes, 0, &send_overlapped, NULL)
				== SOCKET_ERROR) && (WSA_IO_PENDING != (err = WSAGetLastError())))
			{
				printf("H_SEND: Sending Image....FAILED! WSASend, WSA Error: %d\n", WSAGetLastError());
				if (WSAGetLastError() == 10054)
					QUIT->SetEvent();
				return 1;
			}

			if (WSAWaitForMultipleEvents(1, &send_overlapped.hEvent, TRUE, INFINITE, TRUE) == WSA_WAIT_FAILED)
			{
				printf("H_SEND: Sending Image....FAILED! WSASend, WSA Error: %d\n", WSAGetLastError());
				if (WSAGetLastError() == 10054)
					QUIT->SetEvent();
				return 1;
			}

			if (WSAGetOverlappedResult(send_socket, &send_overlapped, &send_bytes, FALSE, &flags) == FALSE)
			{
				printf("H_SEND: Sending Image....FAILED! WSASend, WSA Error: %d\n", WSAGetLastError());
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

UINT h_Receive(LPVOID pParam)
{
	//Set up Connect Socket
	char* remote_port = (char*) pParam;
	SOCKET recv_socket;
	int rv;
	struct addrinfo hints, *servinfo, *p;
	
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET; // set to AF_INET to force IPv4
	hints.ai_socktype = SOCK_STREAM;

	//use NULL to connect to local, use IP address to connect to remote.
	if ((rv = getaddrinfo(REMOTE_ADDR, remote_port, &hints, &servinfo)) != 0) {
		printf("H_RECEIVE: Setting up Connect Socket....FAILED! getaddrinfo, WSA Error: %d\n", WSAGetLastError());
		return 1;
	}

	//Loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((recv_socket = WSASocket(p->ai_family, p->ai_socktype, p->ai_protocol, NULL, 0, WSA_FLAG_OVERLAPPED)) == -1) {
			printf("H_RECEIVE: Setting up Connect Socket....FAILED! WSASocket, WSA Error: %d\n", WSAGetLastError());
			continue;
		}
		break;
	}
	
	if (p == NULL) {
		printf("H_RECEIVE: Setting up Connect Socket....FAILED! WSA Error: %d\n", WSAGetLastError());
		return 1;
	}
	
	//Connect to the server. 
	printf("H_RECEIVE: Attempting to Connect to Server....\n");
	while (WSAConnect(recv_socket, p->ai_addr, p->ai_addrlen, NULL, NULL, NULL, NULL) == -1) {	
		
	}
	
	//Set up Receive Socket and Buffers
	WSAOVERLAPPED recv_overlapped;
	WSABUF wsa_recv_buffer;
	DWORD bytes_recv = 0, flags = 0;
	char recv_buffer[BUFFER_SIZE];
	int err = 0;

	recv_overlapped.hEvent = WSACreateEvent();
	if (recv_overlapped.hEvent == NULL)
	{
		printf("H_RECEIVE: Setting up Receive Socket and Buffers....FAILED! WSA Error: %d\n", WSAGetLastError());
		return 1;
	}
	
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
				printf("H_RECEIVE: Receiving Image....FAILED! WSARecv, WSA Error: %d\n", WSAGetLastError());
				if (WSAGetLastError() == 10054)
					QUIT->SetEvent();
				return 1;
			}

			if (WSAWaitForMultipleEvents(1, &recv_overlapped.hEvent, TRUE, INFINITE, TRUE) == WSA_WAIT_FAILED)
			{
				printf("H_RECEIVE: Receiving Image....FAILED! WSAWaitForMultipleEvents, WSA Error: %d\n", WSAGetLastError());
				if (WSAGetLastError() == 10054)
					QUIT->SetEvent();
				return 1;
			}

			if (WSAGetOverlappedResult(recv_socket, &recv_overlapped, &bytes_recv, FALSE, &flags) == FALSE)
			{
				printf("H_RECEIVE: Receiving Image....FAILED! WSAGetOverlappedResults, WSA Error: %d\n", WSAGetLastError());
				if (WSAGetLastError() == 10054)
					QUIT->SetEvent();
				return 1;
			}
			
			WSAResetEvent(recv_overlapped.hEvent);
		}

		//Write Image to Display Buffer
		ReceiveBuffer->write(recv_buffer);
		DISPLAY->SetEvent();

	}
	return 0;
}

UINT h_Display(LPVOID pParam)
{	
	//Create the Display Window
	cvNamedWindow("Helper", CV_WINDOW_NORMAL);

	//Initialize Images and Buffers
	char display_buffer[BUFFER_SIZE];
	IplImage *display_frame, *loading_frame;
	display_frame = cvCreateImage(capture_size, IPL_DEPTH_8U, 3);
	loading_frame = cvLoadImage("loading.jpg");
	
	//Set Up JPEG Decompression Structures
	dinfo.err = jpeg_std_error(&djerr); 
    jpeg_create_decompress(&dinfo);

	dinfo.src = (struct jpeg_source_mgr *)(*dinfo.mem->alloc_small)
    ((j_common_ptr) &dinfo, JPOOL_PERMANENT, sizeof(my_source_mgr));
		
	src = (my_source_mgr*) dinfo.src;
	src->buffer = (JOCTET *)(*dinfo.mem->alloc_small)
    ((j_common_ptr) &dinfo, JPOOL_PERMANENT, JPEG_BUF_SIZE*sizeof(JOCTET));
	
	while(1)
	{
		WaitForSingleObject(DISPLAY->m_hObject, INFINITE);
		//Verify image has been received.
		if (ReceiveBuffer->hasImage() != 0)
		{
			cvSet(display_frame, cvScalar(0,0,0));

			//Read from Shared Buffer
			ReceiveBuffer->read(display_buffer);

			//Decompress image to IplImage format
			stringstream jpeg_source_buffer;
			//We Only want the jpeg size not the entire buffer!
			jpeg_source_buffer.write(display_buffer, BUFFER_SIZE);
		
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
				printf("H_DISPLAY: Decompressing Image....FAILED! Failed to read JPEG header\n");
			} 
			else if (cinfo.num_components != 3 && cinfo.num_components != 1)
			{
				printf("H_DISPLAY: Decompressing Image....FAILED! Unsupported number of color components: %d\n", cinfo.num_components);
			}
			else
			{
				jpeg_start_decompress(&dinfo);

				JSAMPARRAY imageBuffer = (*dinfo.mem->alloc_sarray)((j_common_ptr)&dinfo, JPOOL_IMAGE, 
																	dinfo.output_width*dinfo.output_components, 1);
				for (int y = 0; y < dinfo.output_height; y++) {
					jpeg_read_scanlines(&dinfo, imageBuffer, 1);
					uint8_t* dstRow = (uint8_t*)display_frame->imageData + display_frame->widthStep*y;
					memcpy(dstRow, imageBuffer[0], dinfo.output_width*dinfo.output_components);
				}
				jpeg_finish_decompress(&dinfo);
			}

			jpeg_source_buffer.flush();
		}

		//Display the received image.
		if (display_frame == NULL)
		{
			//Display loading frame
			cvShowImage("Helper", loading_frame);
		}
		else {
			cvShowImage("Helper", display_frame);
		}
		DISPLAY->ResetEvent();
		if (cvWaitKey(WAIT_TIME) == 27)
			QUIT->SetEvent();
	}
	return 0;
}
int _tmain(int argc, _TCHAR* argv[])
{
	printf("\n");
	printf("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n");
	printf("XX                                                          XX\n");
	printf("XX             HANDS_IN_AIR_V1.2  HELPER STATION            XX\n");
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

	QUIT = new CEvent(FALSE, TRUE);
	QUIT->ResetEvent();
	SEND = new CEvent(FALSE, TRUE);
	SEND->ResetEvent();
	DISPLAY = new CEvent(FALSE, TRUE);
	DISPLAY->ResetEvent();


	//printf("MAIN: Starting Camera thread\n");
	AfxBeginThread(h_Camera, NULL);
	
	AfxBeginThread(h_Send, (LPVOID)LOCAL_PORT_1);
	//AfxBeginThread(h_Send, (LPVOID)LOCAL_PORT_2);
	//AfxBeginThread(h_Send, (LPVOID)LOCAL_PORT_3);

	AfxBeginThread(h_Receive, (LPVOID)REMOTE_PORT_1);
	//AfxBeginThread(h_Receive, (LPVOID)REMOTE_PORT_2);
	//AfxBeginThread(h_Receive, (LPVOID)REMOTE_PORT_3);

	AfxBeginThread(h_Display, NULL);

	printf("MAIN: Waiting For Exit Signal....\n");
	WaitForSingleObject(QUIT->m_hObject, INFINITE);
	time_t elapsed = difftime(time(NULL), before);
	cout << "Frames Displayed: " << reps << endl;
	cout << "Time: " << elapsed << endl;
	cout << "Frame Rate: " << reps/elapsed << endl;

	WSACleanup();
	cvDestroyAllWindows();
	jpeg_destroy_compress(&cinfo);
	jpeg_destroy_decompress(&dinfo);
	return 0;
}
