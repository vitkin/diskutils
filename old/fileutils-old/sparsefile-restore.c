//
// program written by Elmar Stellnberger on 2015-10-3
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

#define MAXBUFSZ 65536
#define DEFBLKSZ 4096

enum { SparsefileRestore, BinPatch } invocatedas;

void help() {
  if( invocatedas == SparsefileRestore )
    printf("sparsefile-restore [--rescue] ii=compact.simg of=sparse.file\n"
	   "                   --size/--table ii=compact.simg\n"
	   "  --size ............... print size of the target file\n"
	   "  --check .............. do not print anything, just check the allocation table for validity (bash: check $?)\n"
	   "  [--rescue] --print-table ... print allocation table\n"
	   "  --rescue ............. try to restore a corrupted allocation table\n"
	   "  --dense .............. if seeking does not succeed then make the output file dense (may be useful for pipe/socket transmission)\n"
	   "  --tiny ............... restrict size of output file to 4GB\n"
	   "  --overwrite .......... overwrite target file\n\n");
  else 
    printf("binpatch --replace ii=compact.simg of=sparse.file\n"
	   "                   --size/--table ii=compact.simg\n"
	   "  --replace .... modify sparse.file in place instead of keeping the old file around\n"
	   "  --size/-s ............... print final size of the target file after modification would be applied\n"
	   "  --check/-c .............. do not print anything, just check the allocation table for validity (bash: check $?)\n"
	   "  [--rescue/-r] --print-table ... print allocation table\n"
	   "  --rescue/-r ............. try to restore a corrupted allocation table\n"
	   "  --tiny/-t ............... restrict size of output file to 4GB\n\n" );
	   // no overwrite, no dense option
};

#define true 1
#define false 0
typedef int bool;

int fdin = -1, fdout = -1;
char buffer[MAXBUFSZ]; int blocksize = DEFBLKSZ;
char *self;

long long segmentcount, filesize;
struct Segment { long long srcfileoffset, imgfileoffset, bytecount; struct Segment *prev; };
long long srcfileoffset, imgfileoffset, bytecount;
struct Segment *current = NULL, *next, *last;

long long segments; int tablecount; const long long magic = 0xbb9ac3739f92915d;
struct Header { long long magic, sectorsize, commentlength; char comment[0]; } *header; off_t headersize;
struct Trailer { long long filelength, sectorsize, segmentcount, tablecount, magic; } trailer;

#define MAXTABLES 7
char *table; struct Trailer *trl[MAXTABLES]; struct Segment *seg[MAXTABLES];

bool poweroftwo( long long n ) {
  register long long m = n; register int i; if(!m) return false;
  i=0; m>>=1; while( m ) { m>>=1; i++; }
  return n == ( 1<<i );
}

inline void readIn( int fd, char *buf, ssize_t len ) { register int ret;
  while( len > 0 && ( ( ret = read(fd,buf,len) ) > 0 || ( ret==-1 && errno == EINVAL ) ) ) if( ret >= 0 ) { buf += ret; len -= ret; };
  if( ret == -1 ) { 
    perror("reading input file");
    if( fdin >= 0 ) { if(close(fdin)) perror("closing input file"); }
    if( fdout >= 0 ) { if(close(fdout)) perror("closing output file"); }
    exit(4);
  } else if( ret == 0 && len > 0 ) {
    fprintf(stderr,"premature end of input file.\n\n");
    exit(4);
  }
}

inline void writeOut( int fd, char *buf, ssize_t len ) { register int ret;
  while( len > 0 && ( ( ret = write(fdout,buf,len) ) >= 0 || ( ret==-1 && errno == EINVAL ) ) ) if( ret >= 0 ) { buf += ret; len -= ret; };
  if( ret == -1 ) { 
    perror("writing output file");
    if( fdin >= 0 ) { if(close(fdin)) perror("closing input file"); }
    if( fdout >= 0 ) { if(close(fdout)) perror("closing output file"); }
    exit(5);
  }
  assert(!len);
}

