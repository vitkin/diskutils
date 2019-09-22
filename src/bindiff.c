//
// program written by Elmar Stellnberger on 2015-10-06
//                    email: estellnb@elstel.org
//
// This program may be used under the terms of GPLv3; see: https://www.gnu.org/licenses/gpl-3.0.en.html.
// If you apply changes please sign our contributor license agreement at https://www.elstel.org/license/CLA-elstel.pdf
// so that your changes can be included into the main trunk at www.elstel.org/software/ - look here for other interesting content!
//

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include "sparsefile-writeout.h"

#define MAXBUFSZ 32768
#define DEFBLKSZ 2048

void help() {
  printf("bindiff [--print/prnfile=outpfile] bs=2048 [df12=difffile] [if1=]binfile1 [if2=]binfile2\n"
         "  compare »binfile1« with »binfile2«, write binary diff format to »df12« and print differing blocks to »prnfile«;\n"
	 "  »binfile1« can later on be converted with »df12« into »binfile2« by 'binpatch'.\n"
	 "  bs=2048 (recommended for CDs) / 4096 (usually right on HDDs): currently also used for »df12«.\n"
	 "  --print/-p: currently the only option is to print all block numbers that differ\n"
	 "  --summary/-s: just return the blockcount of all blocks that differ\n"
	 "  --blocks/-b: print differing block numbers rather than byte ranges\n"
	 "  --tiny/-t: restrict size of output file to 4GB\n"
	 "  --overwrite/-o: overwrite output files\n"
	 "  --hex/-h: write offset as hexadecimal numbers\n"
	 "  --noatime/-n: open if1, if2 without updating the access timestamp\n\n");
};

#define true 1
#define false 0
typedef int bool;

int fdin1 = -1, fdin2 = -1, fd12 = -1; FILE *prn = NULL;
char bufferA[MAXBUFSZ], bufferB[MAXBUFSZ];
struct fd_buf fb12;
char *self;

#define min(a,b) ((a)<=(b)?(a):(b))

ssize_t readIn( int fd, char *buf, ssize_t len, char *filename ) { register int ret; ssize_t bytes_read = 0;
  while( len > 0 && ( ( ret = read(fd,buf,len) ) > 0 || ( ret==-1 && errno == EINVAL ) ) ) if( ret >= 0 ) { buf += ret; len -= ret; bytes_read += ret; };
  if( ret == -1 ) { fprintf(stderr,"error reading from input file '%s': %s.\n\n",filename,strerror(errno)); exit(4); }
  return bytes_read;
}

long long readCopyAll( int fdin, struct fd_buf *fbout, char *filename ) {
  long long difference = 0; off_t curpos, lastpos; ssize_t numread;
  bool donebySeeking = fbout == NULL;
  if( donebySeeking ) {
    curpos = lseek( fdin, 0, SEEK_CUR ); 
    if( curpos >=0 ) lastpos = lseek( fdin, 0, SEEK_END );
    if( curpos >=0 && lastpos >= 0 ) {
      difference += lastpos - curpos;
    } else donebySeeking = false;
  }
  if( !donebySeeking ){
    do { 
      difference += numread = readIn(fdin, fbout ? fbout->flbuffer : bufferA, blocksize, filename ); 
      if( numread == -1 ) { perror ( fbout ? "copying input file into difffile": "read-seeking input file" ); exit(5); }
      if( fbout && numread > 0 ) {
        fbout->bfpos = numread;
        flushBuffer( fbout ); assert( fbout->bfpos == 0 );
      }
    } while ( numread > 0 );
  }
  return difference;
}

char *inp1_filename = NULL, *inp2_filename = NULL, *print_filename = NULL, *diff12_filename = NULL; int err=0;
bool print = false, summary = false, hex = false, blocks = false;

void smry_printf( const char* format, ... ) {
  va_list arg;
  if( summary && prn != stdout && print_filename ) {
    va_start(arg,format);
     vfprintf( stdout, format, arg ); 
    va_end(arg);
  }
  va_start(arg,format);
   vfprintf( prn, format, arg );
  va_end(arg);
}

long long segmentcount, tablecount = 3;
struct Segment { long long srcfileoffset, imgfileoffset, bytecount; struct Segment *prev; };
struct Segment *current = NULL, *next, *last;

