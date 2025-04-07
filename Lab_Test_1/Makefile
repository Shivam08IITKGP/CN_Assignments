all:
	gcc -o server chatserver.c
	gcc -o client chatclient.c
run_server: all
	./server
run_client:
	./client
clean:
	rm -f server client