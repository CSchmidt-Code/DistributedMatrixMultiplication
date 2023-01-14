#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <vector>
#include <chrono>
#include <map>
#include <queue>
#include <arpa/inet.h>
#include "gen-cpp/RPCSendMatrixTasks.h"
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>
#include "MatrixData.h"
#include <pthread.h>
#include <mutex>

using namespace std;
using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;

// DEBUGGING
#define PROGR_TIMEOUT 120000 // Milliseconds after which the program terminates
chrono::steady_clock::time_point program_start_time;

#define BUFLEN 4
#define UDP_PORT 10000
#define HEALTHCHECK_MAX_WAIT 100000   // useconds that controller waits for healthcheck response
#define HEALTHCHECK_PERIOD 1000       // Milliseconds
#define HEALTHCHECK_MSG_STATUS 0x80   // 0: waiting for task, 1: working (or healthcheck request from controller)
#define HEALTHCHECK_MSG_WORKERNR 0x7F // 7 Bit to transfer worker number during healthcheck
#define RPC_PORT 13000

char buf[BUFLEN];
int addr_len = sizeof(struct sockaddr_in);

// Controller data
int controller_socket, result = -1;
struct sockaddr_in controller_addr; // Address of controller

// Worker data
int number_workers;
vector<struct sockaddr_in> worker_addr;
#define WAITING_FOR_TASK 0
#define WORKING 1
#define BLOCKED 2
vector<int> worker_status; // 0: waiting for task, 1: working, 2: blocked
vector<pthread_mutex_t> mutex_worker_status;
vector<pthread_t> thread_control_worker;
pthread_mutex_t mutex_store_result = PTHREAD_MUTEX_INITIALIZER;

// Healthcheck data
chrono::steady_clock::time_point last_healthcheck_time;
int poll_count = 1;

// Measurement data
int max_rtt_hc = 0;
int rtt_sum_count_hc = 0;
int sum_rtt_hc = 0;
pthread_mutex_t rtt_mutex_rpc = PTHREAD_MUTEX_INITIALIZER;
int max_rtt_rpc = 0;
int rtt_sum_count_rpc = 0;
int sum_rtt_rpc = 0;
int blocked_workers = 0;
chrono::steady_clock::time_point time_calc_start;
int calculation_time_ms;

// Calculation data
MatrixData m_data;
long long a_rows;
long long a_cols;
long long b_cols;
int number_matrices;
long long max_chunk_size;
bool finished_calculation = false;
pthread_mutex_t mutex_mqtt_count = PTHREAD_MUTEX_INITIALIZER;
int mqtt_count = 0;

int init_udp()
{
    controller_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (controller_socket < 0)
    {
        perror("Controller failed to create UDP socket\n");
        return -1;
    }

    controller_addr.sin_family = AF_INET;
    controller_addr.sin_port = htons(UDP_PORT);
    controller_addr.sin_addr.s_addr = INADDR_ANY;

    result = ::bind(controller_socket, (struct sockaddr *)&controller_addr, sizeof(struct sockaddr_in));
    if (result < 0)
    {
        return -1;
    }

    return 0;
}

void writeToBuffer(void *input, int length)
{
    for (int i = 0; i < length; ++i)
    {
        buf[i] = *((char *)input + i);
    }
}

bool receiveMsgNonBlocking(struct sockaddr_in &addr, size_t bytelength)
{
    result = recvfrom(controller_socket, buf, bytelength, MSG_DONTWAIT, (struct sockaddr *)&addr, (socklen_t *)&addr_len);
    if (result < 0)
    {
        perror("Receiving failed");
        return false;
    }
    return true;
}

bool receiveMsgTimeout(struct sockaddr_in &addr, size_t bytelength)
{
    result = recvfrom(controller_socket, buf, bytelength, 0, (struct sockaddr *)&addr, (socklen_t *)&addr_len);
    if (result < 0)
    {
        perror("Receiving failed");
        return false;
    }
    return true;
}

bool receiveRegistrationMsg(int worker, size_t bytelength)
{
    result = recvfrom(controller_socket, buf, bytelength, 0, (struct sockaddr *)&worker_addr.at(worker), (socklen_t *)&addr_len);
    if (result < 0)
    {
        perror("Receiving failed");
        return false;
    }
    return true;
}

