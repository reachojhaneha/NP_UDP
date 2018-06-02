
#ifndef	__common_h
#define	__common_h

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <sys/time.h>
#define SEGLENGTH 512

struct hdr {
    uint64_t seq;    /* sequence # */
    int window_size; /* window size*/
};

struct connectioninfo {
    char command[256];
    char filename[256];
    int window;
};

void  getstring(char *buf, int bufsize, FILE *f);

int   getint(FILE *f);

float getfloat(FILE *f); 

const static int datalength = SEGLENGTH - sizeof(struct hdr);

void  *zalloc(size_t size);
void  setitimerwrapper(struct itimerval *timer, long time);


void  debug_i(const char *format, ...);
#endif	
