#include "my_unprtt.h"
#include "common.h"
#include "sendmessages.h"
#include <setjmp.h>

#define RTT_DEBUG

static struct rtt_info  rttinfo;
static int              rttinit = 0, cwin = 1, sst = 0, sw = 0, awindow;
static int              packintransit = 0, secondaryfd = 0, fd = 0;
static uint64_t         sequence = 0;
static int              head = -1, tail = -1, current = -1, csize = 0;
static struct msghdr   *msgsend =  NULL;
static struct msghdr    msgrecv; /* assumed init to 0 */
static sigjmp_buf       jmpbuf;

static void sig_alrm(int signo)
{
    siglongjmp(jmpbuf, 1);
}

void init_sender(int window, int f, int padvwindow) 
{
    int i;
    sw = sst = window;
    msgsend = (struct msghdr *) zalloc(sw * sizeof(struct msghdr));
    for(i =0; i < sw; i++) {
        msgsend[i].msg_iovlen = 2;
        msgsend[i].msg_iov = (struct iovec *) malloc(2 * sizeof(struct iovec));
        msgsend[i].msg_iov[0].iov_len = sizeof(struct hdr);
        msgsend[i].msg_iov[0].iov_base = zalloc(sizeof(struct hdr));
    }
    fd = f;
    sst = awindow = padvwindow;
}

int isswfull()
{
    return csize == sw;
}

void setprimaryfd(int x)
{
    fd = x;
}

void insertmsg(void *out_buff, int out_len)
{
    if(head == -1) head = tail = 0;
    else tail = (tail + 1) % sw;
    csize++;

    struct msghdr *msgheader = &msgsend[tail];
    struct hdr    *header = (struct hdr *)(msgheader->msg_iov[0].iov_base);

    header->seq = sequence++;
    msgheader->msg_iov[1].iov_base = zalloc(out_len);
    memcpy(msgheader->msg_iov[1].iov_base, out_buff, out_len);
    msgheader->msg_iov[1].iov_len = out_len;
}

static void delete_from_sw()
{
    if(!csize) return;
    if(head == tail) head = tail = current = -1;
    else head = (head + 1) % sw;
    csize--;
    struct msghdr *m = &msgsend[head];
    free(m->msg_iov[1].iov_base);
}

static uint64_t getseq(struct msghdr *mh)
{
    return ((struct hdr *)mh->msg_iov[0].iov_base)->seq;
}

static void print_window_content(int newline)
{
    int i;
    printf("{");
    for(i = 0; i < csize; i++)
        printf("%" PRIu64 ", ", getseq(&msgsend[(head + i) % sw]));
    printf("}");
    if(newline)
        printf("\n");
}


static struct hdr *gethdr(struct msghdr *mh)
{
    return (struct hdr *)mh->msg_iov[0].iov_base;
}