void sendMsg(int &worker, void *msg, size_t bytelength)
{
    writeToBuffer(msg, bytelength);
    result = sendto(controller_socket, buf, bytelength, 0, (struct sockaddr *)&worker_addr.at(worker), sizeof(struct sockaddr_in));
    if (result < 0)
    {
        perror("Sending failed");
    }
}

void setControllerRcvTimeout(int us)
{
    struct timeval sock_rcv_timeout;
    sock_rcv_timeout.tv_sec = 0;
    sock_rcv_timeout.tv_usec = us;
    setsockopt(controller_socket, SOL_SOCKET, SO_RCVTIMEO, &sock_rcv_timeout, addr_len);
}

// Reads in a environment variable specified by env_var
int readMatrixSize(const char* env_var)
{
    char *tmp = getenv(env_var);
    if (tmp == nullptr)
    {
        return 1;
    }

    int i = 0;
    int value = 0;
    while (*(tmp + i) != '\0')
    {
        if (*(tmp + i) < 48 || *(tmp + i) > 57)
        {
            return 1;
        }
        value *= 10;
        value += (*(tmp + i) - 48);
        ++i;
    }
    return value;
}

void readNumberWorkers()
{
    char *tmp = getenv("NUMBERWORKERS");
    if (tmp == nullptr)
    {
        cout << ("NUMBERWORKERS isn't set' - check docker-compose file!") << endl;
        number_workers = 1;
        return;
    }

    int i = 0;
    number_workers = 0;
    while (*(tmp + i) != '\0')
    {
        if (*(tmp + i) < 48 || *(tmp + i) > 57)
        {
            perror("NUMBERWORKERS must be a number - check docker-compose file!");
        }
        number_workers *= 10;
        number_workers += (*(tmp + i) - 48);
        ++i;
    }
    
    printf("Expecting %d workers\n", number_workers);
}

void readEnvironmentData()
{
    a_rows = readMatrixSize("MATRIX_A_ROWS");
    a_cols = readMatrixSize("MATRIX_A_COLS");
    b_cols = readMatrixSize("MATRIX_B_COLS");
    number_matrices = readMatrixSize("NUMBER_MATRICES");
    max_chunk_size = readMatrixSize("MAX_CHUNK_SIZE");

    readNumberWorkers();
}

void registerWorkers()
{
    for (int i = 0; i < number_workers; ++i)
    {
        struct sockaddr_in tmp;
        worker_addr.push_back(tmp);
        worker_status.push_back(0); // Intitial status: waiting for task
        pthread_mutex_t mutex_tmp = PTHREAD_MUTEX_INITIALIZER;
        mutex_worker_status.push_back(mutex_tmp);
        pthread_t thread_tmp;
        thread_control_worker.push_back(thread_tmp);
        receiveRegistrationMsg(i, 1); // Wait for every worker to send a registration message and therefore reveal their address
        sendMsg(i, &i, 4);            // Send 4 byte number of worker to corresponding worker
        string test = inet_ntoa(worker_addr.at(0).sin_addr);
        cout << "Registered worker " << i << " with IP " << inet_ntoa(worker_addr.at(i).sin_addr) << endl;
    }
}

void healthcheck()
{
    // cout << "Sending healthcheck request to all active workers" << endl;

    for (int worker = 0; worker < number_workers; ++worker) // Iterate all workers
    {
        if (worker_status.at(worker) != 2) // If worker is not blocked
        {
            // Healthcheck requests consist of a leading 1 and the number of the requested worker modulo 128 (7 Bit used to transfer worker number)
            char request = HEALTHCHECK_MSG_STATUS | (HEALTHCHECK_MSG_WORKERNR & (worker % HEALTHCHECK_MSG_WORKERNR));
            struct sockaddr_in rcv_addr;

            chrono::steady_clock::time_point rtt_start = chrono::steady_clock::now(); // Used to measure RTT
            sendMsg(worker, &request, 1);                                             // Send healthcheck request to worker

            bool store_rtt = true;
            while (1)
            {
                bool received = receiveMsgTimeout(rcv_addr, 1); // Try to receive 1 byte and store sender address in rcv_addr
                int round_trip_time = chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now() - rtt_start).count();
                if (!received) // Nothing was received during HEALTHCHECK_MAX_WAIT us
                {
                    pthread_mutex_lock(&(mutex_worker_status.at(worker)));
                    worker_status.at(worker) = 2;
                    ++blocked_workers;
                    pthread_mutex_unlock(&(mutex_worker_status.at(worker)));
                    cout << "Worker " << worker << " didn't respond to healthcheck and is now blocked" << endl;
                    break;
                }

                if ((buf[0] & HEALTHCHECK_MSG_WORKERNR) == (worker % 128)) // Healthcheck response is valid
                {
                    pthread_mutex_lock(&(mutex_worker_status.at(worker)));
                    worker_status.at(worker) = (buf[0] & HEALTHCHECK_MSG_STATUS) >> 7;
                    pthread_mutex_unlock(&(mutex_worker_status.at(worker)));
                    // cout << "Received valid " << poll_count << ". healthcheck response from worker " << worker << ": " << *(int *)buf << endl;
                    if (store_rtt)
                    {
                        sum_rtt_hc += round_trip_time;
                        ++rtt_sum_count_hc;
                        if (round_trip_time > max_rtt_hc)
                        {
                            max_rtt_hc = round_trip_time;
                        }
                    }
                    break;
                }
                store_rtt = false; // If the correct message wasn't received immediately RTT won't be correct
            }
        }
    }
    ++poll_count; // Count each healthcheck
}

