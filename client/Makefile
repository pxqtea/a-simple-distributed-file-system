LDFLAGS=-lgrpc++_unsecure -lgrpc -lgpr -lprotobuf -lpthread -ldl -lfuse -pthread
PROTOS_PATH = ../proto
GRPC_CPP_PLUGIN = grpc_cpp_plugin
PROTOC = protoc
GRPC_CPP_PLUGIN_PATH ?= `which $(GRPC_CPP_PLUGIN)`
FLAGS=-D_FILE_OFFSET_BITS=64

vpath fs.proto $(PROTOS_PATH)

client: fs.grpc.pb.o fs.pb.o main.o
	g++ main.o fs.grpc.pb.o fs.pb.o $(LDFLAGS) -std=c++11 -o client

main.o: main.cc
	g++ -c -g $(FLAGS) -std=c++11 main.cc 

fs.grpc.pb.o: fs.grpc.pb.cc fs.pb.o
	g++ -c -g $(FLAGS) -std=c++11 fs.grpc.pb.cc

fs.pb.o: fs.pb.cc
	g++ -c -g $(FLAGS) -std=c++11 fs.pb.cc

fs.grpc.pb.cc: fs.proto
	$(PROTOC) -I $(PROTOS_PATH) --grpc_out=. --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN_PATH) $<

fs.pb.cc: fs.proto
	$(PROTOC) -I $(PROTOS_PATH) --cpp_out=. $<

clean:
	rm client *.o
