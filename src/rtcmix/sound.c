/*   sound i/o routines 
*/
/* this version uses pointers to differentiate between different flavours
   of i/o, FLOAT and INT */
/* version that just writes/reads to standard UNIXFILES, with sfheader.*/
/* TODO: keep one file from being opened twice for writing.
	 implement mag tape reads and wipeouts 
	?BUG after using layout, should not call flushbuf
	?in zap and alter routines, (when else..?

	 to add tape read/write need additional arg in open to indicate
	 file number, and this show that it is tape.
	 also need label checking.  first record on each tape file will be
	 header.  
	 only other change for tape is to use read instead of lseek, and 
	 only allow getin and wipeout routines.  also have to add librarian
	 to keep track of what file we are on, and watch out for close, and 
	 thus screw up in eof marks.  (need to know whether we have been 
	 reading or writing.

	 6/6 tape seems to work, need to add positioning, and file finds

	12/12/91 -- tape hooks are here, but are unimplemented, probably not
				worth it.  PL
	10/95 -- moved common play_on() routine here.  play_off() still in
	         soundio.c or soundio.m due to platform-specific stuff -DS
*/

#define SOUND
#include "../H/ugens.h"
#include "../H/sfheader.h"
#include "../H/byte_routines.h"
#include "../H/dbug.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/times.h>       /* obsolete on VAX replace with new proc */
#include <sys/time.h>       /* obsolete on VAX replace with new proc */
#include <string.h>

#ifdef USE_SNDLIB
#include "../H/sndlibsupport.h"
#endif

/* Used to determine if we should swap endian-ness */
extern int swap;       /* defined in check_byte_order.c */
int swap_bytes[NFILES];
short is_Next[NFILES];
extern short isNext;

/* size of buffer to allocate.  this is no longer user-configurable */
/* JGG: might want to make this 64*1024 in Linux */
int nbytes = 32768;          /* exported only for the sake of sfcopy.c */

extern float SR;
int play_is_on=0;
extern int print_is_on;
int  sfd[NFILES];            /* soundfile descriptors */
int  pointer[NFILES];	     /* to be used as pointer within sound buffer */
int  bufsize[NFILES];        /* word length of buffer */

char *sndbuf[NFILES];        /* address of buffer */
char *peak[NFILES];          /* array to store peak amplitude for nchannels */
char *peakloc[NFILES];       /* overall peak amplitude */
			   
char wipe_is_off[NFILES];    /* this is for wipeout */
char peakoff[NFILES];        /* this will set peak test on or off*/
float punch[NFILES];	     /* punch alteration flags */
char istape[NFILES];         /* flag to see if it is a tape unit */
double starttime[NFILES];    /* to save starting time of note */
long  originalsize[NFILES];  /* to save byte length of file */
long  filepointer[NFILES];   /* to save current pointer in file */
int status[NFILES];	     /* save read/write flage as well */
int isopen[NFILES];	     /* open status */
int headersize[NFILES];      /* to accomodate bsd and next headers */

SFHEADER      sfdesc[NFILES];
SFMAXAMP      sfm[NFILES];
struct stat   sfst[NFILES];

static SFCODE ampcode = {
	SF_MAXAMP,
	sizeof(SFMAXAMP) + sizeof(SFCODE)
};

struct tms    clockin[NFILES];
 
int _iaddout(),_faddout(),_iwipeout(),_fwipeout(),_ilayout(),_flayout();
int _igetin(),_fgetin();
int (*addoutpointer[NFILES])();
int (*layoutpointer[NFILES])();
int (*wipeoutpointer[NFILES])();
int (*getinpointer[NFILES])();

/*****   for macros */
float FTEMP1,FTEMP2,FTEMP3,FTEMP4;
int ITEMP1,ITEMP2;
float *PTEMP1;


float SR;   /* now declared here rather than in the profile */
char *sfname[NFILES];
float peakflag;

