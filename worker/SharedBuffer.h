#pragma once

#include "worker.h"

#include <afxmt.h>
#include <iostream>
#include <sstream>
#include <vector>

using namespace std;

class SharedBuffer
{
public:
	SharedBuffer(void);
	~SharedBuffer(void);
	int read(char* out);
	int write(char* in);
	int hasImage(void);

private:
	char buffer[BUFFER_SIZE];
	int jpeg_size;
	int has_image;
};

