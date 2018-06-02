#include "lib/my_unpifi.h"
#include "limits.h"
#include "lib/common.h"
#include "unprtt.h"
#include <setjmp.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>

#define TIMEOUTLIMIT 10

static struct SW {
    char *pkt;
    int length;
    uint64_t seq;
    pthread_spinlock_t lock;
} *sliding_window;

static const uint32_t local_addr = (127 << 24) + 1;
static uint64_t lastSeq = 0, headSeq = 0, firstSeq = 0, connected = 0;
static int last_adv_window = 0, timeout_counter = 0;
static int issamehost = 0, islocal = 0, dontroute = 1;
static float prob;
static int timeout = 0;
static int ssize, sockfd, rseed, n, mean, finish = 0;
static int finishConsumer = 0;
static struct timeval selectTime;
static struct hdr sendhdr, recvhdr;
static struct msghdr msgsend, msgrecv;
static struct iovec iovsend[1], iovrecv[2];
static void *inbuff;
static struct sockaddr_in servaddr, taddr;
static socklen_t len;
static pthread_t consumer;
static int OUTPUT_FD = STDOUT_FILENO;

static void print_sliding_window(int newline) {
    int i=0;
    debug_i("Sliding Window: {");
    for(i = (firstSeq%ssize); i <= (headSeq%ssize); i++) {
        if(sliding_window[i].pkt != NULL) {
            debug_i("%" PRIu64 ",", sliding_window[i].seq);
        } else {
            debug_i("-1,");
        }
    }
    debug_i("}, ");
    if(newline)
        debug_i("\n");
}

static int shouldDrop(int type, uint64_t seqNo) {
    double randomNo = drand48();
    int ret = 0;

    if(prob > randomNo) {
        if(type) {
            if (seqNo)
                debug_i("Dropping Ack: %" PRIu64 ", ", seqNo);
            else
                debug_i("Dropping First Connecting Packet. ");
        } else
            debug_i("Dropping Packet: %" PRIu64 ", ", seqNo);

        print_sliding_window(1);
        ret = 1;
    }
    return ret;
}


static int randomGen() {
    double number = 0;
    double ret = 0;

    number = drand48();
    ret = log(number) * mean * -1;
    ret *= 1000;

    return (int)ret;
}

int send_ack() {
    sendhdr.seq = lastSeq;
    int n;

    if(shouldDrop(1, lastSeq)) {
        return 1;
    }

    sendhdr.window_size = ssize - (int)(headSeq  - firstSeq) - 1;
    last_adv_window = ssize - (int)(headSeq  - firstSeq) - 1;
    debug_i("Sending Ack = %" PRIu64 ", Window Size= %d\n\n", sendhdr.seq, sendhdr.window_size);

    n = sendmsg(sockfd, &msgsend, 0);
    if(n<0) {
        perror("Error sending ack, exiting..!!");
        exit(1);
    }

    return 1;
}

static int initiateLock() {
    int locked = 0, i = 0;	
    for(i=0; i<ssize; i++) {
        if (pthread_spin_init(&(sliding_window[i].lock), 0) != 0) {
            locked = i;
            debug_i("\n spinlock init failed\n");
            break;
        }
    }

    for(i=0; i<locked; i++) {
        pthread_spin_destroy(&(sliding_window[i].lock));
    }

    return locked;
}

static void destroyLock() {
    int i;
    for (i=0; i<ssize; i++) {
        pthread_spin_destroy(&(sliding_window[i].lock));
    }
}