double m_open(float *p, short n_args, double *pp) 
{
	char  *name,*cp,*getsfcode();
	int   fno,i,new;
	float *opk;

	i = (int) pp[0];
	name = (char *) i;
	fno = p[1];
// JGG: will name ptr be valid for entire program run? Is its memory held by
// parser? If not, we should malloc sfname[fno] below (with other mallocs)
	sfname[fno] = name;
	status[fno] = (n_args == 3) ? (int)p[2] : 2;

	if((fno >=  NFILES) || (fno < 0)) {
		fprintf(stderr," Only %d files allowed\n", NFILES);
		closesf();
		}
	new = 0;
	if(isopen[fno]) {
		close(sfd[fno]);
	}
	else new = 1;

	istape[fno] = (n_args == 4) ? 1 : 0;
			/* in the case of a tape, there will be a 
			   4th argument listing the file number */

	rwopensf(name,sfd[fno],sfdesc[fno],sfst[fno],"CMIX",i,status[fno]);
	if (i < 0)
		closesf();

#ifdef USE_SNDLIB
	if (status[fno] == O_RDWR
			&& !WRITEABLE_HEADER_TYPE(sfheadertype(&sfdesc[fno]))) {
		fprintf(stderr, "m_open: can't write this type of header.\n");
		closesf();
	}
#endif

	isopen[fno] = 1;

	swap_bytes[fno] = swap;  /* swap and isNext set in rwopensf */
	is_Next[fno] = isNext;
	headersize[fno] = getheadersize(&sfdesc[fno]);

	if(print_is_on) {
		printf("name: %s   sr: %.3f  nchans: %d  class: %d\n",name,
			sfsrate(&sfdesc[fno]),sfchans(&sfdesc[fno]),
			sfclass(&sfdesc[fno]),sfd[fno]);
#ifdef USE_SNDLIB
		printf("Soundfile type: %s\n",
				sound_type_name(sfheadertype(&sfdesc[fno])));
		printf("   data format: %s\n",
				sound_format_name(sfdataformat(&sfdesc[fno])));
#endif
		printf("Duration of file is %f seconds.\n",
			(float)(sfst[fno].st_size - headersize[fno])/(float)sfclass(&sfdesc[fno])/(float)sfchans(&sfdesc[fno])/sfsrate(&sfdesc[fno]));
	}

	originalsize[fno] = istape[fno] ? 999999999 : sfst[fno].st_size;
	/*
	sfstats(sfd[fno]);
	*/
	if(new) {
		if((sndbuf[fno] = (char *)malloc((unsigned)nbytes)) == NULL) {
			fprintf(stderr," CMIX: malloc sound buffer error\n");
			closesf();
		}
		if((peakloc[fno] = (char *)malloc((unsigned)(sfchans(&sfdesc[fno]) * 
			LONG))) == NULL) {
			fprintf(stderr,"CMIX: malloc ovpeak buffer error\n");
			closesf();
		}
		if((peak[fno] = 
			(char *)malloc((unsigned)(sfchans(&sfdesc[fno])* FLOAT))) 
			== NULL) {
			fprintf(stderr,"CMIX: malloc peak buffer error!\n");
			closesf();
		}
		peakoff[fno] = 0; /* default to peakcheckon when opening file*/
		punch[fno] = 0; /* default to no punch when opening file*/
	}
	if(sfclass(&sfdesc[fno]) == SHORT) {
		addoutpointer[fno] = _iaddout;
		layoutpointer[fno] = _ilayout;
		wipeoutpointer[fno] = _iwipeout;
		getinpointer[fno] = _igetin;
	}
	else 			        {   
		addoutpointer[fno] = _faddout;
		layoutpointer[fno] = _flayout;
		wipeoutpointer[fno] = _fwipeout;
		getinpointer[fno] = _fgetin;
	}

	if(!SR) SR = sfsrate(&sfdesc[fno]);	

	if(sfsrate(&sfdesc[fno])!= SR)
		fprintf(stderr,"Note--> SR reset to %f\n",SR);

	/* read in former peak amplitudes, make sure zero'ed out to start.*/

#ifdef USE_SNDLIB

	/* In the sndlib version, we store peak stats differently. See
	   comments in sndlibsupport.c for an explanation. The sndlib
	   version of rwopensf reads peak stats, so here we just have to
	   copy these into the sfm[fno] array. (No swapping necessary.)
	*/
	memcpy(&sfm[fno], &(sfmaxampstruct(&sfdesc[fno])), sizeof(SFMAXAMP));

#else /* !USE_SNDLIB */

	/* But need to pass swap flag to getsfcode ... and the header is swapped by now */
	/* Could modify getsfcode to take fno as an arg too ... */
	cp = getsfcode(&sfdesc[fno],SF_MAXAMP);
	if(cp == NULL) {
		fprintf(stderr, "Unable to read peak amp from header: code = NULL\n");
		closesf();
	}

	bcopy(cp + sizeof(SFCODE), (char *) &sfm[fno], sizeof(SFMAXAMP));
	
	/* Need to swap here as well ... ugh */
	if (swap_bytes[fno]) {
	  for (i=0;i<SF_MAXCHAN;i++) {
	    byte_reverse4(&(sfm[fno]).value[i]);
	    byte_reverse4(&(sfm[fno]).samploc[i]);
	  }
	  byte_reverse4(&(sfm[fno]).timetag);
	}

#endif /* !USE_SNDLIB */

	for(opk = (float *)peak[fno], i = 0; i<sfchans(&sfdesc[fno]); i++) 
		*(opk+i) = sfmaxamp(&sfm[fno],i);
	bufsize[fno] = nbytes / sfclass(&sfdesc[fno]);/* set size in words */
}

setnote(start,dur,fno)
float start,dur;
int fno;
{
	int nsamps,offset;
	int i;

	if(!isopen[fno]) {
		fprintf(stderr,"You haven't opened file %d yet!\n",fno);
		closesf();
	}
	if(start > 0.) /* if start < 0 it indicates number of samples to skip*/
	        offset = (int) (start * SR + .5) * sfchans(&sfdesc[fno])
	    		* sfclass(&sfdesc[fno]);

	else    offset = -start * sfchans(&sfdesc[fno]) * sfclass(&sfdesc[fno]);

		/* make sure it falls on channel/block boundary */
	offset -= offset % (sfchans(&sfdesc[fno]) * sfclass(&sfdesc[fno]));
	offset = (offset < 0) ? 0 : offset;

	nsamps = (dur > 0.) ? (int)((start+dur) * SR -
	(offset/(sfchans(&sfdesc[fno])*sfclass(&sfdesc[fno])))+ .5) : (int)-dur;

	if(!istape[fno]) {
		if((filepointer[fno] = 
		   lseek(sfd[fno],offset+headersize[fno],0)) == -1) {
			fprintf(stderr,"CMIX: bad lseek in setnote\n");
			closesf();
		}
	}
	pointer[fno] = 0;

	_readit(fno);   /* read in first buffer */

	for(i=0; i<(sfchans(&sfdesc[fno]) * FLOAT); i++)
		*(peak[fno] + i) = 0;

	wipe_is_off[fno] = 1;          /* for wipeout */

	starttime[fno] = (start<0) ? -start/SR : start;

	times(&clockin[fno]);       /* read in starting time */

	return(nsamps);
}

_iaddout(out,fno)
float *out;
{
  int i;
  int ipoint = pointer[fno];
  int incr = sfchans(&sfdesc[fno]);
  short *ibuf;
  short t_isamp;
  
  for(i=0,ibuf = (short *)sndbuf[fno] + ipoint; i<incr; i++) {
    *(ibuf + i) +=  *(out+i); 
  }
  
  if((pointer[fno] += i) >= bufsize[fno] ) {
    _backup(fno);
    if(!peakoff[fno])
      _chkpeak(fno);
    _writeit(fno);
    _readit(fno);
    pointer[fno] = 0;
  }
}

