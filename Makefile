all: release

CCFLAGS=-Wall -Wextra


debug: CCFLAGS+=-Og -ggdb -Wpedantic
debug: clean sender receiver

release: CCFLAGS += -O3
release: sender receiver


sender: sender.c
	$(CC) ${CCFLAGS} -c sender.c -pthread
	$(CC) ${CCFLAGS} -o sender -pthread sender.o

receiver: receiver.c
	$(CC) ${CCFLAGS} -c receiver.c -pthread
	$(CC) ${CCFLAGS} -o receiver -pthread receiver.o


clean:
	rm -f sender receiver
	rm -f *.o
