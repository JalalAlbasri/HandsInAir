#pragma once

#include "worker.h"

const static size_t JPEG_BUF_SIZE = 16384;

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

void my_init_destination (j_compress_ptr cinfo);

boolean my_empty_output_buffer(j_compress_ptr cinfo);

void my_term_destination (j_compress_ptr cinfo);