int dg_send(callback c)
{
    int              window, cincr = 0;
    int              usesecondaryfd = 0, dupcount = 0, i, n, resettimer = 1;
    uint64_t         lastseq = -1;
    struct iovec     iovrecv[1];
    struct itimerval timer;
    struct sigaction sa;
    long             stimer = 0, rto;
    int              has_more_pkts = 1;
    struct msghdr   *m;
    char            *congestion_info_msg;
    struct hdr      *h, recvhdr;

    memset (&sa, 0, sizeof (sa));
    sa.sa_handler = &sig_alrm;
    if (0 == rttinit) {
        rtt_init(&rttinfo); /* first time we're called */
        rtt_d_flag = 1;
        rttinit = 1;
    }

    msgrecv.msg_iov = iovrecv;
    msgrecv.msg_iovlen = 1;
    msgrecv.msg_name = NULL;
    msgrecv.msg_namelen = 0;
    bzero(&recvhdr, sizeof(struct hdr));
    iovrecv[0].iov_base = (char *)&recvhdr;
    iovrecv[0].iov_len = sizeof(struct hdr);

    sigaction(SIGALRM, &sa, NULL);
    rtt_newpack(&rttinfo); /* initialize for this packet */

sendagain:
    window = min(cwin - packintransit, sw);
    window = min(window, awindow - packintransit);
    window = min(window, csize - packintransit);

    rto = rtt_start(&rttinfo);
    if(cwin < sst)
        congestion_info_msg = "SLOW START";
    else
        congestion_info_msg = "CONGESTION AVOIDANCE";

    printf("\ncwin = %d, ", cwin);
    printf("sst = %d, %s, ", sst, congestion_info_msg);
    printf("adv window = %d, ", awindow);
    printf("packet in transit = %d, ", packintransit);
    printf("window content =");
    print_window_content(1);
    rtt_debug(&rttinfo);

    for(i = 0; i < window; i++) {
        if(csize < sw && current == tail)
            break;

        if(!i)
            printf("Sending segment(s)");

        current = (current + 1) % sw;
        m = &msgsend[current];
        h = gethdr(m);
        printf (" %" PRIu64 ", ", h->seq);

        n = sendmsg(fd, m, 0);
        packintransit++;

        if(usesecondaryfd && secondaryfd)
            n = sendmsg(secondaryfd, m, 0);

        if(n < 0 && !usesecondaryfd) {
            perror(" Error in sending data");
            return -1;
        }

        if(n < 512 && h->seq)
            printf(" Last packet sent.");
    }

    if(resettimer) {
        stimer = rtt_ts(&rttinfo);
        resettimer = 0;
    }

    if(window)
        printf("\n");

    usesecondaryfd = 0;
    setitimerwrapper(&timer, rto);

    if (sigsetjmp(jmpbuf, 1) != 0) {
        if (rtt_timeout(&rttinfo) < 0) {
            errno = ETIMEDOUT;
            rttinit = 0; /* reinit in case we're called again */
            err_msg("No response from client, giving up");
            return (-1);
        }

        packintransit = 0;
        current = (head + sw - 1) % sw;

        if(0 == awindow) {
            awindow = 1;
            printf("Adv Window was 0, probing client\n");
            goto sendagain;
        }

        printf("Timeout at: %uus ... Resending,\n", rtt_ts(&rttinfo));
        sst = min(cwin/2, sw);
        sst = max(sst, 2);
        usesecondaryfd = 1;
        cincr = 0;
        cwin = 1;
        goto sendagain;
    }

recieveagain:
    h = gethdr(&msgsend[head]);
    if(secondaryfd) {
        do {
            n = recvmsg(secondaryfd, &msgrecv, 0);
        } while(n < 0);
    } else
        n = recvmsg(fd, &msgrecv, 0);

    if(n < 0 && errno !=EINTR) {
        perror("Fatal Error while reading from client");
        close(fd);
        return -1;
    } else if(n < 0 && errno == EINTR) {
        goto recieveagain;
    }

    if(recvhdr.seq < h->seq) {
        printf("Recieved old ack:%" PRIu64 ", discarding\n", recvhdr.seq);
        goto recieveagain;
    }

    if(recvhdr.seq == h->seq + 1 || recvhdr.seq == h->seq) {
        resettimer = 1;
        rtt_stop(&rttinfo, rtt_ts(&rttinfo) - stimer);
        printf("Resetting rtt timer.\n");
    }

    if(recvhdr.seq == lastseq + 1) {
        printf("Recieved dup ack:%" PRIu64 ", dup count:%d\n", 
                recvhdr.seq, dupcount);
        dupcount++;
    } else {
        printf("Recieved ack:%" PRIu64 "\n", recvhdr.seq); 
        packintransit = max(0, packintransit - (int)(recvhdr.seq - h->seq));
        dupcount = 0;
        lastseq = recvhdr.seq - 1;
    }

    if(dupcount > 2) {
        setitimerwrapper(&timer, 0);
        m = &msgsend[head];
        h = gethdr(m);

        printf("Sending segment %" PRIu64 " again(Fast retransmission)\n", 
                recvhdr.seq);
        if(cwin > sst) {
            printf("Fast recovery\n");
            cwin = sst;
        }

        n = sendmsg(fd, m, 0);
        dupcount = 0;
        setitimerwrapper(&timer, rtt_start(&rttinfo));
        goto recieveagain;
    }

    setitimerwrapper(&timer, 0);
    if(cwin >= sst) {
        cincr += (int)(recvhdr.seq - h->seq);
        if(cincr >= cwin) {
            cwin++;
            cincr = cincr - cwin + 1;
        }
    } else {
        cincr = (int)(recvhdr.seq - h->seq);
        if(cwin + cincr > sst)
            cwin = sst;
        else
            cwin = cwin + cincr;
        cincr = 0;
    }

    awindow = recvhdr.window_size;
    for(i = 0; i < recvhdr.seq - h->seq; i++) {
        delete_from_sw();
    }

    if(has_more_pkts == 0 && csize == 0)
        return 0;

    if(has_more_pkts && (sw - csize))
        has_more_pkts = c(sw - csize); 

    if(csize && ((((current + 1) % sw) < head && (current + 1)%sw + sw >= head 
                    + csize) || (current + 1)%sw >= head + csize)) {
        current = (head + sw - 1)%sw;
    }

    rtt_newpack(&rttinfo);              /* initialize for this packet */
    goto sendagain;
}

void setsecondaryfd(int s)
{
    secondaryfd = s;
}
