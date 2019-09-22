//
// program part written by Elmar Stellnberger on 2015-10-06
//                     email: estellnb@elstel.org
//
// This program may be used under the terms of GPLv3; see: https://www.gnu.org/licenses/gpl-3.0.en.html.
// If you apply changes please sign our contributor license agreement at https://www.elstel.org/license/CLA-elstel.pdf
// so that your changes can be included into the main trunk at www.elstel.org/software/ - look here for other interesting content!
//

#define _GNU_SOURCE
#include "sparsefile-writeout.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <errno.h>

#define fd fb->flds
#define fdout fb->flds
#define buffer fb->flbuffer
#define pos fb->bfpos

int blocksize = 4096;
const long long magic = 0xbb9ac3739f92915d;

void set_fb( struct fd_buf *fb, int fb_fdout, int fb_pos, char *fb_buffer ) {
  fd = fb_fdout; pos = fb_pos; buffer = fb_buffer;
}

void resetBufPos( struct fd_buf *bf ) { bf->bfpos = 0; };

void flushBuffer( struct fd_buf *fb ) {
  register int len = pos; register char *buf = buffer; int ret;
  while( len > 0 && ( ( ret = write(fdout,buf,len) ) >= 0 || ( ret==-1 && errno == EINVAL ) ) ) if( ret >= 0 ) { buf += ret; len -= ret; };
  if( ret == -1 ) { perror ("writing output file"); exit(5); }
  assert( len == 0 );
  if( pos % blocksize ) pos = -1;  // only last block may be smaller / not a multiple of block size
  else pos = 0;
}

void writeZeroes( struct fd_buf *fb, ssize_t length ) {
  assert( pos + length <= MAXBUFSZ && pos >= 0 );
  bzero( buffer + pos, length );
  pos += length;
  if( pos >= MAXBUFSZ ) flushBuffer( fb ); 
}

void writeOut( struct fd_buf *fb, const void *content, ssize_t length ) {
  assert( pos >= 0 );
  while( pos + length > MAXBUFSZ ) {
    ssize_t firstpart = MAXBUFSZ - pos;
    memcpy( buffer + pos, content, firstpart );
    pos += firstpart; flushBuffer( fb );
    content += firstpart; length -= firstpart;
  }
  memcpy( buffer + pos, content, length );
  pos += length; if( pos >= MAXBUFSZ ) flushBuffer( fb ); 
}

// implicit parameter: blocksize, fdin
long long writeHeader( struct fd_buf *fb, bool stdheader, const char *srcfilename, const char* message, int msglength ) {
  char hostname[255]; char *username; time_t curtime; char *timeanddate; int padding;
  struct Header *header = (struct Header*)( buffer + pos ); off_t bytes_written;
  header->magic = magic; header->sectorsize = blocksize; pos+=sizeof(struct Header);
  if( stdheader ) {
    if(gethostname(hostname,255)) strcpy(hostname,"localhost"); username = getlogin();
    time(&curtime); timeanddate = asctime(localtime(&curtime));
    header->commentlength = snprintf( buffer+pos, blocksize-pos, "created on %s by %s with %s from %s", timeanddate, username, hostname, srcfilename );
    pos += header->commentlength;
  } else header->commentlength = 0;
  if( pos >= MAXBUFSZ ) flushBuffer( fb );
  if( message && msglength ) { 
    if(stdheader) writeOut( fb, ", ", 2 ); header->commentlength+=2;
    writeOut( fb, message, msglength ); header->commentlength += msglength + message[msglength-1] ? 0 : -1;
  } else {
    if(stdheader) writeOut( fb, ".\000", 2 ); header->commentlength++;
  }
  padding = pos % blocksize; if(padding) padding = blocksize - padding;
  bzero( buffer+pos, padding ); pos += padding;
  flushBuffer( fb );
  bytes_written = lseek( fdout, 0 , SEEK_CUR ); if(bytes_written==-1) { perror("getting file position"); exit(3); }
  return bytes_written;
}
