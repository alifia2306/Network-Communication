all: httpserver
httpserver: httpserver.c
	clang -g -lpthread -o httpserver httpserver.c