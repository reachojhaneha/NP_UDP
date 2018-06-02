
#ifndef	__sendmessages_h
#define	__sendmessages_h

void        init_sender(int window, int f, int awindow);
int         isswfull();
void        setsecondaryfd(int s);
void        insertmsg(void *outbuff, int outlen);
void        setprimaryfd(int s);
typedef int (*callback)(int numberofsegments);
int         dg_send(callback c); 
#endif /* __sendmessages_h */
