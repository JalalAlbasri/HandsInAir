#include "StdAfx.h"
#include "SharedBuffer.h"

CCriticalSection m_CriticalSection;

SharedBuffer::SharedBuffer(void)
{	
//	printf("SHARED_BUFFER: Creating new SharedBuffer....\n");
	jpeg_size = 0;
	has_image = 0;

//	printf("SHARED_BUFFER: Creating new SharedBuffer....OK!\n");
}

SharedBuffer::~SharedBuffer(void)
{
}

int SharedBuffer::read(char* out)
{
	CSingleLock singleLock(&m_CriticalSection);
	singleLock.Lock();
	for (int i = 0; i < BUFFER_SIZE; i++)
		out[i] = buffer[i];
	singleLock.Unlock();
	return 0;
}

int SharedBuffer::write(char* in)
{
	CSingleLock singleLock(&m_CriticalSection);
	singleLock.Lock();
	for (int i=0; i < BUFFER_SIZE; i++)
		buffer[i] = in[i];
	singleLock.Unlock();
	has_image = 1;
	return 0;
}

int SharedBuffer::hasImage(void)
{
	return has_image;
}