static void* consume() {
    int thisConsumed = 0;
    uint64_t iSeq, startSeq, endSeq;
    int length = 0;
    char *data;

    while(!finishConsumer) {
        startSeq = firstSeq;
        while(!thisConsumed) {
            iSeq = firstSeq % ssize;
            pthread_spin_lock(&(sliding_window[iSeq].lock));
            data = NULL;
            if (sliding_window[iSeq].pkt != NULL) {
                length = sliding_window[iSeq].length;
                data = sliding_window[iSeq].pkt;
                sliding_window[iSeq].pkt = NULL;
                firstSeq += 1;
            } else {
                thisConsumed = 1;
            }
            pthread_spin_unlock(&(sliding_window[iSeq].lock));

            if(data != NULL) {
                if(firstSeq != 1) {
#ifndef NCONTENT
                    debug_i("\n");
                    n = write(OUTPUT_FD, data, length);
                    debug_i("\n\n");
#endif
                }
                free(data);
            }
            if(firstSeq == headSeq + 1 && finish) {
                debug_i("Consumed Seq %" PRIu64 " - %" PRIu64 "\n", startSeq, firstSeq-1);
                debug_i("File Recieved\n");
                debug_i("Exiting consumer\n");
                return NULL;
            }

            if(last_adv_window == 0)
                send_ack();
        }

        endSeq = firstSeq;
        if(startSeq != endSeq) {
            debug_i("Consumed Sequence %" PRIu64 " : %" PRIu64 "\n", startSeq, endSeq-1);
            print_sliding_window(0);
        }
        usleep(randomGen());
        thisConsumed = 0;
    }
    debug_i("Exiting consumer\n");
    return NULL;
}

static void declare_msg_hdr()
{
    inbuff = zalloc(datalength * sizeof(char));

    msgsend.msg_iovlen = 1;
    msgsend.msg_iov = iovsend;

    iovsend[0].iov_len = sizeof(struct hdr);
    iovsend[0].iov_base = (char *)&sendhdr;

    msgrecv.msg_iovlen = 2;
    msgrecv.msg_iov = iovrecv;

    iovrecv[0].iov_len = sizeof(struct hdr);
    iovrecv[0].iov_base = (char *)&recvhdr;
    iovrecv[1].iov_len = datalength;
    iovrecv[1].iov_base = inbuff;
}

static void insert_to_SW(ssize_t num) {
    int length = num - sizeof(struct hdr);
    uint64_t newSeq = recvhdr.seq;
    char *temp;

    if(headSeq + last_adv_window < newSeq)
        goto exit;

    if(sliding_window[newSeq%ssize].pkt != NULL)
        goto exit;

    temp = (char *) zalloc (datalength);
    memcpy(temp, inbuff, length);

    pthread_spin_lock(&(sliding_window[newSeq % ssize].lock));
    sliding_window[newSeq % ssize].length = length;
    sliding_window[newSeq % ssize].pkt = temp;
    sliding_window[newSeq % ssize].seq = newSeq;
    pthread_spin_unlock(&(sliding_window[newSeq % ssize].lock));

    if(lastSeq == newSeq)
    {
        lastSeq++;
        while(sliding_window[(lastSeq)%ssize].pkt != NULL 
                && sliding_window[(lastSeq)%ssize].seq == lastSeq)
            lastSeq++;
    }

    if(num<SEGLENGTH && connected) 
        finish = 1;

    if(newSeq > headSeq)
        headSeq = newSeq;

exit:
    print_sliding_window(0);
}

void dg_recv_send(int sockfd)
{
    ssize_t num = 0;
    fd_set allset;
    uint64_t newSeq;
    do {
        debug_i("Recieving..!!\n");
        do {
            FD_ZERO(&allset);
            FD_SET(sockfd, &allset);
            select(sockfd+1, &allset, NULL, NULL, &selectTime);
            if(FD_ISSET(sockfd, &allset)) {
                num = recvmsg(sockfd, &msgrecv, 0);
                newSeq = recvhdr.seq;
                debug_i("Recieve Seq: %" PRIu64 "\n", newSeq);
                timeout_counter = 0;
            } else {
                timeout = 1;
                return;
            }

        } while(shouldDrop(0, recvhdr.seq));

    } while(newSeq < lastSeq && send_ack());

    if(newSeq)
        insert_to_SW(num);

    if(!connected) {
        len = sizeof(servaddr);
        taddr.sin_family = AF_UNSPEC;
        servaddr.sin_port   = strtol(inbuff, NULL, 10);
        connect(sockfd, (SA *)&servaddr, len);

        if (getpeername(sockfd, (SA *) &servaddr, &len) < 0) {
            close(sockfd);
            perror("Error getting socket info for server.");
            exit(1);
        }
        debug_i(" Server Address: %s\n", Sock_ntop((SA *) &servaddr, len));
        connected = 1;

        lastSeq = 1;
        firstSeq = 1;

        debug_i("Creating consumer thread\n");
        pthread_create(&consumer, NULL, consume, NULL);
    }

    send_ack();
}

