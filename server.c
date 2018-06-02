#include "lib/my_unpifi.h"
#include "lib/common.h"
#include "lib/sendmessages.h"
#include "lib/linkedlist.h"
#include <dirent.h>

typedef struct {
    int sockfd;
    struct in_addr *addr;
    struct in_addr *ntmaddr;
    struct in_addr subaddr;
} SockStruct;

typedef struct {
    int port;
    struct in_addr addr;
    int pfd;
    int pid;
} NodeData;

static SockStruct *currentserver;
static const uint32_t localaddr = (127<<24) | 1;
static int window, primaryfd;
static FILE *filefd;
static uint32_t bufsize;
static char *filebuf = NULL, *file_error = NULL;
static int portclosed = 0;
static char path[1024];

int writetowindow(char * buf, int len)
{
    if(!isswfull())
        insertmsg(buf, len);
    else
        return -1;
    return 0;
}

static void closepeerconnection(int sockfd)
{
    struct sockaddr_in addr;
    socklen_t len;
    len = sizeof(addr);
    bzero(&addr, len);
    addr.sin_family = AF_UNSPEC;
    connect(sockfd, (SA *)&addr, len);
}
//Returns 0 when no more data
int fillslidingwindow(int segments)
{
    static uint32_t offset = 0;
    static int hasmoredata = 1;
    static int readcount = 0;
    static int sendzerobytedata = 0;
    if(!portclosed)
    {
        portclosed = 1;
        setsecondaryfd(0);
        closepeerconnection(currentserver->sockfd);
        close(currentserver->sockfd);
        setprimaryfd(primaryfd);
        if(filefd == NULL)
        {
            writetowindow(file_error, strlen(file_error));
            return 0;
        }
    }
    int i;
    for(i = 0; i < segments; i++)
    {
        if(offset < readcount) {
            writetowindow(filebuf + offset, min(datalength, readcount - offset));
            offset += datalength;
        } 
        else if(sendzerobytedata) {
            writetowindow("", 0);
            hasmoredata = 0;
            return hasmoredata;
        }
        else if(hasmoredata)
        {
            offset = 0;
            readcount = read(fileno(filefd), filebuf, bufsize);
            if(readcount < bufsize && readcount%datalength == 0) {
                sendzerobytedata = 1;
            }
            else if(readcount < bufsize)
                hasmoredata = 0;
            i--;
        }
        else
            return 0;
    }
    return 1;

}

void listFiles() {
    DIR *d;
    struct dirent *dir;
    d = opendir(path);
    FILE *fp = fopen(".list", "w");
    if(d) {
        while ((dir = readdir(d)) != NULL){
            if(strcmp(dir->d_name, ".list") != 0)
                fprintf(fp, "%s\n", dir->d_name);
        }
        closedir(d);
    }
    fclose(fp);
}

void handleChild(struct sockaddr_in *caddr, struct connectioninfo *info, 
        SockStruct *server) {

    struct sockaddr_in servaddr;
    socklen_t len;
    int reuse = 1, n;
    char buf[6];
    uint32_t clientip, serverip, servernetmask, serversubnet;
    currentserver = server;
    len = sizeof(*caddr);
    printf(" Command recieved from client: %s\n", info->command);
    printf(" Filename recieved from client: %s\n", info->filename);
    printf(" Advertised window: %u\n", info->window);
    printf(" Client Address: %s\n", Sock_ntop((SA *) caddr, len));

    clientip = htonl(caddr->sin_addr.s_addr);
    serverip = htonl(server->addr->s_addr);
    servernetmask = htonl(server->ntmaddr->s_addr);
    serversubnet = htonl(server->subaddr.s_addr);

    primaryfd = Socket(AF_INET, SOCK_DGRAM, 0);

    if(serverip == localaddr || serversubnet == (clientip & servernetmask)) {
        printf(" Client is local.\n");
        setsockopt(primaryfd, SOL_SOCKET, SO_DONTROUTE, &reuse, sizeof(reuse));
        setsockopt(server->sockfd, SOL_SOCKET, SO_DONTROUTE, &reuse, sizeof(reuse));
    } else
        printf(" Client is not local\n");	
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(0);
    servaddr.sin_addr = *(server->addr);

    len = sizeof(servaddr);

    if(bind(primaryfd, (SA *) &servaddr, len) < 0)
    {
        perror("Not able to make local connection");
        close(server->sockfd);
        return;
    }

    connect(primaryfd, (SA *)caddr, sizeof(*caddr));
    connect(server->sockfd, (SA *)caddr, sizeof(*caddr));

    if (getpeername(primaryfd, (SA *) &caddr, &len) < 0)
    {
        perror("Error getting socket info for server.");
        goto end;
    }

    if (getsockname(primaryfd, (SA *) &servaddr, &len) < 0)
    {
        perror("Error getting socket info for client.");
        goto end;
    }
    printf(" Client Address: %s\n", Sock_ntop((SA *) &caddr, len));
    printf(" Server Address: %s\n", Sock_ntop((SA *) &servaddr, len));

    if(strcmp(info->command, "list") == 0) {
        listFiles();
        filefd = fopen(".list", "r");
    } else 
        filefd = fopen(info->filename, "r");
    if(filefd == NULL)
    {
        file_error = strerror(errno);
    }
    bufsize = min(datalength * window * 2, sysconf(_SC_PAGESIZE));
    bufsize = (bufsize/datalength)*datalength;
    filebuf = zalloc(bufsize);
    init_sender(window, server->sockfd, info->window);

    n = sprintf(buf, "%d", servaddr.sin_port);
    setsecondaryfd(primaryfd);
    writetowindow(buf, n + 1);
    dg_send(fillslidingwindow);
end:
    if(!portclosed)
    {
        closepeerconnection(server->sockfd);
        close(server->sockfd);
    }

    close(primaryfd);

}


