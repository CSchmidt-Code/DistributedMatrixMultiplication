# Compiler:
CC=g++


LIBS=-lthrift
FLAGS=-std=c++20

# Path of homebrew:
FLAGS+=-I /usr/local/include/ -L/usr/local/lib

# Files to generate:
GENSRC=gen-cpp/RPCSendMatrixTasks.cpp gen-cpp/RPCSendMatrixTasks_types.cpp




CONTROLLERSRC=./Controller/Dockerfile
WORKERSRC=./Worker/Dockerfile
DATABASESRC=./Database/
THRIFTSRC=./thrift_install_image/
MQTTSRC=./MQTT_broker/

FIXDOCKER=DOCKER_BUILDKIT=0
#FIXDOCKER= 


controller: ./Controller/controller.cpp $(CONTROLLERSRC)
	sudo $(FIXDOCKER) docker build -f $(CONTROLLERSRC) . -t vs_p_controller:1.0.0

worker: ./Worker/worker.cpp $(WORKERSRC)
	sudo $(FIXDOCKER) docker build -f $(WORKERSRC) . -t vs_p_worker:1.0.0

database: ./Database/database.cpp $(DATABASESRC)
	sudo $(FIXDOCKER) docker build $(DATABASESRC) -t vs_p_database:1.0.0

thrift: ./RPC/RPCSendMatrixTasks.thrift
	thrift --gen cpp ./RPC/RPCSendMatrixTasks.thrift

thrift_image: $(THRIFTSRC)
	sudo $(FIXDOCKER) docker build $(THRIFTSRC) -t thrift_install:1

mqtt: $(MQTTSRC)
	sudo $(FIXDOCKER) docker build $(MQTTSRC) -t mqtt_broker:1

docker: controller worker database
	
all: thrift_image thrift mqtt controller worker database



