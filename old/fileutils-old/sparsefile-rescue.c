//
// program written by Elmar Stellnberger on 2015-10-03
//                    email: estellnb@elstel.org
//
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include "sparsefile-writeout.h"

void help() {
  printf("sparsefile-rescue [--sync] if=sparse.file bs=4096 oi=compacted.simg\n"
	 "  bs defaults to 4096; it should be set to the file systems block size for performance and efficiency reasons;\n"
	 "    some solid state disk may use 8192; lower values like 2048, 1024 and 512 are also possible.\n"
	 "  --overwrite/-o: overwrite target image even if it does already exist.\n"
	 "  --tiny/-t: restrict image to a tiny size smaller than 4GB (some target file systems may require this option).\n"
	 "  --noatime/-n: do not change the access time of the input file.\n"
	 "  --sync/-s: synchronizes to disk on finishing; improves error reporting and durability but may last longer. \n\n");
};

int fdin, fdout;
char buffer[MAXBUFSZ]; struct fd_buf fbout;
char *self;

long long segmentcount;
struct Segment { long long srcfileoffset, imgfileoffset, bytecount; struct Segment *prev; };
long long srcfileoffset, imgfileoffset, bytecount;
struct Segment *current = NULL, *next, *last;

long long segments = 0, tablecount = 3;


union { int i; char c[sizeof(int)]; } endianess;