void resolveips(struct sockaddr_in *servaddr, struct sockaddr_in *cliaddr, uint32_t serverip)
{
    uint32_t clientip, pnetmask = 0, netmask;
    struct ifi_info *ifi, *ifihead;
    struct sockaddr_in *sa;

    for (ifihead = ifi = Get_ifi_info(AF_INET, 1);
            ifi != NULL; ifi = ifi->ifi_next) {
        debug_i(" IP addr: %s, ", Sock_ntop_host(ifi->ifi_addr, sizeof(*ifi->ifi_addr)));
        debug_i("Network Mask: %s\n", Sock_ntop_host(ifi->ifi_ntmaddr, sizeof(*ifi->ifi_ntmaddr)));

        if(!issamehost)
        {
            sa = (struct sockaddr_in *) ifi->ifi_addr;
            clientip = htonl(sa->sin_addr.s_addr);
            if(clientip == serverip)
            {
                issamehost = 1;
                servaddr->sin_addr.s_addr = cliaddr->sin_addr.s_addr = ntohl(local_addr);
                continue;
            }
            sa = (struct sockaddr_in *) ifi->ifi_ntmaddr;
            netmask = htonl(sa->sin_addr.s_addr);
            if(!islocal || netmask > pnetmask)
            {
                if( (clientip & netmask) == (serverip & netmask))
                {
                    islocal = 1;
                    pnetmask = netmask;
                    sa = (struct sockaddr_in *) ifi->ifi_addr;
                    cliaddr->sin_addr.s_addr = sa->sin_addr.s_addr;
                }
            }
        }
    }
    if(issamehost) {
        debug_i("Server is on same host.\n");
        servaddr->sin_addr.s_addr = cliaddr->sin_addr.s_addr = ntohl(local_addr);
    } else if(islocal) {
        debug_i("Server is local.\n");
    } else {
        dontroute = 0;
        debug_i("Server is not local.\n");
        ifi = ifihead;
        sa = (struct sockaddr_in *) ifi->ifi_addr;
        if(ifihead->ifi_next && local_addr == htonl(sa->sin_addr.s_addr))
            ifi = ifi->ifi_next;
        sa = (struct sockaddr_in *) ifi->ifi_addr;
        cliaddr->sin_addr.s_addr = sa->sin_addr.s_addr;
    }
    debug_i(" IPserver: %s, ", inet_ntoa(servaddr->sin_addr));
    debug_i("IPclient: %s\n", inet_ntoa(cliaddr->sin_addr));
    free_ifi_info(ifihead);
}

