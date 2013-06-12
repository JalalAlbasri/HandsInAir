#include "stdafx.h"
#include "jpeg.h"

using namespace std;

/***********************
 
 JPEG HELPER FUNCTIONS
 
 
 *************************/


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