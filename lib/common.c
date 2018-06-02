#include "common.h"

void getstring(char *buf, int bufsize, FILE *f) 
{
    char *ch;
    int s;
    ch = fgets(buf, bufsize, f);
    if(ch == NULL) {
        perror("Error reading from file");
        fclose(f);
        exit(1);
    }
    s = strlen(buf);
    if(buf[s-1] == '\n')
        buf[s - 1]  = 0;

}

int getint(FILE *f) 
{
    char buf[100];
    int ret;
    getstring(buf, 100, f);

    ret = strtol(buf, NULL, 10);
    if (errno) {
        perror("Error while converting string to int");
        fclose(f);
        exit(1);
    }
    return ret;
}

float getfloat(FILE *f) 
{
    char buf[100];
    float ret;
    getstring(buf, 100, f);

    ret = strtof(buf, NULL);
    if (errno) {
        perror("Error while converting string to float");
        fclose(f);
        exit(1);
    }
    return ret;
}

void *zalloc(size_t size)
{
    void *p = malloc(size);
    assert(p!=NULL);
    memset(p, 0, size);
    return p;
}

void setitimerwrapper(struct itimerval *timer, long time)
{
    timer->it_interval.tv_sec = time / 1000;
    timer->it_interval.tv_usec = (time%1000) * 1000;
    timer->it_value.tv_sec = time / 1000;
    timer->it_value.tv_usec = (time%1000) * 1000;
    setitimer(ITIMER_REAL, timer, NULL);
}

void debug_i(const char *fmt, ...)
{
#ifdef NDEBUGINFO
    return;
#endif
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
