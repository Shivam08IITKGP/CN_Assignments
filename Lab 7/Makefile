all:
	gcc -o cldp_server cldp_server.c
	gcc -o cldp_client cldp_client.c
clean:
	rm -f cldp_server cldp_client
run_server:
	sudo ./cldp_server
run_client_discover:
	sudo ./cldp_client discover
run_client_query:
	sudo ./cldp_client query hostname
run_client_time:
	sudo ./cldp_client query systime
run_client_cpuload:
	sudo ./cldp_client query cpuload
run_client_all:
	sudo ./cldp_client query all