void *controlWorker(void *args)
{
    int worker = *((int *)args);

    // Initialize RPC client
    shared_ptr<TSocket> socket(new TSocket(inet_ntoa(worker_addr.at(worker).sin_addr), RPC_PORT));
    shared_ptr<TTransport> transport(new TBufferedTransport(socket));
    shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
    RPCSendMatrixTasksClient rpc_client(protocol);
    transport->open();

    if (!rpc_client.isMQTTInitialized())
    {
        cout << "ERROR: RPC isMQTTinitialized returned false" << endl;
    }

    pthread_mutex_lock(&mutex_mqtt_count);
    ++mqtt_count;
    pthread_mutex_unlock(&mutex_mqtt_count);

    while (mqtt_count != number_workers); // Wait until all workers are ready

    rpc_client.MQTTReady(); // Tell our worker that MQTT was initialized by all workers

    while (1)
    {
        while (m_data.getNumberTaskFields() < 1)
        {
            // Wait until there are tasks again or m_data tells that all results are stored in the database
            if (m_data.allTasksFinished())
            {
                finished_calculation = true;
                rpc_client.calculationIsFinished(); // Tell worker that all tasks have been calculated
                transport->close();
                delete ((int *)args); // Worker number was allocated on the heap
                pthread_exit(nullptr);
            }
        }
        if (worker_status.at(worker) == BLOCKED)
        {
            transport->close();
            delete ((int *)args); // Worker number was allocated on the heap
            pthread_exit(nullptr);
        }

        RPCMatrixTask task;
        pthread_mutex_unlock(&rtt_mutex_rpc);
        if (!m_data.getNextTask(task, worker))
        {
            cout << "controlWorker(): Task of worker " << worker << " was stolen by other worker" << endl;
            continue; // Another worker was faster and got the last task in the queue
        }

        // cout << "Controller fetched task for result matrix " << task.result_matrix << ", row " << task.row << ", col " << task.col << endl;

        chrono::steady_clock::time_point rtt_start = chrono::steady_clock::now();
        bool rpc_result = rpc_client.calcTask(task);
        int rtt = chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now() - rtt_start).count();
        // cout << "RTT of RPC calcTask() call: " << rtt << " us" << endl;
        pthread_mutex_lock(&rtt_mutex_rpc);
        sum_rtt_rpc += rtt;
        ++rtt_sum_count_rpc;
        if (rtt > max_rtt_rpc)
        {
            max_rtt_rpc = rtt;
        }
        pthread_mutex_unlock(&rtt_mutex_rpc);
        if (!rpc_result)
        {
            // AssignTask returned false, so the task has to be reassigned
            cout << "RPC assignTask() returned false" << endl;
            m_data.reassignTask(task);
        }
        else // Worker finished calculation
        {
            if (worker_status.at(worker) == BLOCKED)
            {
                // Reassign task and exit thread (worker is blocked)
                m_data.reassignTask(task);
                transport->close();
                delete ((int *)args); // Worker number was allocated on the heap
                pthread_exit(nullptr);
            }
            // cout << "RPC calcResult() returned true" << endl;
            pthread_mutex_lock(&mutex_store_result);

            rtt_start = chrono::steady_clock::now();
            rpc_result = rpc_client.sendTaskResult(m_data.lastChunk(task));
            rtt = chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now() - rtt_start).count();
            // cout << "RTT of RPC sendTaskResult() call: " << rtt << " us" << endl;
            pthread_mutex_lock(&rtt_mutex_rpc);
            sum_rtt_rpc += rtt;
            ++rtt_sum_count_rpc;
            if (rtt > max_rtt_rpc)
            {
                max_rtt_rpc = rtt;
            }
            pthread_mutex_unlock(&rtt_mutex_rpc);
            if (rpc_result)
            {
                // Worker stored its result in database
                m_data.informAboutStoredResult(task);
            }
            else
            {
                cout << "RPC sendTaskResult returned false - Blocking worker " << worker << endl;
                m_data.reassignTask(task);
                pthread_mutex_lock(&(mutex_worker_status.at(worker)));
                worker_status.at(worker) = BLOCKED;
                ++blocked_workers;
                pthread_mutex_unlock(&(mutex_worker_status.at(worker)));
            }
            pthread_mutex_unlock(&mutex_store_result);
        }
    }

    transport->close();
    delete ((int *)args); // Worker number was allocated on the heap
    pthread_exit(nullptr);
}