void docomp( const char *inp_filenames ) {
  ssize_t numread1, numread2, tocomp; int cmp;
  long long diffcount = 0, samecount = 0;
  long long lastpos, filepos = 0, filepos2, start = -1; 
  bool last_was_different = false;
  long long imgfilepos = 0; segmentcount = 0; current = NULL;

  if( fd12 >= 0 ) {
    imgfilepos = writeHeader( &fb12, true, inp_filenames, NULL, 0 ); 
  }

  while(true) {
    numread1 = readIn(fdin1,bufferA,blocksize,inp1_filename);
    numread2 = readIn(fdin2,bufferB,blocksize,inp2_filename);
    tocomp = min( numread1, numread2 );
    if( numread1 != numread2 ) break;
    if( tocomp <= 0 ) break;
    cmp = memcmp( bufferA, bufferB, tocomp );
    if( !last_was_different && cmp ) start = filepos;
    else if( last_was_different && !cmp && ( print ) ) {
      if( blocks ) {
	assert( !( start % blocksize ) && !( lastpos % blocksize ) );
	start /= blocksize; lastpos /= blocksize;
	if( start == lastpos) fprintf( prn, "block " );
	                 else fprintf( prn, "blocks " );
      }
      if( start == lastpos)
	if(hex) fprintf( prn, "0x%llx differs\n", start );
           else	fprintf( prn, "%lli differs\n", start );
      else
	if(hex) fprintf( prn, "0x%llx - 0x%llx differ\n", start, lastpos );
	   else	fprintf( prn, "%lli - %lli differ\n", start, lastpos );

      if( fd12 >= 0 ) {
	next = (struct Segment*)malloc(sizeof(struct Segment));
	next->srcfileoffset = start;
	next->imgfileoffset = imgfilepos;
	next->bytecount =  filepos - start;
	next->prev = current; current = next;
	segmentcount++; imgfilepos += next->bytecount;
      }
    }
    if( cmp && fd12 >= 0 ) {
      fb12.bfpos = tocomp;
      flushBuffer( &fb12 ); assert( fb12.bfpos == 0 );
    }
    if( cmp ) diffcount += 1; else samecount += 1;
    lastpos = filepos; last_was_different = cmp;
    filepos += tocomp; 
  }

  if( fd12 >= 0 && numread2 > 0 ) {
    fb12.bfpos = numread2;
    flushBuffer( &fb12 ); assert( fb12.bfpos == 0 );
  }
  if( numread1 != numread2 ) {
    if( blocks ) {
      assert( !( start % blocksize ) && !( filepos % blocksize ) );
      start /= blocksize; lastpos /= blocksize;
      if( start == lastpos) fprintf( prn, "block " );
		       else fprintf( prn, "blocks " );
    }
    if( !last_was_different ) {
       if( numread1 > 0 && numread2 > 0 ) {
         if(hex) fprintf( prn, "0x%llx differs\n", filepos );
            else fprintf( prn, "%lli differs\n", filepos );
       }} else {
       if(hex) fprintf( prn, "0x%llx - 0x%llx differ\n", start, filepos );
	  else fprintf( prn, "%lli - %lli differ\n", start, filepos );
    }
    if( fd12 >= 0 ) {
      next = (struct Segment*)malloc(sizeof(struct Segment));
      next->srcfileoffset = last_was_different ? start : filepos ;
      next->imgfileoffset = imgfilepos;
      next->bytecount =  tocomp + last_was_different ? filepos - start : 0;
      next->prev = current; current = next;
      segmentcount++; imgfilepos += next->bytecount;
    }
    filepos += tocomp;
  }
  filepos2 = filepos;
  if( numread1 - tocomp > 0 ) {
    long long difference = numread1 - tocomp;
    if( difference >= blocksize ) difference += readCopyAll( fdin1, NULL, inp1_filename );
    smry_printf( "first input file '%s' is %li bytes longer.\n", inp1_filename, difference );
  } else if( numread2 - tocomp > 0 ) {
    long long difference = numread2 - tocomp;
    if( difference >= blocksize ) difference += readCopyAll( fdin2, &fb12, inp2_filename );
    smry_printf( "second input file '%s' is %li bytes longer.\n", inp2_filename, difference );
    if( fd12 >= 0 ) {
      next = (struct Segment*)malloc(sizeof(struct Segment));
      next->srcfileoffset = filepos ;
      next->imgfileoffset = imgfilepos;
      next->bytecount =  difference;
      next->prev = current; current = next;
      segmentcount++; imgfilepos += next->bytecount;
    }; filepos2 += difference;
  } else 
      if(diffcount==0) smry_printf( "files are the same.\n");
  smry_printf( "%lli %s in common differ, %lli are the same.\n", diffcount, blocksize!=1 ? "blocks" : "bytes", samecount );

  last = current;

  //struct Trailer { long long filelength, sectorsize, segmentcount, tablecount, magic; } trailer;
  if( fd12 >= 0 ) {
    long long tablesize = ( sizeof(struct Segment) * segmentcount + sizeof(struct Trailer) ) * tablecount;
    int padding = tablesize % blocksize; int t, s;
    if(padding) padding = blocksize - padding;
    if( fb12.bfpos <= padding ) 
      writeZeroes( &fb12, padding - fb12.bfpos );
    else {
      writeZeroes( &fb12, blocksize - fb12.bfpos );
      flushBuffer( &fb12 );
      writeZeroes( &fb12, padding );
    }

    trailer.filelength = filepos2;
    trailer.sectorsize = blocksize;
    trailer.segmentcount = segmentcount;
    trailer.tablecount = tablecount;
    trailer.magic = magic;

    for( t=0; t < tablecount; t++ ) {

      current = last;
      for( s=0; s < segmentcount; s++ ) {
	writeOut( &fb12, current, sizeof(struct Segment) );
	current = current->prev;
      }; assert(!current);

      trailer.tablecount = t + 1;
      writeOut( &fb12, &trailer, sizeof(struct Trailer) );
    }

    flushBuffer( &fb12 );
  }
}