int main( int argc, char *argv[] ) {
  bool sync = false, cmdlineerr = false, lastoptgone = false, tiny = false, overwrite = false, noatime = false;
  char *inp_filename = NULL, *outp_imagename = NULL; int flags = 0, mode = 0664, err=0; int t; long long s;
  endianess.i = 1; if( endianess.c[0] != 1 ) { fprintf(stderr,"program needs to be compiled on a little endian machine; exiting."); exit(6); }
  self = *argv; argv++; argc--;
  while( argc > 0 ) {
    register char *arg = *argv;
    //printf("%i: %s\n", argc, arg ); fflush(stdout);
    if( arg[0]=='-' && !lastoptgone) {
      if( !arg[1] ) {  fprintf(stderr,"stale '-' minus in command line.\n"); cmdlineerr = true; }
      else if( arg[1] != '-' ) {
	while(*++arg) switch(*arg) {
	  case 's': sync = true; break; 
	  case 't': tiny = true; break; 
	  case 'o': overwrite = true; break; 
	  case 'n': noatime = true; break; 
	  case 'h': help(); return 0;
	  default: fprintf(stderr,"unknown option -%c.\n",*arg); cmdlineerr = true;
	}
      } else if(!strcasecmp(arg+2,"sync")) sync = true;
      else if(!strcasecmp(arg+2,"tiny")) tiny = true;
      else if(!strcasecmp(arg+2,"overwrite")) overwrite = true;
      else if(!strcasecmp(arg+2,"noatime")) noatime = true;
      else if(!strcasecmp(arg+2,"help")) { help(); return 0; }
      else if(!arg[2]) lastoptgone = true;
      else { fprintf(stderr,"unknown option %s.\n",arg); cmdlineerr = true; } 
    } 
      else if(!memcmp(arg,"if=",3)) inp_filename = arg+3;
      else if(!memcmp(arg,"oi=",3)) outp_imagename = arg+3;
      else if(!memcmp(arg,"bs=",3)) {
	char *endptr; long value;
	value = strtol(arg+3,&endptr,0);
	if(*endptr) { fprintf( stderr, "error parsing block size (int/hex/oct number) at '%s'.\n", endptr ); cmdlineerr = true; }
	else if(!*(arg+3)) { fprintf( stderr, "no number given directly after bs=%s.\n", endptr ); cmdlineerr = true; }
	else if( value <= 0 || value > MAXBUFSZ ) {
	  fprintf( stderr, "block size value %li bigger than internal buffer or below or equal to zero.\n", value ); cmdlineerr = true;
	} else 
          blocksize = value;
      } else {
	fprintf( stderr, "unknown command line argument '%s'.\n", arg ); cmdlineerr = true;
      }
    argv++; argc--;
  }
  if( cmdlineerr ) { fputc('\n',stderr); return 2; }
  if(!inp_filename) { fprintf(stderr,"please specify input filename with if=filename.\n\n"); return 2; }
  if(!outp_imagename) { fprintf(stderr,"please specify output imagename with oi=imagename.\n\n"); return 2; }
  //printf("if=%s, oi=%s, bs=%i\n", inp_filename, outp_imagename, blocksize );
  
  flags = O_RDONLY | ( -noatime & O_NOATIME ) | O_LARGEFILE;
  fdin = open( inp_filename, flags, mode );
  if(fdin==-1) {
    flags = O_RDONLY | ( -noatime & O_NOATIME );
    fdin = open( inp_filename, flags, mode );
  }
  if(fdin==-1) { fprintf(stderr,"error opening file '%s': %s.\n\n",inp_filename,strerror(errno)); return 3; }

  flags = O_WRONLY | ( -!tiny & O_LARGEFILE ) | O_CREAT | ( -!overwrite & O_EXCL ) | ( -overwrite & O_TRUNC );
  fdout = open( outp_imagename, flags, mode );
  if(fdout==-1) { fprintf(stderr,"error opening file '%s': %s.\n\n",outp_imagename,strerror(errno)); return 3; }
  set_fb( &fbout, fdout, 0, buffer );

  srcfileoffset = 0; bytecount = 0; 
  resetBufPos( &fbout );
  imgfileoffset = writeHeader( &fbout, true, inp_filename, NULL, 0 ); 

  while(true) {
    int len, length; char *buf; long *longbuf; 
    int i, ret; bool allzero;

    len = blocksize; buf = buffer; ret = 1;
    while( len > 0 && ( ( ret = read(fdin,buf,len) ) > 0 || ( ret==-1 && errno == EINVAL ) ) ) if( ret >= 0 ) { buf += ret; len -= ret; };
    if( ret == -1 ) { perror ("reading input file"); err=4; break; }
    if( ret == 0 ) break; // done, EOF reached.
    length = blocksize - len;

    longbuf = (long*) buffer; len = length / sizeof(long);
    for( allzero=true, i=0; i<len; i++ ) if(longbuf[i]) { allzero = false; break; }

    if(!allzero) {
      buf = buffer; len = length; ret = 1;
      while( len > 0 && ( ( ret = write(fdout,buf,len) ) >= 0 || ( ret==-1 && errno == EINVAL ) ) ) if( ret >= 0 ) { buf += ret; len -= ret; };
      if( ret == -1 ) { perror ("writing output file");  err=5; break; }
      bytecount += length;

    } else if (bytecount) {
      // allzero && bytecount:
      next = (struct Segment*) malloc(sizeof(struct Segment));
      next->prev = current;
      current = next;
      current->imgfileoffset = imgfileoffset;
      current->srcfileoffset = srcfileoffset;
      current->bytecount = bytecount;
      imgfileoffset += bytecount;
      srcfileoffset += bytecount + length;
      bytecount = 0; segments++;

    } else {
      // allzero, continuing
      srcfileoffset += length;

    };
    
    if( length < blocksize ) break;
  }

  if (bytecount && err!=5) {
    // allzero && bytecount:
    next = (struct Segment*) malloc(sizeof(struct Segment));
    next->prev = current;
    current = next;
    current->imgfileoffset = imgfileoffset;
    current->srcfileoffset = srcfileoffset;
    current->bytecount = bytecount;
    segments++;
  };
  last = current;

  //long long segments = 0, tablecount = 3; const long long magic = 0xbb9ac3739f92915d;
  //struct Trailer { long long filelength, sectorsize, segmentcount, tablecount, magic; } trailer;
  if( err != 5 ) {
    long long tablesize = ( sizeof(struct Segment) * segments + sizeof(struct Trailer) ) * tablecount;
    int padding = tablesize % blocksize; 
    if(padding) padding = blocksize - padding;
    writeZeroes( &fbout, padding );

    trailer.filelength = srcfileoffset;
    trailer.sectorsize = blocksize;
    trailer.segmentcount = segments;
    trailer.tablecount = tablecount;
    trailer.magic = magic;

    for( t=0; t < tablecount; t++ ) {

      current = last;
      for( s=0; s < segments; s++ ) {
	writeOut( &fbout, current, sizeof(struct Segment) );
	current = current->prev;
      }; assert(!current);

      trailer.tablecount = t + 1;
      writeOut( &fbout, &trailer, sizeof(struct Trailer) );
    }

    flushBuffer( &fbout );
  }

  if(sync) if(fdatasync(fdout)) { perror("fdatasync-ing output file"); err=5; }
  if(close(fdin)) { perror("closing input file"); err=4; }
  if(sync) if(fdatasync(fdout)) { perror("fdatasync-ing output file"); err=5; }
  if(close(fdout)) { perror("closing output file"); err=5; }

  current = last;
  for( s=0; s < segments; s++ ) {
    next = current->prev;
    free(current);
    current = next;
  }; assert(!current);

  if(err) fputc('\n',stderr);
  return err;
}

