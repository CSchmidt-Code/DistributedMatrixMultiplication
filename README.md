# DistributedMatrixMultiplication
Distributed system of docker containers, that uses several workers supervised by a controller to multiply matrices.

# Author: Christoph Schmidt

1. make all - generates docker images and thrift files
2. docker-compose up - starts controller, database, all the workers and the MQTT broker

# Settings:
All settings like changing the number of workers can be done in the hidden .env file:
NUMBERWORKERS: Number of workers
MATRIX_A_ROWS: Number of rows of matrix A
MATRIX_A_COLS: Number of columns of matrix A (and number of rows of matrix B)
MATRIX_B_COLS: Number of columns of matrix B
NUMBER_MATRICES: Number of matrix pairs to be multiplied by the workers
MAX_CHUNK_SIZE: The maximum size of matrix chunks that get send to a worker for a single task




# Documentation:
The controller creates one or multiple pairs of matrices to be multiplied by multiple workers.
At the beginning the controller divides these matrices into tasks, that get later executed by the workers.

The controller further does a healthcheck via UDP to make sure all workers are working properly.
Thrift RPC is used to send tasks from the controller to the workers.
Once a worker has finished its calculation it waits for the controller to instruct it via another 
remote procedure call to write the results to the database via HTTP.
Before a worker is finally able to write its data to the database,
it sends a request via MQTT to the other workers and writes to the 
database once all other workers replied. (Ricart-Agrawala algorithm of mutual exclusion)

To see the results of the calculation a HTTP GET message can be send to the database, the target being 
the number of the result matrix to be addressed:
localhost:8081/0 - Requests result matrix 0