_faddout(out,fno)
register float *out;
{
	register int i;
	register int ipoint = pointer[fno];
	register int incr = sfchans(&sfdesc[fno]);
	register float *fbuf;
	float t_fsamp;

	for(i=0,fbuf = (float *)sndbuf[fno] + ipoint; i<incr; i++) {
	  *(fbuf + i) +=  *(out+i);
	}
	if((pointer[fno] += i) >= bufsize[fno] ) {
	  _backup(fno);
	  if(!peakoff[fno]) _chkpeak(fno);
	  _writeit(fno);
	  _readit(fno);
	  pointer[fno] = 0;
	}
}

_igetin(in,fno)
float *in;
{
	int i;
	int ipoint = pointer[fno];
	int incr = sfchans(&sfdesc[fno]);
	short *ibuf;
	short t_isamp;

	for(i=0,ibuf = (short *)sndbuf[fno] + ipoint; i<incr; i++) {
	  *(in+i) = *(ibuf + i);
	}
	if((pointer[fno] += i) >= bufsize[fno] ) {
		pointer[fno] = 0;
		return(_readit(fno));
	}
	return(nbytes);
}

_fgetin(in,fno)
register float *in;
{
	register i;
	register int ipoint = pointer[fno];
	register int incr = sfchans(&sfdesc[fno]);
	register float *fbuf;

	for(i=0,fbuf = (float *)sndbuf[fno] + ipoint; i<incr; i++) {
	  *(in+i) =  *(fbuf + i);
	}
	if((pointer[fno] += i) >= bufsize[fno] ) {
		pointer[fno] = 0;
		return(_readit(fno));
	}
	return(nbytes);
}

_ilayout(out,chlist,fno)
register float *out;
int *chlist;

{
	register i;
	register int ipoint = pointer[fno];
	register int incr = sfchans(&sfdesc[fno]);
	register short *ibuf;
	short t_isamp;

	for(i=0,ibuf = (short *)sndbuf[fno] + ipoint; i<incr; i++) {
	  if(chlist[i]) {
	      *(ibuf + i) = *(out+i); 
	  }
	}
	if((pointer[fno] += i) >= bufsize[fno] ) {
		_backup(fno);
		if(!peakoff[fno]) _chkpeak(fno);
		_writeit(fno);
		_readit(fno);
		pointer[fno] = 0;
	}
}

_flayout(out,chlist,fno)
register float *out;
int *chlist;

{
	register i;
	register int ipoint = pointer[fno];
	register int incr = sfchans(&sfdesc[fno]);
	register float *fbuf;

	for(i=0,fbuf = (float *)sndbuf[fno] + ipoint; i<incr; i++) {
	  if(chlist[i]) {
	    *(fbuf + i) =  *(out+i);
	  }
	}
	if((pointer[fno] += i) >= bufsize[fno] ) {
		_backup(fno);
		if(!peakoff[fno]) _chkpeak(fno);
		_writeit(fno);
		_readit(fno);
		pointer[fno] = 0;
	}
}

_iwipeout(out,fno)
register float *out;
			/* to force destructive writes */ 
{
	register i;
	register int ipoint = pointer[fno];
	register int incr = sfchans(&sfdesc[fno]);
	register short *ibuf;
	short t_isamp;

	for(i=0,ibuf = (short *)sndbuf[fno] + ipoint; i<incr; i++) {
	  *(ibuf + i) = *(out+i); 
	}
	if((pointer[fno] += i) >= bufsize[fno] ) {
		if(wipe_is_off[fno]) {   /*setnot positions after first read*/
			_backup(fno);
			wipe_is_off[fno] = 0;
			}
		if(!peakoff[fno])
			_chkpeak(fno);
		_writeit(fno);
		pointer[fno] = 0;
	}
}

_fwipeout(out,fno)
register float *out;
			/* to force destructive writes */ 
{
	register i;
	register int ipoint = pointer[fno];
	register int incr = sfchans(&sfdesc[fno]);
	register float *fbuf;

	for(i=0,fbuf = (float *)sndbuf[fno] + ipoint; i<incr; i++) {
	  *(fbuf + i) =  *(out+i);
	}
	if((pointer[fno] += i) >= bufsize[fno] ) {
		if(wipe_is_off[fno]) {   /*setnot positions after first read*/
			_backup(fno);
			wipe_is_off[fno] = 0;
			}
		if(!peakoff[fno])
			_chkpeak(fno);
		_writeit(fno);
		pointer[fno] = 0;
	}
}

bgetin(input,fno,size)
register float *input;   
{
	register int i;
	register short *ibuf;
	register float *fbuf;
	register int todo,remains;
	int n;
	int len = bufsize[fno]; 

refill:	todo = ((pointer[fno] + size) > len) 
				? len - pointer[fno] : size;

        /* If it's a short */
	if(sfclass(&sfdesc[fno]) == SHORT) {
	  for(i=0,ibuf = (short *)sndbuf[fno] + pointer[fno];i<todo;i++) {
	    *(input++) = (float) *(ibuf++);
	  }
	}
	
	/* If it's a float */
	else {
	  for(i=0,fbuf = (float *)sndbuf[fno] + pointer[fno];i<todo;i++) {
	    *(input++) =  *(fbuf++);
	  }
	}
	
	pointer[fno] += todo;
	
	if(pointer[fno] == len) {
		n = _readit(fno);
		pointer[fno] = 0;
		if(!n) return(n);
	}
	if(size -= todo) goto refill;
	return(i);
}

