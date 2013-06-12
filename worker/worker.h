//Shared Header File

#pragma once

#include <afxwin.h>
#include <ws2tcpip.h>
#include <winsock2.h>

#pragma comment(lib, "Ws2_32.lib")

//OpenCV
#include <cv.h>
#include <cxcore.h>
#include <highgui.h>
#include <math.h>
#include <cvaux.h>

//Common
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <conio.h>
#include <stdint.h>
#include <iostream>
#include <sstream>
#include <vector>
#include <time.h>

#include <jpeglib.h>

const int BUFFER_SIZE = 30000;
const int JPEG_QUALITY = 70;
const int WAIT_TIME = 5;
const CvSize capture_size = cvSize(320, 240);
const double HAND_REDUCTION = 0.8;
const double HAND_TRANSPARENCY = 0.8;

#include "SharedBuffer.h"
#include "jpeg.h"