int main()
{
    chrono::steady_clock::time_point program_start_time = chrono::steady_clock::now();
    cout << "Controller is running" << endl;
    if (init_udp() < 0)
    {
        perror("bind() failed");
        return -1;
    }
    readEnvironmentData(); // Reads number of workers from docker-compose file
    registerWorkers();     // Establishes communication with every worker

    m_data.initialize(number_workers, a_rows, a_cols, b_cols, number_matrices, max_chunk_size);

    time_calc_start = chrono::steady_clock::now(); // Start measuring the calculation time

    // Create threads to feed each worker
    for (int i = 0; i < number_workers; ++i)
    {
        int *worker = new int(i);
        pthread_create(&(thread_control_worker.at(i)), nullptr, controlWorker, (void *)worker);
    }

    last_healthcheck_time = chrono::steady_clock::now(); // Initialize time measurement for healthcheck
    setControllerRcvTimeout(HEALTHCHECK_MAX_WAIT);

    while (1) // Program loop
    {
        if (chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - program_start_time).count() > PROGR_TIMEOUT)
        {
            cout << "Controller timed out" << endl;
            break;
        }

        if (finished_calculation)
        {
            calculation_time_ms = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - time_calc_start).count();
            cout << endl
                 << "All tasks have been computed and the results have been stored in the database" << endl;
            break;
        }

        // Check if it's time for the next healthcheck
        chrono::steady_clock::time_point now = chrono::steady_clock::now();
        auto elapsed_ms = chrono::duration_cast<chrono::milliseconds>(now - last_healthcheck_time).count();
        if (elapsed_ms > HEALTHCHECK_PERIOD) // Time for next healthcheck
        {
            healthcheck();
            last_healthcheck_time = chrono::steady_clock::now(); // Save time of current healthcheck
        }
    }

    // Output results
    cout << endl
         << "---------------------------------------------------------" << endl;
    cout << "Controller:" << endl;
    cout << "Total calculation took " << calculation_time_ms << " ms." << endl
         << endl;
    cout << "Maximum of measured healthcheck RTT: " << max_rtt_hc << " us." << endl;
    cout << "Mean of healthcheck RTT values: " << sum_rtt_hc / (rtt_sum_count_hc > 0 ? rtt_sum_count_hc : 1) << " us." << endl;
    cout << blocked_workers << " workers were blocked during execution." << endl
         << endl;
    cout << endl
         << "Maximum of measured RPC RTT: " << max_rtt_rpc << " us." << endl;
    cout << "Mean of RPC RTT values: " << sum_rtt_rpc / (rtt_sum_count_rpc > 0 ? rtt_sum_count_rpc : 1) << " us." << endl;
    cout << "---------------------------------------------------------" << endl
         << endl;

    cout << endl << "The containers of database and MQTT broker have to be stopped manually." << endl;
    cout << "Wait for the workers to output their results." << endl;

    for (int i = 0; i < number_workers; ++i)
    {
        pthread_join(thread_control_worker.at(i), nullptr);
    }
    close(controller_socket);
    return 0;
}