all: sender receiver

sender: sender.c
	cc -c sender.c -ggdb -pthread
	cc -o sender -ggdb -pthread sender.o

receiver: receiver.c
	cc -c receiver.c -ggdb -pthread
	cc -o receiver -ggdb -pthread receiver.o

clean:
	rm -f sender receiver
	rm -f *.o
