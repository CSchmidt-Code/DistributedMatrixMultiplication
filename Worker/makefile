## Worker

# Compiler:
CC=g++


LIBS=-lthrift -pthread -lmosquitto
FLAGS=

# Files to generate:
GENSRC=gen-cpp/RPCSendMatrixTasks.cpp gen-cpp/RPCSendMatrixTasks_types.cpp


all: worker.o

worker.o: worker.cpp
	$(CC) $(FLAGS) $(GENSRC) worker.cpp -o worker.o $(LIBS)

run:
	./worker.o