blayout(out,chlist,fno,size)
register float *out;   
register int *chlist;
{
	register int i,j;
	register short *ibuf;
	register float *fbuf;
	register int todo,remains;
	register int nchans;
	int len = bufsize[fno]; 
	short t_isamp;

	nchans = sfchans(&sfdesc[fno]);

refill:	todo = ((pointer[fno] + size) > len) 
				? len - pointer[fno] : size;
	if(sfclass(&sfdesc[fno]) == SF_SHORT) {
	  for(i=0,ibuf = (short *)sndbuf[fno] + pointer[fno];i<todo;i += nchans) {
	    for(j=0; j<nchans; j++,ibuf++,out++) {
	      if(chlist[j]) {
		*ibuf = (short) *out;	
	      }
	    }
	  }
	}
	else {
	  for(i=0,fbuf = (float *)sndbuf[fno] + pointer[fno];i<todo;i += nchans) {
	    for(j=0; j<nchans; j++,fbuf++,out++) {
	      if(chlist[j]) {
		*fbuf = *out;
	      }
	    }
	  }
	}
	pointer[fno] += todo;

	if(pointer[fno] == len) {
		_backup(fno);
		if(!peakoff[fno])
			_chkpeak(fno);
		_writeit(fno);
		_readit(fno);
		pointer[fno] = 0;
	}

	if(size -= todo) goto refill;
}

baddout(out,fno,size)
register float *out;   
{
	register int i;
	register short *ibuf;
	short t_isamp;
	register float *fbuf;
	register int todo,remains;
	int len = bufsize[fno]; 
	
 refill:	todo = ((pointer[fno] + size) > len) 
		  ? len - pointer[fno] : size;
	
	if(sfclass(&sfdesc[fno]) == SHORT) {
	  for(i=0,ibuf = (short *)sndbuf[fno] + pointer[fno];i<todo;i++) {
	    *(ibuf++) += (short) *(out++);
	  }
	}

	else {
	  for(i=0,fbuf = (float *)sndbuf[fno] + pointer[fno];i<todo;i++) {
	    *(fbuf++) += *(out++);
	  }
	}

	pointer[fno] += todo;

	if(pointer[fno] == len) {
		_backup(fno);
		if(!peakoff[fno])
			_chkpeak(fno);
		_writeit(fno);
		_readit(fno);
		pointer[fno] = 0;
	}
	
	if(size -= todo) goto refill;
}

bwipeout(out,fno,size)
register float *out;   
{
	register int i;
	register short *ibuf;
	short t_isamp;
	register float *fbuf;
	register int todo,remains;
	int len = bufsize[fno]; 

	
refill:	todo = ((pointer[fno] + size) > len) 
				? len - pointer[fno] : size;

	if(sfclass(&sfdesc[fno]) == SHORT) {
	  for(i=0,ibuf = (short *)sndbuf[fno] + pointer[fno];i<todo;i++) {
	    *(ibuf++) = (short)*(out++);
	  }
	}
	else {
	  for(i=0,fbuf = (float *)sndbuf[fno] + pointer[fno];i<todo;i++) {
	    *(fbuf++) = *(out++);
	  }
	}

	pointer[fno] += todo;

	if(pointer[fno] == len) {
		if(wipe_is_off[fno]) {   
			_backup(fno);
			wipe_is_off[fno] = 0;
			}
		if(!peakoff[fno])
			_chkpeak(fno);
		_writeit(fno);
		pointer[fno] = 0;
	}
	if(size -= todo) goto refill;
}

