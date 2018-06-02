UNAME := $(shell uname)

ifeq ($(UNAME), SunOS)
	slib = /home/courses/cse533/Stevens/unpv13e_solaris2.10
	LIBS =  -lsocket -lnsl ${slib}/libunp.a
else
	slib = ../../unpv13e
	LIBS =  ${slib}/libunp.a
	UBUNTUF=-DUBUNTU
endif

CC = gcc

SRC=$(wildcard *.c)
OBJS=$(SRC:.c=.o)


FLAGS =  -g -O2
CFLAGS = ${FLAGS} -I${slib}/lib

all: ${OBJS} my_lib.a my_prifinfo

my_prifinfo: my_prifinfo.o my_get_ifi_info.o
	${CC} -o my_prifinfo my_prifinfo.o my_get_ifi_info.o ${LIBS}

my_lib.a: my_get_ifi_info.o my_rtt.o linkedlist.o sendmessages.o common.o
	ar r my_lib.a $^

%.o: %.c
	${CC} ${DEBUGFLAG} -c $^ $(CFLAGS)

clean:
	rm my_prifinfo *.o my_lib.a

