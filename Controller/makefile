## Controller

# Compiler:
CC=g++


LIBS=-lthrift -pthread
FLAGS=

# Files to generate:
GENSRC=gen-cpp/RPCSendMatrixTasks.cpp gen-cpp/RPCSendMatrixTasks_types.cpp


all: controller.o

controller.o: controller.cpp
	$(CC) $(FLAGS) $(GENSRC) controller.cpp -o controller.o $(LIBS)
	#g++ gen-cpp/RPCSendMatrixTasks.cpp gen-cpp/RPCSendMatrixTasks_types.cpp controller.cpp -o controller.o -lthrift


run:
	./controller.o