int lst_find(lst_node *lst_head, NodeData *data) {
    NodeData *lst_data;
    if(lst_head == NULL)
        return 0;
    do {
        lst_data = (NodeData *)lst_head->data;
        if(lst_data->port == data->port && lst_data->addr.s_addr == data->addr.s_addr)
            return 1;
        lst_head = lst_head -> next;
    } while(lst_head != NULL);
    return 0;
}


int main(int argc, char **argv)
{
    int i=0, sockfd, maxfd=0, port=0, count=0;
    pid_t childpid;
    struct ifi_info *ifi, *ifihead;
    struct sockaddr_in *sa, ca;
    socklen_t clilen;
    int n, j;
    FILE *fp;
    SockStruct *array;
    struct in_addr subaddr;
    fd_set rset;
    lst_node *lst_head = NULL;
    NodeData *lst_data;
    struct connectioninfo info;

    bzero(&info, sizeof(info));

    fp = fopen("server.in", "r");
    if(fp == NULL)
    {
        perror("Error opening file - server.in");
        exit(1);
    }
    port = getint(fp);
    window = getint(fp);
    getstring(path, 1024, fp);
    fclose(fp);

    for (ifihead = ifi = Get_ifi_info(AF_INET, 1); ifi != NULL; ifi = ifi->ifi_next) {
        count++;	
    }

    printf("\n Server Port: %d, Sliding Window: %d\n", port, window);
    array = (SockStruct *) malloc(count * sizeof(SockStruct));

    for (ifihead = ifi = Get_ifi_info(AF_INET, 1), i=0; ifi != NULL; ifi = ifi->ifi_next, i++) {
        sockfd = Socket(AF_INET, SOCK_DGRAM, 0);
        sa = (struct sockaddr_in *) ifi->ifi_addr;
        sa->sin_family = AF_INET;
        sa->sin_port = htons(port);
        Bind(sockfd, (SA *) sa, sizeof(*sa));
        array[i].addr = &(sa->sin_addr);
        array[i].ntmaddr = &((struct sockaddr_in *) ifi->ifi_ntmaddr)->sin_addr;
        subaddr.s_addr = (*array[i].addr).s_addr & (*array[i].ntmaddr).s_addr;
        array[i].subaddr = subaddr;
        array[i].sockfd = sockfd;

        printf(" IP Address: %s, ", inet_ntoa(*(array[i].addr)));
        printf(" Network Mask: %s, ", inet_ntoa(*(array[i].ntmaddr)));
        printf(" Subnet Address: %s\n", inet_ntoa(array[i].subaddr));
    }

    for( ; ; ) {
        lst_node *lst_n = lst_head;
        FD_ZERO(&rset);
        while(lst_n != NULL)
        {
            lst_data = (NodeData *) lst_n->data;
            FD_SET(lst_data->pfd, &rset);
            maxfd = max(maxfd, lst_data->pfd);
            lst_n = lst_n->next;
        }
        for(i=0; i<count; i++) {
            FD_SET(array[i].sockfd, &rset);
            maxfd = max(maxfd, array[i].sockfd);
        }
        Select(maxfd+1, &rset, NULL, NULL, NULL);

        for(i=0; i < count; i++) {
            lst_n = lst_head;
            lst_node *prev = NULL;
            while(lst_n != NULL)
            {
                lst_data = (NodeData *) lst_n->data;
                if(FD_ISSET(lst_data->pfd, &rset)) {
                    int pid = lst_data->pid, status = 0;
                    close(lst_data->pfd);
                    if(prev)
                        prev->next = lst_n->next;
                    else
                        lst_head = lst_n->next;
                    free(lst_data);
                    free(lst_n);
                    waitpid(pid, &status, 0);
                    break;
                }
                prev = lst_n;
                lst_n = lst_n->next;
            }

            if(FD_ISSET(array[i].sockfd, &rset)) {
                clilen = sizeof(ca);
                n = recvfrom(array[i].sockfd, &info, sizeof(info), 0, (SA *)&ca, &clilen);
                if(n < 0)
                {
                    break;
                }

                lst_data = (NodeData *) malloc(sizeof(NodeData));
                lst_data->port = ca.sin_port;
                lst_data->addr = ca.sin_addr;

                if (!lst_find(lst_head, lst_data)) {
                    int *pfd = malloc(2 * sizeof(int));
                    n = pipe(pfd);
                    if(lst_head == NULL)
                        lst_head = lst_initiate(lst_data);
                    else
                        lst_insert(lst_head, lst_data);

                    lst_data->pfd = pfd[1];

                    if((childpid = fork()) == 0) {
                        close(pfd[1]);
                        for(j = 0; j<count; j++) {
                            if(j != i)
                                close(array[j].sockfd);
                        }
                        printf("Got a client Connection!!!\n");
                        handleChild(&ca, &info, &array[i]);
                        printf("Exiting child\n");
                        n = write(pfd[0], "a", 2);
                        close(pfd[0]);
                        free(pfd);
                        goto exitLabel;
                    } else
                    {
                        close(pfd[0]);
                        free(pfd);
                        lst_data->pid = childpid;
                    }
                } 
                else
                {
                    free(lst_data);
                }
            }
        }
    }
    printf("\nExiting..!!\n");
exitLabel:
    free_ifi_info(ifihead);
    free(array);
    exit(0);
}
