//
// program part extracted by Elmar Stellnberger on 2015-10-06
//                     email: estellnb@elstel.org
//
#ifndef SPARSEFILE_WRITEOUT
#define SPARSEFILE_WRITEOUT

#include <sys/types.h>

#define MAXBUFSZ 32768
extern int blocksize;

#define true 1
#define false 0
typedef int bool;

struct fd_buf {
  int flds; int bfpos; char *flbuffer;
};

extern const long long magic;

void set_fb( struct fd_buf *fb, int fb_fdout, int fb_pos, char *fb_buffer );
void resetBufPos( struct fd_buf *bf );

struct Header { long long magic, sectorsize, commentlength; char comment[0]; };
struct Trailer { long long filelength, sectorsize, segmentcount, tablecount, magic; } trailer;

void flushBuffer( struct fd_buf *fb );
void writeZeroes( struct fd_buf *fb, ssize_t length );
void writeOut( struct fd_buf *fb, const void *content, ssize_t length );

long long writeHeader( struct fd_buf *fb, bool stdheader, const char *srcfilename, const char* message, int msglength );

#endif