endnote(xno)
{
	struct timeval tp;	
	struct timezone tzp;	
	int i,j,final_bytes,fno,err;
	float notepeak,*pk;
	double total;
	long *pkloc;
	struct tms timbuf;
	float peakval,getpeakval();
	SFHEADER      tSFHeader;
	SFMAXAMP      tSFMaxamp;
	SFCODE        tSFCode;
	struct stat st;
	short tisamp,*tibuf;
	float tfsamp,*tfbuf;

	fno = ABS(xno);  /* if fno is negative it means don't write
					final buffer,just pretend to */
	if(wipe_is_off[fno]) 
		_backup(fno); 
	/* else _flushbuf(fno); */
	if(!peakoff[fno]) _chkpeak(fno);
	final_bytes =  pointer[fno]  * sfclass(&sfdesc[fno]);
	
	/* This was DS's and PL's version of real time */
	/* Not used in this version */
#ifdef OLDRT

	/*  SHOULD NOT PLAY HERE -- LAST BUFFERS ALREADY PLAYED */
	if ((sfclass(&sfdesc[fno]) == SF_SHORT) && play_is_on)
		playbuf(sndbuf[fno],final_bytes/SF_SHORT);
	else if ((sfclass(&sfdesc[fno]) == SF_FLOAT) && play_is_on) {
		peakval = getpeakval(peakflag,fno);
		playfbuf(sndbuf[fno],peakval,swap[fno],nbytes/SF_FLOAT);
	}
#endif

	/* write out only fractional part of last record, god bless unix!*/
	if(pointer[fno] && (play_is_on < 2)) {
		if(xno >= 0) {
			/* Swap bytes if necessary */
			if(final_bytes && swap_bytes[fno]) {
				/* SHORT file */
				if(sfclass(&sfdesc[fno]) == SF_SHORT) {
					tibuf = (short *)sndbuf[fno]; 
					for (i=0;i<final_bytes/SF_SHORT;i++) {
						tisamp = *(tibuf+i);
						*(tibuf+i) = reverse_int2(&tisamp);
					}
				}
				/* FLOAT file */
				if(sfclass(&sfdesc[fno]) == SF_FLOAT) {
					tfbuf = (float *)sndbuf[fno]; 
					for (i=0;i<final_bytes/SF_FLOAT;i++) {
						/* 	byte_reverse4(tfbuf+i); */
						/* 	tfsamp = *(tfbuf+i); */
						/* 	*(tfbuf+i) = (float)reverse_int4(&tfsamp); */
					  	tfsamp = *(tfbuf+i);
						byte_reverse4(&tfsamp);
					  	*(tfbuf+i) = tfsamp;
					}
				}
   			}
   			if((i = write(sfd[fno],sndbuf[fno],final_bytes)) 
											!= final_bytes) {
				fprintf(stderr,
					"CMIX: Bad UNIX write, file %d, nbytes = %ld\n",
					fno,i);
				perror("write");
				closesf();
   			}
   		}
   		if((filepointer[fno] += final_bytes) > originalsize[fno]) 
   		if(xno >0)  originalsize[fno] = filepointer[fno];
	}
	/* DT: 	if(play_is_on) flush_buffers(); */
	
	pk = (float *)peak[fno];
	pkloc = (long *)peakloc[fno];
	total = ((double)filepointer[fno]-headersize[fno])
					/((double)sfclass(&sfdesc[fno]))
					/(double)sfchans(&sfdesc[fno])/SR;
	
	/* _writeit(fno);  /*  write out final record */

	for(i = 0,notepeak=0; i<sfchans(&sfdesc[fno]); i++) { 
		if(*(pk+i) > sfmaxamp(&sfm[fno],i)) {
			sfmaxamp(&sfm[fno],i) = *(pk+i);
			sfmaxamploc(&sfm[fno],i) = *(pkloc+i);
		}
		if(*(pk+i) > notepeak) notepeak = *(pk+i);
	}
	
	gettimeofday(&tp,&tzp);
	sfmaxamptime(&sfm[fno]) = tp.tv_sec;
		
	if((filepointer[fno] = lseek(sfd[fno],0L,0)) < 0) {
		fprintf(stderr,"Bad lseek to beginning of file\n");
		perror("lseek");
		closesf();
	}


	times(&timbuf);

	printf("\n(%6.2f)",(float)(
					(timbuf.tms_stime-clockin[fno].tms_stime)+
					(timbuf.tms_utime-clockin[fno].tms_utime))/60.);
	printf(" %9.4f .. %9.4f MM ",starttime[fno],total);
	
	if(!peakoff[fno]) {
		for(j=0;j<sfchans(&sfdesc[fno]);j++)
			printf(" c%d=%e",j,*(pk+j));
		printf("\n");
		if(punch[fno]) {
			printf("alter(%e,%e,%e/%e",
						(double)starttime[fno],(double)(total-starttime[fno]),
						punch[fno],notepeak);
			for(i=0; i<sfchans(&sfdesc[fno]); i++)
				printf(",1 ");
			printf(")\n");
			printf("mix(%g,%g,%g,%g/%g",
							(double)starttime[fno],(double)starttime[fno],-(double)(total-starttime[fno]),punch[fno],notepeak);
			for(i=0; i<sfchans(&sfdesc[fno]); i++)
				printf(",%d ",i);
			printf(")\n");
		}
	}

#ifdef USE_SNDLIB

	/* Copy the updated peak stats into the SFHEADER struct for this
	   output file. (No swapping necessary.)
	*/
	memcpy(&(sfmaxampstruct(&sfdesc[fno])), &sfm[fno], sizeof(SFMAXAMP));

	/* Write header to file. */
	if (wheader(sfd[fno], &sfdesc[fno])) {
		fprintf(stderr, "endnote: bad header write\n");
		perror("write");
		closesf();
	}

#else /* !USE_SNDLIB */

	/* This was the last part of the biggest pain in the ass I ever */
	/* had to deal with in all my hacking into cmix.  In short, byte_swapping */
	/* sucks ! Dave Topper - Feb. 5, 1998 */
	/* At this point I thought I was finished */
	/* I was wrong */
	/* It remained a pain the ass until Feb 10.  I hope I'm done now */
	/* Nope:  2/22/97 - 11:53pm */ 

	/* Here we need to do some header maintenance for the old Next stuff */
	if (!is_Next[fno]) {
		if(stat((char *)sfname[fno],&st))  {
			fprintf(stderr, "putlength:  Couldn't stat file %s\n",sfname);
			return(1);
		}
		NSchans(&sfdesc[fno]) = sfchans(&sfdesc[fno]);
		NSmagic(&sfdesc[fno]) = SND_MAGIC;
		NSsrate(&sfdesc[fno]) = sfsrate(&sfdesc[fno]);
		NSdsize(&sfdesc[fno]) = (st.st_size - 1024);
		NSdloc(&sfdesc[fno]) = 1024;
	  
		switch(sfclass(&sfdesc[fno])) {
			case SF_SHORT:
				NSclass(&sfdesc[fno]) = SND_FORMAT_LINEAR_16;
				break;
			case SF_FLOAT:
				NSclass(&sfdesc[fno]) = SND_FORMAT_FLOAT;
				break;
			default:
				NSclass(&sfdesc[fno]) = 0;
				break;
		}
		if (swap_bytes[fno]) {
			byte_reverse4(&NSchans(&sfdesc[fno]));
  			byte_reverse4(&NSmagic(&sfdesc[fno]));
			byte_reverse4(&NSsrate(&sfdesc[fno]));
			byte_reverse4(&NSdsize(&sfdesc[fno]));
  			byte_reverse4(&NSdloc(&sfdesc[fno]));
  			byte_reverse4(&NSclass(&sfdesc[fno]));
  		}
 	}
	
	/* First, we swap the sfcode maxamp info, if we need to */
	swap = swap_bytes[fno];
	if (swap_bytes[fno]) {
	  
		for (i=0;i<SF_MAXCHAN;i++) { 
			byte_reverse4(&(sfm[fno].value[i])); 
			byte_reverse4(&(sfm[fno].samploc[i]));
		}
		byte_reverse4(&(sfm[fno].timetag));
	  
		/* Then we write that information to the header */
		/* NOTE:  the putsfcode swaps and un-swaps the sfm[] data internally */
		putsfcode(&sfdesc[fno],&sfm[fno],&ampcode); 
	  
		/* Then swap the main header struct info */
		byte_reverse4(&sfdesc[fno].sfinfo.sf_magic);
		byte_reverse4(&sfdesc[fno].sfinfo.sf_srate);
		byte_reverse4(&sfdesc[fno].sfinfo.sf_chans);
		byte_reverse4(&sfdesc[fno].sfinfo.sf_packmode); 
	  
		/* Then write that header to the soundfile descriptor */
		if(wheader(sfd[fno],(char *)&sfdesc[fno])) {  
			fprintf(stderr,"Bad header write\n");
			perror("write");
			closesf();
		}

		/* THEN (this was the tricky part we SWAP THE HEADER INFO BACK !*/
		byte_reverse4(&sfdesc[fno].sfinfo.sf_magic);
		byte_reverse4(&sfdesc[fno].sfinfo.sf_srate);
		byte_reverse4(&sfdesc[fno].sfinfo.sf_chans);
		byte_reverse4(&sfdesc[fno].sfinfo.sf_packmode); 
	}
	
	else {
		/* Just do the normal thing */
		putsfcode(&sfdesc[fno],&sfm[fno],&ampcode);
		if(wheader(sfd[fno],(char *)&sfdesc[fno])) {
			fprintf(stderr,"Bad header write\n");
			perror("write");
			closesf();
		}
	}

#endif /* !USE_SNDLIB */
}