union { int i; char c[sizeof(int)]; } endianess;

int main( int argc, char *argv[] ) {
  bool noatime= false, overwrite = false, tiny = false; bool cmdlineerr = false, lastoptgone = false;
  int flags, mode = 0664; blocksize = DEFBLKSZ;
  endianess.i = 1; if( endianess.c[0] != 1 ) { fprintf(stderr,"program needs to be compiled on a little endian machine; exiting."); exit(6); }
  self = *argv; argv++; argc--;
  while( argc > 0 ) {
    register char *arg = *argv;
    //printf("%i: %s\n", argc, arg ); fflush(stdout);
    if( arg[0]=='-' && !lastoptgone) {
      if( !arg[1] ) {  fprintf(stderr,"stale '-' minus in command line.\n"); cmdlineerr = true; }
      else if( arg[1] != '-' ) {
	while(*++arg) switch(*arg) {
	  case 'p': print = true; break; 
	  case 's': summary = true; break; 
	  case 'n': noatime = true; break; 
	  case 'b': blocks = true; break; 
	  case 'o': overwrite = true; break; 
	  case 't': tiny = true; break; 
	  case 'h': hex = true; break; 
	  default: fprintf(stderr,"unknown option -%c.\n",*arg); cmdlineerr = true;
	}
      } else if(!strcasecmp(arg+2,"print")) print = true;
      else if(!strcasecmp(arg+2,"summary")) summary = true;
      else if(!strcasecmp(arg+2,"noatime")) noatime = true;
      else if(!strcasecmp(arg+2,"hex")) hex = true;
      else if(!strcasecmp(arg+2,"blocks")) blocks = true;
      else if(!strcasecmp(arg+2,"overwrite")) overwrite = true;
      else if(!strcasecmp(arg+2,"tiny")) tiny = true;
      else if(!strcasecmp(arg+2,"help")) { help(); return 0; }
      else if(!arg[2]) lastoptgone = true;
      else { fprintf(stderr,"unknown option %s.\n",arg); cmdlineerr = true; } 
    } 
    else if(!memcmp(arg,"if1=",3)) inp1_filename = arg+4;
    else if(!memcmp(arg,"if2=",3)) inp2_filename = arg+4;
    else if(!memcmp(arg,"bs=",3)) {
      char *endptr; long value;
      value = strtol(arg+3,&endptr,0);
      if(*endptr) { fprintf( stderr, "error parsing block size (int/hex/oct number) at '%s'.\n", endptr ); cmdlineerr = true; }
      else if(!*(arg+3)) { fprintf( stderr, "no number given directly after bs=%s.\n", endptr ); cmdlineerr = true; }
      else if( value <= 0 || value > MAXBUFSZ ) {
	fprintf( stderr, "block size value %li bigger than internal buffer or below or equal to zero.\n", value ); cmdlineerr = true;
      } else 
	blocksize = value;
    } else if(!memcmp(arg,"prnfile=",8)) print_filename = arg+8;
    else if(!memcmp(arg,"df12=",5)) diff12_filename = arg+5;
    else if(strchr(arg,'=')) { fprintf(stderr,"unknown argument assignment: %s.\n",arg); cmdlineerr=true; }
    else {
      if(!inp1_filename) inp1_filename = arg;
      else if (!inp2_filename ) inp2_filename = arg;
      else { fprintf( stderr, "at most two files can be compared: %s.\n", arg ); cmdlineerr = true; }
    }
    argv++; argc--;
  }
  if( cmdlineerr ) { fputc('\n',stderr); return 2; }
  if(!inp1_filename||!inp2_filename) { fprintf(stderr,"please give exactly two input filenames.\n\n"); return 2; }

  fdin1 = open( inp1_filename, O_RDONLY | O_LARGEFILE | ( -noatime & O_NOATIME ) );
  if( fdin1 == -1 ) {
    fdin1 = open( inp1_filename, O_RDONLY | ( -noatime & O_NOATIME ) );
  }
  if(fdin1==-1) { fprintf(stderr,"error opening first input file '%s': %s.\n\n",inp1_filename,strerror(errno)); return 3; }

  fdin2 = open( inp2_filename, O_RDONLY | O_LARGEFILE | ( -noatime & O_NOATIME ) );
  if( fdin2 == -1 ) {
    fdin2 = open( inp2_filename, O_RDONLY | ( -noatime & O_NOATIME ) );
  }
  if(fdin2==-1) { fprintf(stderr,"error opening second input file '%s': %s.\n\n",inp2_filename,strerror(errno)); return 3; }

  if(print_filename) { int fdprn;
    fdprn = open( print_filename, O_WRONLY | O_CREAT | O_TRUNC | ( -!overwrite & O_EXCL ) | O_LARGEFILE, mode );
    if(fdprn==-1) fdprn = open( print_filename, O_WRONLY | O_CREAT | O_TRUNC | ( -!overwrite & O_EXCL ) ); if(fdprn==-1) { perror("opening print file"); return 5; }
    prn = fdopen( fdprn, "w" ); if(!prn) { perror("stream allocation for print file"); return 4; }
  } else prn = stdout;

  if( print_filename ) print = true;

  if(diff12_filename) {
    flags = O_WRONLY | ( -!tiny & O_LARGEFILE ) | O_CREAT | ( -!overwrite & O_EXCL ) | ( -overwrite & O_TRUNC );
    fd12 = open( diff12_filename, flags, mode );
    if(fd12==-1) { fprintf(stderr,"error opening file '%s': %s.\n\n",diff12_filename,strerror(errno)); return 5; }
    set_fb( &fb12, fd12, 0, bufferB );
  }

  { int inp1_len = strlen(inp1_filename);
    int inp2_len = strlen(inp2_filename);
    int all_inp_len = inp1_len + inp2_len + 7;
    char *inp_filenames = (char*) malloc(all_inp_len);
    snprintf( inp_filenames, all_inp_len, "%s and %s", inp1_filename, inp2_filename );
    docomp( inp_filenames );
  }

  if(close(fdin1)) { fprintf(stderr,"error closing input file '%s': %s.\n\n",inp1_filename,strerror(errno)); err=4; }
  if(close(fdin2)) { fprintf(stderr,"error closing input file '%s': %s.\n\n",inp2_filename,strerror(errno)); err=4; }

  fputc('\n',prn); if( summary && prn!=stdout && print_filename ) fputc('\n',stdout);
  if(prn) if(fclose(prn)) {  perror("closing print stream"); err=5; }
  if(fd12>=0) if(close(fd12)) {  perror("closing diff 1->2 file"); err=5; }

  return err;
}
