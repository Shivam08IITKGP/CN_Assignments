all:
	gcc -o server task_queue_server.c -pthread
	gcc -o client worker_client.c
run:
	./server tasks.txt
clean:
	rm -f server client