_flushbuf(fno)
int fno;
{
	register i;
	for(i=pointer[fno]*sfclass(&sfdesc[fno]); i<nbytes; i++)
		*(sndbuf[fno] + i) = 0;
}

_chkpeak(fno)
int fno;
{
	register int i,incr;
	register short *ibuf,*bufend;
	register float *fbuf,*fbufend,*pk;
	short *ibufx;
	float *fbufx;
	long *pkloc,currentloc;

	pk = (float *)peak[fno];
	incr = sfchans(&sfdesc[fno]);
	pkloc = (long *)peakloc[fno];

	if(sfclass(&sfdesc[fno]) == SHORT) {
		ibufx = ibuf = (short *)sndbuf[fno];
		bufend = ibuf + pointer[fno]; /* to allow for final check */
		currentloc = (long)
				((filepointer[fno]-headersize[fno])/(SHORT * incr));
		while(ibuf<bufend)  {
			for(i=0; i<incr; i++)  {
				if(ABS(*(ibuf + i)) > (int)*(pk+i)) {
					*(pk+i) = ABS(*(ibuf + i)); 
					*(pkloc+i) = currentloc + 
					(long)((ibuf - ibufx)/incr);
				}
			}
			ibuf += incr;
		}
	}
	else {
		fbufx = fbuf = (float *)sndbuf[fno];
		fbufend = fbuf + pointer[fno];
		currentloc = (long)
				((filepointer[fno]-headersize[fno])/(FLOAT * incr));
		while(fbuf<fbufend) {
			for(i=0; i<incr; i++)  {
				if(ABS(*(fbuf + i)) > *(pk+i)) {
					*(pk+i) = ABS(*(fbuf + i));
					*(pkloc+i) = currentloc +
					(long)((fbuf - fbufx)/incr);
				}
			}
			fbuf += incr;	
		} 
	} 
}

peak_off(p,n_args)
float *p;
{
	peakoff[(int)p[0]] = (char)p[1];
	if(p[1]) printf("      peak check turned off for file %d\n",(int)p[0]);
		else
		 printf("      peak check turned on for file %d\n",(int)p[0]);
}
punch_on(p,n_args)
float *p;
{
	punch[(int)p[0]] = p[1];
	if(!p[1]) printf("      punch turned off for file %d\n",(int)p[0]);
		else
		 printf("      punch check turned on for file %d\n",(int)p[0]);
}

_readit(fno)
int fno;
{
	int i,n,maxread;
	short tisamp,*tibuf;
	float tfsamp,*tfbuf;

	/* check to see if we are attempting to read beyond current
	 * EOF, and if so adjust amount to be read and flush rest of buffer
	 */

	if(filepointer[fno] > originalsize[fno])
		maxread = 0;
	else if((filepointer[fno]+nbytes) > originalsize[fno]) 
		maxread = originalsize[fno]-filepointer[fno];
	else
		maxread = nbytes;
	
	if((play_is_on <  3) || (status[fno] == 0)) {
		if((n = read(sfd[fno],sndbuf[fno],maxread)) != maxread) {
			if(!n) {
				/*if(istape[fno] && n) continue;*/
				perror("read");
				fprintf(stderr,
				    "CMIX: Bad UNIX read, nbytes = %ld\n",n);
				fprintf(stderr, " sfd[fno]= %d\n",sfd[fno]);
			        closesf();
			}
		}
	}
	if(((play_is_on==2) && !maxread) || (play_is_on==3) && (status[fno]))
	      bzero(sndbuf[fno],nbytes);  /* clean buffer out if not readin */

	/* Swap input buffer */
 	if(maxread && swap_bytes[fno]) {
		/* SHORT file */
		if(sfclass(&sfdesc[fno]) == SF_SHORT) {
			tibuf = (short *)sndbuf[fno]; 
			for (i=0;i<nbytes/SF_SHORT;i++) {
				tisamp = *(tibuf+i);
				*(tibuf+i) = reverse_int2(&tisamp);
			}
		}
		/* FLOAT file */
		if(sfclass(&sfdesc[fno]) == SF_FLOAT) {
			tfbuf = (float *)sndbuf[fno]; 
			for (i=0;i<nbytes/SF_FLOAT;i++) {
				/* byte_reverse4(tfbuf+i); */
				/* tfsamp = *(tfbuf+i); */
				/* *(tfbuf+i) = (float)reverse_int4(&tfsamp); */
				tfsamp = *(tfbuf+i);
				byte_reverse4(&tfsamp);
				*(tfbuf+i) = tfsamp;
			}
		}
	}

	/*  if we haven't read in full buffer, zero out rest of buffer,
	 *  and adjust filepointer with lseek.  Otherwise just update 
	 *  filepointer.  This will position pointer properly for any
	 *  situation.  Only a write will change physical size of file.
	 */
	if(play_is_on < 2) {        		
		if(maxread < nbytes) {
			for(n=maxread; n<nbytes; n++) *(sndbuf[fno] + n) = 0;
			filepointer[fno] = lseek(sfd[fno],(nbytes-maxread),1);
		}               
		else filepointer[fno] += nbytes;
	}
	else filepointer[fno] += nbytes;
	return(maxread ? n : maxread);
}

