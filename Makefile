UNAME := $(shell uname)
ifeq ($(UNAME), SunOS)
	slib = /home/courses/cse533/Stevens/unpv13e_solaris2.10
	LIBS =  -lsocket -lpthread -lm -lnsl lib/my_lib.a ${slib}/libunp.a
else
	slib = ../unpv13e
	LIBS =  -lpthread -lm lib/my_lib.a ${slib}/libunp.a
	UBUNTUF=-DUBUNTU
endif

ifeq ($(NODEBUG), 1)
	 DEBUGFLAG=-DNDEBUGINFO
endif

ifeq ($(NOCONTENT), 1)
	 NCONTENT=-DNCONTENT
endif


CC = gcc


FLAGS =  -g -O2
CFLAGS = ${FLAGS} -I${slib}/lib

all: libmake client server

libmake: 
	${MAKE} -C lib DEBUGFLAG=$(DEBUGFLAG)

client: client.o lib/my_lib.a lib/common.h
	${CC} ${FLAGS} -o client client.o lib/my_lib.a ${LIBS}
client.o: client.c 
	${CC} ${CFLAGS} ${UBUNTUF} ${DEBUGFLAG} ${NCONTENT} -c client.c

server: server.o lib/my_lib.a lib/common.h
	${CC} ${FLAGS} -o server server.o lib/my_lib.a ${LIBS}
server.o: server.c 
	${CC} ${CFLAGS} ${UBUNTUF} ${DEBUGFLAG} -c server.c

clean:
	@rm client server *.o ||:
	${MAKE} -C lib clean 