int main(int argc, char **argv)
{
    int sport, reuse = 1, i=0;
    FILE *infile;
    struct connectioninfo info;
    char serveripaddr[16];
    struct sockaddr_in cliaddr;
    uint32_t serverip;
    time_t t1;

    time(&t1);

    infile = fopen("client.in", "r");
    if(infile == NULL) {
        perror("Error opening file - client.in");
        exit(1);
    }
    getstring(serveripaddr, 16, infile);
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    if (inet_pton(AF_INET, serveripaddr, &servaddr.sin_addr) <= 0) 
    {
        debug_i("inet_pton error for %s\n", serveripaddr);
        fclose(infile);
        exit(1);
    }
    serverip = htonl(servaddr.sin_addr.s_addr);
    if(serverip == local_addr) {
        issamehost = 1;
    }

    bzero(&info, sizeof(info));
    sport = getint(infile);
    servaddr.sin_port   = htons(sport);
    rseed = getint(infile);
    prob = getfloat(infile);
    info.window = ssize = last_adv_window = getint(infile);
    mean = getint(infile);

    fclose(infile);

    int choice; 
    char command[256];
    do {
        sliding_window = (struct SW *)zalloc(ssize*sizeof(struct SW));

        srand48((long) t1 + rseed);
        //debug_i("initiating locks..!!\n");
        if(initiateLock() != 0) {
            //free all allocated memory and exit the program
            exit(-1);
        }
        //debug_i("locks initiated..!!\n");

        for(i=0; i<ssize; i++) {
            sliding_window[i].pkt = NULL;
            sliding_window[i].seq = -1;
            sliding_window[i].length = 0;
        }

        bzero(&cliaddr, sizeof(cliaddr));
        cliaddr.sin_family = AF_INET;
        resolveips(&servaddr, &cliaddr, serverip);
        cliaddr.sin_port = htons(0);

        sockfd = Socket (AF_INET, SOCK_DGRAM, 0);

        if(dontroute)
            setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR | SO_DONTROUTE, &reuse, sizeof(reuse));
        else
            setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        len = sizeof(cliaddr);
        if(bind(sockfd, (SA *) &cliaddr, len) < 0) {
            perror("Not able to make local connection");
        }
        len = sizeof(servaddr);
        Connect(sockfd, (SA *) &servaddr, len);
        if (getpeername(sockfd, (SA *) &servaddr, &len) < 0)
        {
            perror("Error getting socket info for server.");
            close(sockfd);
            exit(1);
        }

        len = sizeof(cliaddr);
        if (getsockname(sockfd, (SA *) &cliaddr, &len) < 0)
        {
            perror("Error getting socket info for client.");
            close(sockfd);
            exit(1);
        }

        debug_i(" Client Address: %s\n", Sock_ntop((SA *) &cliaddr, len));
        debug_i(" Server Address: %s\n\n", Sock_ntop((SA *) &servaddr, len));

        printf("Valid Commands: \nlist");
        debug_i(" - List the files available in the server’s public directory.");
        printf("\ndownload FILE [>FILENAME]");
        debug_i(" - Request that the server transmit one of the available files. The file being downloaded will be displayed on stdout unless the redirection symbol and file name are specified.");
        printf("\nquit");
        debug_i(" - Quit the client gracefully\n");
        printf("\n$ ");
        fflush(stdout);
        int n = read(fileno(stdin), command, 256);
        command[n - 1] = '\0';
        if(strcmp("list", command) == 0) {
            choice = 1;
            strcpy(info.command, "list");
            strcpy(info.filename, "");
        } else if(strncmp(command, "download ", 9) == 0) {
            strcpy(info.command, "download");
            char *pch;
            pch = strtok(command+9," >");
            strcpy(info.filename, pch);
            pch = strtok(NULL," >");
            if(pch != NULL) {
                int fd = open(pch, O_CREAT | O_WRONLY, S_IRUSR | S_IRGRP);
                if (fd < 0) {
                    fprintf(stderr, "***ERROR: Unable to open file %s for writing. Please enter a valid file name.\n\n", pch);
                    continue;
                }
                OUTPUT_FD = fd;
            }
        } else if(strcmp("quit", command) == 0)
            break;
        else 
            continue;


sendagain:
        if(!shouldDrop(1, 0)) {
            debug_i("writing first packet now..\n");
            n = write(sockfd, &info, sizeof(info));
            if (n<0) {
                perror("ERROR while writing data..!!\n");
                exit(1);
            }
        }

        declare_msg_hdr();

        selectTime.tv_sec = 3;
        selectTime.tv_usec = 0;

        dg_recv_send(sockfd);
        if(timeout) {
            debug_i("timeout first pkt\n");
            if(timeout_counter < TIMEOUTLIMIT) {
                timeout_counter++;
                timeout = 0;
                goto sendagain;	
            } else {
                fprintf(stderr, "***ERROR: Timeout Limit reached..!! Exiting now..!! \n");
                return -1;
            }
        }

        while(!finish || lastSeq <= headSeq) {
            timeout = 0;
            selectTime.tv_sec = 40;
            selectTime.tv_usec = 0;
            dg_recv_send(sockfd);
            if (timeout) {
                debug_i("timeout while recieving data..!!\n");
                finish = 1;
                finishConsumer = 1;
                break;
            }
        }

        if(!timeout) {
            debug_i("client going in timewait state..!!\n");
            selectTime.tv_sec = 6;
            selectTime.tv_usec = 0;
            dg_recv_send(sockfd);
        }
        pthread_join(consumer, NULL);
    } while(0);

    printf("For other commands Please start the client again\n");
    destroyLock();

    close(sockfd);
    exit(0);
}