#define min(a,b) ((a)<=(b)?(a):(b))
#define HighestValueBit(type) ((type)1<<(sizeof(type)*8-2))
#define MaxSigned(type) (HighestValueBit(type)-1+HighestValueBit(type))

union { int i; char c[sizeof(int)]; } endianess;

bool dataok = true;
#define check(cond) if(check||rescue) { dataok &= cond; if(check&&!dataok) return 1; } else assert(cond);

int main( int argc, char *argv[] ) {
  bool cmdlineerr = false, lastoptgone = false, size = false, check = false, rescue = false, printtable = false;
  off_t tinyfilepos; int mempos; bool tiny = false, overwrite = false, dense = false; struct Segment *segm;
  char *inp_imagename = NULL, *outp_filename = NULL; int flags = 0, mode = 0664, err=0; int t; long long s; bool firstrun;
  endianess.i = 1; if( endianess.c[0] != 1 ) { fprintf(stderr,"program needs to be compiled on a little endian machine; exiting."); exit(6); }
  self = basename(*argv); argv++; argc--;
  if(!strcmp(self,"sparsefile-restore")) invocatedas = SparsefileRestore;
  else if(!strcmp(self,"binpatch")) invocatedas = BinPatch;
  else { fprintf( stderr, "this program must either be called binpatch or sparsefile-restore; exiting." ); return 1; }
  while( argc > 0 ) {
    register char *arg = *argv;
    //printf("%i: %s\n", argc, arg ); fflush(stdout);
    if( arg[0]=='-' && !lastoptgone) {
      if( !arg[1] ) {  fprintf(stderr,"stale '-' minus in command line.\n"); cmdlineerr = true; }
      else if( arg[1] != '-' ) {
	while(*++arg) switch(*arg) {
	  case 's': size = true; break; 
	  case 'c': check = true; break; 
	  case 'r': rescue = true; break; 
	  case 'p': printtable = true; break; 
	  case 't': tiny = true; break; 
	  case 'o': overwrite = true; break; 
	  case 'h': help(); return 0;
	  default: fprintf(stderr,"unknown option -%c.\n",*arg); cmdlineerr = true;
	}
      } else if(!strcasecmp(arg+2,"size")) size = true;
      else if(!strcasecmp(arg+2,"check")) check = true;
      else if(!strcasecmp(arg+2,"rescue")) rescue = true;
      else if(!strcasecmp(arg+2,"tiny")) tiny = true;
      else if(!strcasecmp(arg+2,"print-table")) printtable = true;
      else if(!strcasecmp(arg+2,"overwrite")) overwrite = true;
      else if(!strcasecmp(arg+2,"dense")) dense = true;
      else if(!strcasecmp(arg+2,"help")) { help(); return 0; }
      else if(!arg[2]) lastoptgone = true;
      else { fprintf(stderr,"unknown option %s.\n",arg); cmdlineerr = true; } 
    } 
      else if(!memcmp(arg,"ii=",3)) inp_imagename = arg+3;
      else if(!memcmp(arg,"of=",3)) outp_filename = arg+3;
      else {
	fprintf( stderr, "unknown command line argument '%s'.\n", arg ); cmdlineerr = true;
      }
    argv++; argc--;
  }
  if( cmdlineerr ) { fputc('\n',stderr); return 2; }
  if(!inp_imagename) { fprintf(stderr,"please specify input imagename with ii=imagename.\n\n"); return 2; }
  if(outp_filename) {
    if(size||check)  {
      fprintf(stderr,"the --size and --check options are thought to be used without an output filename.\n\n"); return 2;
  }} else {
    if(!(size||check||printtable))  {
      fprintf(stderr,"please specify output filename with of=filename.\n\n"); return 2;
  }};
  //printf("ii=%s, of=%s\n", inp_imagename, outp_filename );
  
  fdin = open( inp_imagename, O_RDONLY | O_LARGEFILE );
  if( fdin == -1 ) {
    fdin = open( inp_imagename, O_RDONLY );
  }
  if(fdin==-1) { fprintf(stderr,"error opening file '%s': %s.\n\n",inp_imagename,strerror(errno)); return 3; }

  readIn( fdin, buffer, sizeof(struct Header) );
  header = (struct Header*) &buffer[0]; check(header->magic==magic);
  headersize = sizeof(struct Header) + header->commentlength; check(headersize<=65536);
  if(rescue&&!dataok) {
    header = NULL; fprintf(stderr,"no/corrupt header found; assuming ther would be no header at all.\n");
    headersize = 0; dataok = true;
  } else {
    tinyfilepos = lseek( fdin, 0, SEEK_SET ); if( tinyfilepos == -1 ) { 
      if(errno==ESPIPE) fprintf(stderr,"unseekable image file: please copy the image file to your hard disk first."); 
      else perror("seek on input file");
      return 4; 
    }
    header = (struct Header*) malloc(headersize);
    readIn( fdin, (char*)header, headersize );
    blocksize = header->sectorsize;
    headersize = ( ( headersize - 1 ) / blocksize + 1 ) * blocksize;
  }

  tinyfilepos = lseek( fdin, -blocksize, SEEK_END );
  if( tinyfilepos == -1 && errno != EOVERFLOW ) {
    if(errno==ESPIPE) fprintf(stderr,"unseekable image file: please copy the image file to your hard disk first."); 
    else perror("seek on input file");
    return 4;
  }
  // besser: einfach direkt einlesen
  readIn( fdin, buffer, blocksize ); assert(sizeof(struct Trailer)<=blocksize);

  //long long segments = 0, tablecount = 3; const long long magic = 0xbb9ac3739f92915d;
  //struct Header { long long magic, sectorsize, imgfilelength, destfilelength, commentlength; char comment[MAXBUFSZ]; };
  //struct Trailer { long long filelength, sectorsize, segmentcount, tablecount, magic; } trailer, addtrailer[2];

  mempos = blocksize; tinyfilepos = -blocksize; firstrun = true;

  do { bool magicok;
    memcpy( &trailer, buffer + mempos - sizeof(trailer), sizeof(trailer) );

    check( trailer.magic == magic );
    blocksize = trailer.sectorsize; check( poweroftwo(trailer.sectorsize) && blocksize == trailer.sectorsize );
    segments = trailer.segmentcount;
    filesize = trailer.filelength; check( blocksize && segmentcount <= (( (filesize-1) / blocksize + 1 - 1) >> 1 ) + 1 );
    tablecount = trailer.tablecount >= MAXTABLES ? MAXTABLES : trailer.tablecount; check( 0 <= tablecount && trailer.tablecount < 256 );
    if( tablecount <= 0 ) tablecount = 1;
    if( dataok || !rescue ) break;
    if( firstrun ) { fprintf(stderr,"trailer seems to be destroyed; starting backward search.\n"); firstrun = false; }
    while(true) { off_t pret;
      do { mempos -= sizeof(long long); } while( mempos >= sizeof(trailer) && !( magicok = *(long*)(buffer+mempos) == magic )  );
      if( mempos >= sizeof(trailer) && magicok ) break;
      tinyfilepos -= DEFBLKSZ;
      pret = lseek( fdin, tinyfilepos, SEEK_END ); if( pret == -1 ) { if(errno!=EINVAL) { perror("lseek"); return 3; } else break; }
      readIn( fdin, buffer, DEFBLKSZ + sizeof(trailer) ); 
      mempos = DEFBLKSZ<<1;
    };
    if( magicok ) { dataok = true; mempos += sizeof(long long); }
  } while( tinyfilepos > 0 || mempos > sizeof(trailer) );
  
  if(!dataok) { fprintf(stderr,"no intact trailer found; exiting.\n\n"); 
		if(close(fdin)) perror("closing input file"); err=3; return 6; }

  if(size) printf("%lli\n",filesize);

  long long onetable = sizeof(struct Segment) * segments + sizeof(struct Trailer);
  long long tablesize = onetable * tablecount;
  table = (char*) malloc( tablesize );

  tinyfilepos = lseek( fdin, -tablesize + mempos + tinyfilepos, SEEK_END ); if( tinyfilepos == -1 && errno != EOVERFLOW ) { perror("seek on input file"); return 4; }
  readIn( fdin, table, tablesize );

  for( t=0; t < tablecount; t++ ) {
    trl[t] = (struct Trailer*)( table + tablesize - t * onetable - sizeof(struct Trailer) );
    seg[t] = (struct Segment*)( table + tablesize - (t+1) * onetable );
  }; segm = seg[0];


  // todo: check segments of different tables for equality
  //       or take the table with non-corrupted segments (ascending order of srcoffsets, destoff` = destoff + bytecount )

  if(printtable) {
     printf("filelesize: %lli / 0x%llx\nblocksize: %i\ntablecount: %i\nsegments: %lli\n", filesize, filesize, blocksize, tablecount, segments );
     if(header) printf("headersize: %li\ncomment: %s\n", headersize, header->comment );
     for( s = segments; --s >= 0; ) 
       printf("segment #%lli: src=0x%llx dest=0x%llx bytes=0x%llx\n", segments-s, segm[s].srcfileoffset, segm[s].imgfileoffset, segm[s].bytecount );
  }

  headersize = 0; // better trust on running imgfileoffsets 

  if( outp_filename ) {
    long long writepos = 0, desiredpos, readpos = headersize, goodreadpos, chunksize; off_t pret, seekoffs; ssize_t block;

    flags = O_WRONLY | ( -!tiny & O_LARGEFILE ) | O_CREAT | ( -!overwrite & O_EXCL ) | ( -overwrite & O_TRUNC );
    fdout = open( outp_filename, flags, mode );
    if(fdout==-1) { fprintf(stderr,"error opening file '%s': %s.\n\n",outp_filename,strerror(errno)); return 3; }

    pret = lseek( fdin, headersize, SEEK_SET ); if( pret == -1 ) { perror("seek on input file"); return 4; }
    assert(!dense);

    for( s = segments; --s >= 0; )  {

       desiredpos = segm[s].srcfileoffset;
       while( writepos < desiredpos ) {
	 seekoffs = min( desiredpos - writepos, MaxSigned(off_t) );
	 pret = lseek( fdout, seekoffs, SEEK_CUR ); if( pret == -1 && errno != EOVERFLOW ) { perror("seek on output file"); return 5; }
	 writepos += seekoffs;
       }
       assert( desiredpos == writepos );

       goodreadpos = segm[s].imgfileoffset;
       while( readpos < goodreadpos ) {
	 seekoffs = min( goodreadpos - readpos, MaxSigned(off_t) );
	 pret = lseek( fdin, seekoffs, SEEK_CUR ); if( pret == -1 && errno != EOVERFLOW ) { perror("seek on input file"); return 4; }
	 readpos += seekoffs;
       }
       assert( readpos== goodreadpos );

       chunksize = segm[s].bytecount;
       while( chunksize > 0 ) {
	 block = min( chunksize, MAXBUFSZ );
         readIn( fdin, buffer, block );
         writeOut( fdout, buffer, block );
	 chunksize -= block; writepos += block; readpos+=block;
      }

    }

    if( writepos < filesize ) {
      tinyfilepos = filesize;
      if( tinyfilepos == filesize ) ftruncate( fdout, tinyfilepos );
      else {
	desiredpos = filesize - 512;
	while( writepos < desiredpos ) {
	  seekoffs = min( desiredpos - writepos, MaxSigned(off_t) );
	  pret = lseek( fdout, seekoffs, SEEK_CUR ); if( pret == -1 && errno != EOVERFLOW ) { perror("seek on output file"); return 5; }
	  writepos += seekoffs;
	}
	bzero(buffer,512);
	writeOut( fdout, buffer, 512 );
    }}

  }

  if(close(fdin)) { perror("closing input file"); err=3; }
  if(outp_filename) 
    if(close(fdout)) { perror("closing output file"); err=3; }

  free(table); free(header);
  if(err) fputc('\n',stderr);
  return err;
}