_writeit(fno)
int fno;
{
	int i,n;
	short tisamp,*tibuf;
	float tfsamp,*tfbuf;
	float getpeakval(),peakval;

	if(!status[fno]) {
		fprintf(stderr,"File %d is write-protected!\n",fno);
		closesf();
	}
  
#ifdef OLDRT
	/*  to play before writing */
	if((sfclass(&sfdesc[fno]) == SF_SHORT) && play_is_on)
		playbuf(sndbuf[fno],nbytes/SF_SHORT);
  
	/* swap and/or play floating point files */
	if(play_is_on && (sfclass(&sfdesc[fno]) == SF_FLOAT)) {
		peakval = getpeakval(peakflag,fno);
		playfbuf(sndbuf[fno],peakval,swap_bytes[fno],nbytes/SF_FLOAT);
	}
	else {	/* just swap if necessary */
		if(swap_bytes[fno] && (sfclass(&sfde sc[fno]) == SF_FLOAT))
		bytrev4(sndbuf[fno],nbytes);
	}
#endif

	if(swap_bytes[fno]) {
		/* SHORT file */
		if(sfclass(&sfdesc[fno]) == SF_SHORT) {
			tibuf = (short *)sndbuf[fno]; 
			for (i=0;i<nbytes/SF_SHORT;i++) {
				tisamp = *(tibuf+i);
				*(tibuf+i) = (short) reverse_int2(&tisamp);
			}
		}
		/* FLOAT file */
		if(sfclass(&sfdesc[fno]) == SF_FLOAT) {
			tfbuf = (float *)sndbuf[fno]; 
			for (i=0;i<nbytes/SF_FLOAT;i++) {
				/* byte_reverse4(tfbuf+i); */
				/* tfsamp = *(tfbuf+i); */
				/* *(tfbuf+i) = (float) reverse_int4(&tfsamp); */
				tfsamp = *(tfbuf+i);
				byte_reverse4(&tfsamp);
				*(tfbuf+i) = tfsamp;
			}
		}
	}

	if(play_is_on < 2) {
		if((n = write(sfd[fno],sndbuf[fno],nbytes)) != nbytes) {
			fprintf(stderr,
					"CMIX: Bad UNIX write, file %d, nbytes = %ld\n",fno,n);
			perror("write");
			closesf();
		}
		/* update output file size */
		if((filepointer[fno] += nbytes) > originalsize[fno]) 
			originalsize[fno] = filepointer[fno];
	}
  
	if(!play_is_on)
		printf(".");

	return(n);
}

_backup(fno)     /* utility routine to backspace one 'record' */
{

	if(play_is_on >= 2) return; 

	if((filepointer[fno] = lseek(sfd[fno],(long)-nbytes,SEEK_CUR)) < 0) {
		fprintf(stderr,"CMIX: bad back space in file %d\n",fno);
		perror("lseek");
		closesf();
	}
}

_forward(fno)     /* utility routine to forwardspace one 'record' */
{
	if((filepointer[fno] = lseek(sfd[fno],(long)nbytes,1)) < 0) {
		fprintf(stderr,"CMIX: bad forward space  in file %d\n",fno);
		perror("lseek");
		closesf();
	}
}

closesf()
{
	int i;

	for(i = 0; i<NFILES; i++) {
		if(isopen[i]) {
			if (status[i]) 
				putlength(sfname[i], sfd[i], &sfdesc[i]);
#ifndef USE_SNDLIB
 #ifdef sgi
			if(sfdesc[i].sfinfo.handle) AFclosefile(sfdesc[i].sfinfo.handle);
			sfdesc[i].sfinfo.handle = NULL;
 #endif
#endif /* !USE_SNDLIB */
			close(sfd[i]);
		}
	}
	exit(0);
}

m_clean(p,n_args)
float *p;		/* a fast clean of file, after header */
{
/* if p1-> = 0, clean whole file, else skip=p1, dur=p2, ch-on? p3--> */
	int i,todo,nwrite,n;
	char *point;
	int fno,segment,zapsize,chlist[4];
	int skipbytes;

	fno = (int) p[0];
	skipbytes = 0;
	if(!status[fno]) {
		fprintf(stderr,"fno %d is write-protected!\n",fno);
		closesf();
	}
#ifdef USE_SNDLIB
	todo = originalsize[fno] - headersize[fno];
#else
	todo = originalsize[fno] - sizeof(SFHEADER);
#endif

	segment = (n_args > 1) ? 1 : 0;

	if(segment) {
		skipbytes = (p[1] > 0) ? p[1] * sfclass(&sfdesc[fno]) *
			    SR * sfchans(&sfdesc[fno]) 
			    : -p[1] * sfclass(&sfdesc[fno]) * 
							 sfchans(&sfdesc[fno]);
		todo =  (p[2] > 0) ? p[2] * sfclass(&sfdesc[fno]) * 
			SR * sfchans(&sfdesc[fno])
			: -p[2] * sfclass(&sfdesc[fno]) * 
						sfchans(&sfdesc[fno]);
		for(i=0; i<sfchans(&sfdesc[fno]); i++) chlist[i] = p[i+3];
	}
	point = (char *)sndbuf[fno];
	if(!segment) for(i=0; i<nbytes; i++) *(point+i) = 0;

	if((filepointer[fno] = 
	   lseek(sfd[fno],skipbytes+headersize[fno],0)) == -1) {
		fprintf(stderr,"CMIX: bad sflseek in clean\n");
		closesf();
	}
	printf("Clean %d bytes\n",todo);
	while(todo) {
		nwrite = (todo > nbytes) ? nbytes : todo;
		if(segment) {
			if((n = read(sfd[fno],sndbuf[fno],nwrite)) 
					== 0) { /* allow for fractional reads*/
				fprintf(stderr,
                 		"CMIX: Apparent eof in clean\n",n);
				return;
			}
			if(lseek(sfd[fno],-n,1) < 0) {
				fprintf(stderr,"Bad UNIX lseek in clean\n");
				closesf();
			}
			m_zapout(fno,sndbuf[fno],n,chlist);
			nwrite = n;
		}
		if((n = write(sfd[fno],sndbuf[fno],nwrite)) == 0) {
			fprintf(stderr,
                 	"CMIX: Apparent eof in clean\n",n);
	        	closesf();
		}
		todo -= n;
	}
	if(!segment) {
		if((lseek(sfd[fno],0,0)) == -1) {
			fprintf(stderr,"CMIX: bad lseek in clean\n");
			closesf();
		}

		for(i = 0; i<sfchans(&sfdesc[fno]); i++) { 
			sfmaxamp(&sfm[fno],i) = 0;
			sfmaxamploc(&sfm[fno],i) = 0;
		}

		putsfcode(&sfdesc[fno],(char *)&sfm[fno],&ampcode);

		if(wheader(sfd[fno],(char *)&sfdesc[fno])) {
			fprintf(stderr,"Bad header write\n");
			perror("write");
			closesf();
		}
	}
	else 
		if((lseek(sfd[fno],headersize[fno],0)) == -1) {
			fprintf(stderr,"CMIX: bad lseek in clean\n");
			closesf();
		}
	filepointer[fno] = headersize[fno];
	printf("Clean successfully finished.\n");
}

m_zapout(fno,buffer,nwrite,chlist)
char *buffer;
int *chlist;
{
	float *fbuf;
	int i,j,nchunks,chans;
	short *ibuf;

	chans = sfchans(&sfdesc[fno]);

	if(sfclass(&sfdesc[fno]) == SF_SHORT) {
		ibuf = (short *) buffer;
		nchunks = nwrite/SF_SHORT;
		for(i=0; i<nchunks; i += chans)
			for(j=0; j<chans; j++)
				if(chlist[j]) *(ibuf+j+i) = 0;
	}
	else {
		fbuf = (float *) buffer;
		nchunks = nwrite/SF_FLOAT;
		for(i=0; i<nchunks; i += chans) 
			for(j=0; j<chans; j++)
				if(chlist[j]) *(fbuf+j+i) = 0;
	}
}
float
getpeakval(peakflag,fno)
float peakflag;
{
	float opeak;
	int i;
	float *pk;
	pk = (float *)peak[fno];

	if(peakflag < 0) {
		for(i=0,opeak=0; i<sfchans(&sfdesc[fno]); i++)
			if(pk[i] > opeak) 
					opeak=pk[i];
	}
	else if(peakflag == 0) {
		for(i=0,opeak=0; i<sfchans(&sfdesc[fno]); i++)
			if((float)sfmaxamp(&sfm[fno],i) > opeak) 
					opeak=sfmaxamp(&sfm[fno],i);
	}
	else opeak = peakflag;
/*	printf("peakflag=%f, peakval=%f\n",peakflag,opeak); */
	return(opeak);
}

#ifdef OBSOLETE
extern int init_sound();

double
play_on(p,n_args)
float *p;
{	
	int output;
	output = (int)p[0];
	if(!isopen[output]) {
		fprintf(stderr,"You haven't opened file %d yet!\n",output);
		closesf();
	}
	if(p[1] == 0) play_is_on = 1;  /* play and write to disk */
	if(p[1] == 1) play_is_on = 2;  /* play and read disk, but don't write */
	if(p[1] == 2) play_is_on = 3;  /* play but don't read disk */
	peakflag = p[2];
	if(print_is_on)
		printf("%s\n",
		       (play_is_on == 1) ? "writing to and playing from disk"
		       : (play_is_on == 2) ? "playing and reading disk only"
		       : (play_is_on == 3) ? "playing without reading disk"
		       : ""
		);
	if(p[1] > 2. || p[1] < 0.) {
		fprintf(stderr, "illegal value for p[1]\n");
		closesf();
	}
	if(print_is_on) {
	    printf(peakflag < 0 ? "scaling to current overall peak" :
	       peakflag == 0.0 ? "scaling to file peak value" :
	       "scaling to peak of %f", peakflag); 
	    printf("\n");
	}
	return init_sound((float)sfsrate(&sfdesc[output]),
		sfchans(&sfdesc[output]));
}

#else /* !OBSOLETE */

double
play_on(float p[], int n_args)
{	
   fprintf(stderr, "Sorry, this version of RTcmix does not support "
                   "the old cmix method of playing real-time audio.");
   return 0.0;
}

#endif /* !OBSOLETE */

