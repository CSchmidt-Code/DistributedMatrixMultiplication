#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>
#include <chrono>
#include <pthread.h>
#include <semaphore.h>
#include <string>
#include <queue>
#include <arpa/inet.h>
#include <stack>
#include <mosquitto.h>
#include "RPCImplementation.h"

using namespace std;

// DEBUGGING
#define PROGR_TIMEOUT 120000 // Milliseconds after which the program terminates
//#define DEBUG_LAMPORT       // If defined lamport debug messages will be outputted
//#define DEBUG_MUTEX         // If defined mutex debug messages will be outputted
chrono::steady_clock::time_point program_start_time;

#define BUFLEN 4
#define UDP_PORT 10000
#define HEALTHCHECK_MSG_STATUS 0x80   // 0: waiting for task, 1: working (or healthcheck request from controller)
#define HEALTHCHECK_MSG_WORKERNR 0x7F // 7 Bit to transfer worker number during healthcheck
#define TCP_PORT 80
#define TCP_BUFLEN 1024
#define RPC_PORT 13000

bool finished_calculation = false;
int count_calculations = 0;

// UDP data
char buf[BUFLEN];
int addr_len = sizeof(struct sockaddr_in);
int worker_socket, result = -1;
int worker_nr = -1;
char worker_status = 0;             // 0: waiting for task, 1: working
struct sockaddr_in controller_addr; // Address of controller

// healthcheck data
char healthcheckRequestMsg;

// TCP data
int sock_TCP;
struct sockaddr_in cli_addr, database_addr;
char *buf_TCP = new char[TCP_BUFLEN]();

// MQTT data
sem_t sem_mqtt;
sem_t sem_mqtt_ready;
struct mosquitto *client;
typedef char byte;
#define REPLY 0
#define REQUEST 1
struct MQTT_Payload
{
    char message_type;
    int lamport_time;
    int worker_number;
};
int number_workers;
int lamport_time = 0;
bool mqtt_request_open = false;
pthread_mutex_t mutex_request_lamport_time = PTHREAD_MUTEX_INITIALIZER;
int request_lamport_time = -1;
pthread_mutex_t mutex_lamport_time = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_mqtt_request_open = PTHREAD_MUTEX_INITIALIZER;
sem_t sem_write_database;
int received_replies = 0;
pthread_mutex_t mutex_received_replies = PTHREAD_MUTEX_INITIALIZER;

// Received MQTT requests from other workers
queue<int> req_worker_numbers;
queue<int> req_lamport_times;
pthread_mutex_t mutex_request_queue = PTHREAD_MUTEX_INITIALIZER;
int received_lamport_time;
char received_message_type;
int received_worker_number;
chrono::steady_clock::time_point request_time;
chrono::steady_clock::time_point all_replies_time;
int sum_mqtt_request_times = 0;
int min_mqtt_request_times = -1;
int max_mqtt_request_times = 0;
int count_mqtt_requests = 0;

// HTTP RTT measurement
int max_rtt_hc = 0;
int sum_rtt_hc = 0;
int rtt_count = 0;

// RPC data (has to be initialized before worker registers at controller)
::std::shared_ptr<RPCSendMatrixTasksHandler> handler(new RPCSendMatrixTasksHandler());
::std::shared_ptr<TProcessor> processor(new RPCSendMatrixTasksProcessor(handler));
::std::shared_ptr<TServerTransport> serverTransport(new TServerSocket(RPC_PORT));
::std::shared_ptr<TTransportFactory> transportFactory(new TBufferedTransportFactory());
::std::shared_ptr<TProtocolFactory> protocolFactory(new TBinaryProtocolFactory());

int init_udp()
{
    worker_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (worker_socket < 0)
    {
        perror("Worker failed to create UDP socket\n");
        return -1;
    }

    controller_addr.sin_family = AF_INET;
    controller_addr.sin_port = htons(UDP_PORT);
    controller_addr.sin_addr.s_addr = inet_addr("173.20.0.6");
    return 0;
}

void init_tcp()
{
    // Open TCP socket
    if ((sock_TCP = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Worker: can't open stream socket");
    }

    database_addr.sin_family = AF_INET;
    database_addr.sin_port = htons(TCP_PORT);
    database_addr.sin_addr.s_addr = inet_addr("173.30.0.8");
}

void printTCPBuf(int length = TCP_BUFLEN)
{
    for (int i = 0; i < length; ++i)
    {
        if (buf_TCP[i] == '\r')
        {
            cout << string{R"(\r)"};
        }
        else if (buf_TCP[i] == '\n')
        {
            cout << string{R"(\n)"} << endl;
        }
        else if (buf_TCP[i] == '\0')
        {
            // cout << string{R"(\0)"};
        }
        else
        {
            cout << buf_TCP[i];
        }
    }
}

bool byteAddressInTCPBuf(char *address)
{
    if (address >= &buf_TCP[0] && (address - &buf_TCP[0]) < TCP_BUFLEN)
    {
        return true;
    }
    return false;
}

void setTCPSockTimeoutSeconds(int sec)
{
    struct timeval sock_rcv_timeout;
    sock_rcv_timeout.tv_sec = sec;
    sock_rcv_timeout.tv_usec = 0;
    setsockopt(sock_TCP, SOL_SOCKET, SO_RCVTIMEO, &sock_rcv_timeout, sizeof(sockaddr_in));
}

void setUDPSockTimeoutSeconds(int sec)
{
    struct timeval sock_rcv_timeout;
    sock_rcv_timeout.tv_sec = sec;
    sock_rcv_timeout.tv_usec = 0;
    setsockopt(worker_socket, SOL_SOCKET, SO_RCVTIMEO, &sock_rcv_timeout, sizeof(sockaddr_in));
}

void writeToBuffer(void *input, int length)
{
    for (int i = 0; i < length; ++i)
    {
        buf[i] = *((char *)input + i);
    }
}

void sendMsg(void *msg, size_t bytelength)
{
    writeToBuffer(msg, bytelength);
    result = sendto(worker_socket, buf, bytelength, 0, (struct sockaddr *)&controller_addr, sizeof(struct sockaddr_in));
    if (result < 0)
    {
        perror("Sending failed");
    }
}

bool receiveMsg(size_t bytelength)
{
    result = recvfrom(worker_socket, buf, bytelength, 0, NULL, NULL); // Not interested in source address
    if (result < 0)
    {
        perror("Receiving failed");
        return false;
    }
    return true;
}

void registerAtController()
{
    char reg_msg = 0xFF;
    sendMsg(&reg_msg, 1);
    receiveMsg(4); // Expect 4 byte number of this worker
    worker_nr = *(int *)buf;
    healthcheckRequestMsg = HEALTHCHECK_MSG_STATUS | (HEALTHCHECK_MSG_WORKERNR & (worker_nr % HEALTHCHECK_MSG_WORKERNR)); // Used to validate healthcheck requests
    cout << "Registered as worker " << worker_nr << endl;
}

void readNumberWorkers()
{
    char *tmp = getenv("NUMBERWORKERS");
    if (tmp == nullptr)
    {
        cout << ("NUMBERWORKERS isn't set - check docker-compose file!") << endl;
        number_workers = 1;
    }
    else
    {
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
    }
}

string numberToString(int number)
{
    if (number == 0)
    {
        return "0";
    }

    string str_number = "";

    if (number < 0)
    {
        str_number += "-";
        number *= -1;
    }

    stack<char> reverse_number;
    while (number > 0)
    {
        reverse_number.push((number % 10) + '0');
        number /= 10;
    }
    while (reverse_number.size() > 0)
    {
        str_number.push_back(reverse_number.top());
        reverse_number.pop();
    }
    return str_number;
}

string numberToString(long long number)
{
    if (number == 0)
    {
        return "0";
    }

    string str_number = "";

    if (number < 0)
    {
        str_number += "-";
        number *= -1;
    }

    stack<char> reverse_number;
    while (number > 0)
    {
        reverse_number.push((number % 10) + '0');
        number /= 10;
    }
    while (reverse_number.size() > 0)
    {
        str_number.push_back(reverse_number.top());
        reverse_number.pop();
    }
    return str_number;
}

// Compares input with cmp_str with length and returns true if they are equal
bool strCmpLength(char *input, string cmp_str, int length)
{
    if (length == 0)
    {
        return true;
    }

    for (int i = 0; i < length; ++i)
    {
        if (!byteAddressInTCPBuf(input + i) || *(input + i) != cmp_str[i]) // Ran out of tcp buffer or found chars that didn't match
        {
            return false;
        }
    }
    return true;
}

void *rpc_serve(void *args)
{
    TSimpleServer server(processor, serverTransport, transportFactory, protocolFactory);
    server.serve();
    return nullptr;
}

void mqtt_reply(int recv_worker_nr)
{
    __cxx11::basic_string<char> topic = "REPLY/" + numberToString(recv_worker_nr);
    MQTT_Payload payload_struct;
    payload_struct.message_type = REPLY;
    payload_struct.worker_number = worker_nr;

#ifdef DEBUG_MUTEX
    cout << "0: lock mutex_lamport_time" << endl;
#endif
    pthread_mutex_lock(&mutex_lamport_time);
#ifdef DEBUG_MUTEX
    cout << "0: locked mutex_lamport_time" << endl;
#endif
    ++lamport_time;
    payload_struct.lamport_time = lamport_time;
    pthread_mutex_unlock(&mutex_lamport_time);
#ifdef DEBUG_MUTEX
    cout << "0: unlocked mutex_lamport_time" << endl;
#endif

    void *payload = &payload_struct;
#ifdef DEBUG_LAMPORT
    cout << "Sending MQTT REPLY with lamport time " << payload_struct.lamport_time << endl;
#endif
    if (mosquitto_publish(client, nullptr, topic.c_str(), sizeof(MQTT_Payload), payload, 0, false))
    {
        perror("Unable to publish.\n");
        return;
    }
}

void mqtt_respond_to_open_requests()
{
    int w_nr;

#ifdef DEBUG_MUTEX
    cout << "1: lock mutex_mqtt_request_open" << endl;
#endif
    pthread_mutex_lock(&mutex_mqtt_request_open);
#ifdef DEBUG_MUTEX
    cout << "1: locked mutex_mqtt_request_open" << endl;
    cout << "2: lock mutex_request_queue" << endl;
#endif
    pthread_mutex_lock(&mutex_request_queue);
#ifdef DEBUG_MUTEX
    cout << "2: locked mutex_request_queue" << endl;
#endif
    while (req_worker_numbers.size() > 0)
    {
        w_nr = req_worker_numbers.front();
        req_worker_numbers.pop();
        req_lamport_times.pop();
        pthread_mutex_unlock(&mutex_request_queue);
#ifdef DEBUG_MUTEX
        cout << "2: unlocked mutex_request_queue" << endl;
#endif
#ifdef DEBUG_LAMPORT
        cout << "Replying to an request, received during the time of an own request:" << endl;
#endif
        mqtt_reply(w_nr);
#ifdef DEBUG_MUTEX
        cout << "3: lock mutex_request_queue" << endl;
#endif
        pthread_mutex_lock(&mutex_request_queue);
#ifdef DEBUG_MUTEX
        cout << "3: locked mutex_request_queue" << endl;
#endif
    }
    pthread_mutex_unlock(&mutex_request_queue);
#ifdef DEBUG_MUTEX
    cout << "2: unlocked mutex_request_queue" << endl;
#endif

    mqtt_request_open = false; // The request is now finished
    pthread_mutex_unlock(&mutex_mqtt_request_open);
#ifdef DEBUG_MUTEX
    cout << "1: unlocked mutex_mqtt_request_open" << endl;
#endif
}

void mqtt_request()
{
    if (number_workers > 1) // lamport algorithm is skipped when only one worker is registered
    {
        // Publish MQTT request
        MQTT_Payload payload_struct;
        payload_struct.message_type = REQUEST;
        payload_struct.worker_number = worker_nr;
        __cxx11::basic_string<char> topic = "REQUEST/" + numberToString(worker_nr);
        void *payload = &payload_struct;

#ifdef DEBUG_MUTEX
        cout << "4: lock mutex_mqtt_request_open" << endl;
#endif
        pthread_mutex_lock(&mutex_mqtt_request_open);
#ifdef DEBUG_MUTEX
        cout << "4: locked mutex_mqtt_request_open" << endl;
        cout << "5: lock mutex_lamport_time" << endl;
#endif
        pthread_mutex_lock(&mutex_lamport_time);
#ifdef DEBUG_MUTEX
        cout << "5: locked mutex_lamport_time" << endl;
#endif
        ++lamport_time;
        payload_struct.lamport_time = lamport_time;

#ifdef DEBUG_MUTEX
        cout << "6: lock mutex_request_lamport_time" << endl;
#endif
        pthread_mutex_lock(&mutex_request_lamport_time);
#ifdef DEBUG_MUTEX
        cout << "6: locked mutex_request_lamport_time" << endl;
#endif
        request_lamport_time = lamport_time; // Store the lamport time of the current request
        pthread_mutex_unlock(&mutex_request_lamport_time);
#ifdef DEBUG_MUTEX
        cout << "6: unlocked mutex_request_lamport_time" << endl;
#endif

        pthread_mutex_unlock(&mutex_lamport_time);
#ifdef DEBUG_MUTEX
        cout << "5: unlocked mutex_lamport_time" << endl;
#endif

        mqtt_request_open = true; // Mark the request as open
        pthread_mutex_unlock(&mutex_mqtt_request_open);
#ifdef DEBUG_MUTEX
        cout << "4: unlocked mutex_mqtt_request_open" << endl;
#endif

#ifdef DEBUG_LAMPORT
        cout << "Sending MQTT REQUEST with lamport time " << payload_struct.lamport_time << endl;
#endif
        request_time = chrono::steady_clock::now();
        if (mosquitto_publish(client, nullptr, topic.c_str(), sizeof(MQTT_Payload), payload, 0, false))
        {
            perror("Unable to publish.\n");
        }

        // Wait until we are allowed to send to database, then continue
        sem_wait(&sem_write_database);
    }
}

bool writeToDatabase(httpTask task)
{
    // cout << "Call of writeToDatabase()" << endl;

    sem_wait(&sem_mqtt_ready);
    sem_post(&sem_mqtt_ready);

    mqtt_request();

    // HTTP request
    init_tcp();
    if (connect(sock_TCP, (sockaddr *)&database_addr, sizeof(struct sockaddr_in)) > 0)
    {
        perror("Worker: TCP connection failed");
    }

    string msg = "POST /" + numberToString(task.result_matrix) + " HTTP/1.1\r\nRow: " + numberToString(task.row) + "\r\nColumn: " + numberToString(task.col) + "\r\nLast-Chunk: " + (task.last_chunk ? "TRUE" : "FALSE") + "\r\nContent-Length: ";
    string result = numberToString(task.result);
    msg.append(numberToString((long long)(result.length())) + "\r\n\r\n\r\n" + result);

    for (int i = 0; i < msg.length(); ++i) // Fill buffer with POST message
    {
        *(buf_TCP + i) = msg[i];
    }

    chrono::steady_clock::time_point rtt_start = chrono::steady_clock::now();
    int send_result = sendto(sock_TCP, buf_TCP, msg.length(), 0, (struct sockaddr *)&database_addr, sizeof(struct sockaddr_in));
    if (send_result < 0)
    {
        perror("Sending failed");
    }

    while (true) // Send until HTTP OK message was received
    {
        // Wait for the response
        if (recv(sock_TCP, buf_TCP, 19, 0))
        {
            // cout << "Still waiting for HTTP response" << endl;
            continue;
        }

        int round_trip_time = chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now() - rtt_start).count();
        if (round_trip_time > max_rtt_hc)
        {
            max_rtt_hc = round_trip_time;
        }
        sum_rtt_hc += round_trip_time;
        ++rtt_count;

        if (strCmpLength(buf_TCP, "HTTP/1.1 200 OK\r\n\r\n", 19))
        {
            // cout << "Received valid HTTP response" << endl;
            // cout << "RTT of http request: " << round_trip_time << " us" << endl;
            ++count_calculations;
            break;
        }
        else // send again
        {
            printTCPBuf(19);
            // cout << "Received invalid HTTP response" << endl;
            // cout << "RTT of http request: " << round_trip_time << " us" << endl;
            close(sock_TCP);
            return false;
        }
    }

    close(sock_TCP);

    // If requests were collected since the own request was sent, these requests get answered now
    mqtt_respond_to_open_requests();

    return true;
}

void *healthcheck(void *args)
{
    setUDPSockTimeoutSeconds(PROGR_TIMEOUT / 1000); // Set udp socket timeout to PROGR_TIMEOUT in seconds to make timeout work

    while (true)
    {
        if (chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - program_start_time).count() > PROGR_TIMEOUT)
        {
            break;
        }

        if (receiveMsg(1)) // See if there was a healthcheckrequest
        {
            if (buf[0] != healthcheckRequestMsg) // Healthcheck request was not valid
            {
                perror("Received invalid healthcheck request");
            }
            else
            {
                char healthcheck_response = (HEALTHCHECK_MSG_STATUS & (worker_status << 7)) | (HEALTHCHECK_MSG_WORKERNR & (worker_nr % 128));
                sendMsg(&healthcheck_response, 1);
            }
        }
    }

    close(worker_socket);
    return nullptr;
}

// Callback function for incoming messages
void onMQTTMessage(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *msg)
{
    /*
    MQTT Message:
    8 Bit Message Type (0: REPLY, 1: REQUEST)
    4 Byte Lamport Time
    4 Byte Worker Number
    */

    MQTT_Payload mqtt_msg = *(MQTT_Payload *)(msg->payload);
    received_lamport_time = mqtt_msg.lamport_time;
    received_message_type = mqtt_msg.message_type;
    received_worker_number = mqtt_msg.worker_number;

#ifdef DEBUG_MUTEX
    cout << "7: lock mutex_lamport_time" << endl;
#endif
    pthread_mutex_lock(&mutex_lamport_time);
#ifdef DEBUG_MUTEX
    cout << "7: locked mutex_lamport_time" << endl;
#endif
    lamport_time = (lamport_time >= received_lamport_time) ? (lamport_time + 1) : (received_lamport_time + 1);
    int debug_lamport = lamport_time;
    pthread_mutex_unlock(&mutex_lamport_time);
#ifdef DEBUG_MUTEX
    cout << "7: unlocked mutex_lamport_time" << endl;
#endif

    if (received_message_type == REPLY) // Received a REPLY
    {
#ifdef DEBUG_LAMPORT
        cout << "Received MQTT REPLY - From worker: " << received_worker_number << ", Lamport: " << received_lamport_time << endl;
#endif
#ifdef DEBUG_MUTEX
        cout << "8: lock mutex_received_replies" << endl;
#endif
        pthread_mutex_lock(&mutex_received_replies);
#ifdef DEBUG_MUTEX
        cout << "8: locked mutex_received_replies" << endl;
#endif
        ++received_replies;                         // Count the reply
        if (received_replies == number_workers - 1) // All workers responded to our request
        {
            all_replies_time = chrono::steady_clock::now();
            int us_duration = chrono::duration_cast<chrono::microseconds>(all_replies_time - request_time).count();
#ifdef DEBUG_LAMPORT
            cout << "Workers took " << us_duration << " us to respond to our request" << endl;
#endif
            ++count_mqtt_requests;
            sum_mqtt_request_times += us_duration;
            max_mqtt_request_times = (max_mqtt_request_times < us_duration) ? us_duration : max_mqtt_request_times;
            min_mqtt_request_times = (min_mqtt_request_times > us_duration || min_mqtt_request_times == -1) ? us_duration : min_mqtt_request_times;
            sem_post(&sem_write_database); // Now this worker is allowed to write to the database
            received_replies = 0;          // Reset the counter for the next request
        }
        pthread_mutex_unlock(&mutex_received_replies);
#ifdef DEBUG_MUTEX
        cout << "8: unlocked mutex_received_replies" << endl;
#endif
    }
    else if (received_message_type == REQUEST) // Receieved a REQUEST
    {
#ifdef DEBUG_LAMPORT
        cout << "Received MQTT REQUEST - From worker: " << received_worker_number << ", Lamport: " << received_lamport_time << endl;
        cout << "Current L: " << debug_lamport << ", request: " << request_lamport_time << endl;
#endif
        /*
         *   Respond to Lamport REQUESTs:
         *   If this worker doesn't want to write to the database itself, then reply immediately
         *   Or if this worker has a REQUEST running itself but the REQUEST is younger than the received one, reply immediately
         *   Else if this worker has a REQUEST running that is older than the received one, wait till this workers request is finished
         */

#ifdef DEBUG_MUTEX
        cout << "9: lock mutex_mqtt_request_open" << endl;
#endif
        pthread_mutex_lock(&mutex_mqtt_request_open);
#ifdef DEBUG_MUTEX
        cout << "9: locked mutex_mqtt_request_open" << endl;
#endif

        if (!mqtt_request_open) // Worker doesn't want to write to the database itself
        {
            pthread_mutex_unlock(&mutex_mqtt_request_open);
#ifdef DEBUG_MUTEX
            cout << "9.1: unlocked mutex_mqtt_request_open" << endl;
#endif
            // Reply immediately
#ifdef DEBUG_LAMPORT
            cout << "MQTT: Replying immediately" << endl;
#endif
            mqtt_reply(received_worker_number);
        }
        else // This worker has requested to write to the database and is waiting for replies
        {
            pthread_mutex_unlock(&mutex_mqtt_request_open);
#ifdef DEBUG_MUTEX
            cout << "9.2: unlocked mutex_mqtt_request_open" << endl;
            cout << "10: lock mutex_request_lamport_time" << endl;
#endif
            pthread_mutex_lock(&mutex_request_lamport_time);
#ifdef DEBUG_MUTEX
            cout << "10: locked mutex_request_lamport_time" << endl;
#endif
            if (request_lamport_time == received_lamport_time)
            {
                pthread_mutex_unlock(&mutex_request_lamport_time);
#ifdef DEBUG_MUTEX
                cout << "10.1: unlocked mutex_request_lamport_time" << endl;
#endif
                if (worker_nr > received_worker_number)
                {
                    // Reply immediately
#ifdef DEBUG_LAMPORT
                    cout << "MQTT: Nothing to wait for, replying immediately (same lamport time)" << endl;
#endif
                    mqtt_reply(received_worker_number);
                }
                else
                {
                    // Wait until own request is finished
#ifdef DEBUG_LAMPORT
                    cout << "MQTT: Waiting for replies to my request (same lamport time)" << endl;
#endif
#ifdef DEBUG_MUTEX
                    cout << "11: lock mutex_request_queue" << endl;
#endif
                    pthread_mutex_lock(&mutex_request_queue);
#ifdef DEBUG_MUTEX
                    cout << "11: locked mutex_request_queue" << endl;
#endif
                    req_worker_numbers.push(received_worker_number);
                    req_lamport_times.push(received_lamport_time);
                    pthread_mutex_unlock(&mutex_request_queue);
                }
            }
            else if (request_lamport_time > received_lamport_time)
            {
                pthread_mutex_unlock(&mutex_request_lamport_time);
#ifdef DEBUG_MUTEX
                cout << "10.2: unlocked mutex_request_lamport_time" << endl;
#endif
                // Reply immediately
#ifdef DEBUG_LAMPORT
                cout << "MQTT: Nothing to wait for, replying immediately (received request older)" << endl;
#endif
                mqtt_reply(received_worker_number);
            }
            else
            {
// Wait until own request is finished
#ifdef DEBUG_LAMPORT
                cout << "MQTT: Waiting for replies to my request (received request younger)" << endl;
#endif
                pthread_mutex_unlock(&mutex_request_lamport_time);
#ifdef DEBUG_MUTEX
                cout << "10.2: unlocked mutex_request_lamport_time" << endl;
                cout << "12: lock mutex_request_queue" << endl;
#endif
                pthread_mutex_lock(&mutex_request_queue);
#ifdef DEBUG_MUTEX
                cout << "12: locked mutex_request_queue" << endl;
#endif
                req_worker_numbers.push(received_worker_number);
                req_lamport_times.push(received_lamport_time);
                pthread_mutex_unlock(&mutex_request_queue);
#ifdef DEBUG_MUTEX
                cout << "12: unlocked mutex_request_queue" << endl;
#endif
            }
        }
    }
}

void mqtt_subscribe(string topic)
{
    if (mosquitto_subscribe(client, nullptr, topic.c_str(), 0))
    {
        perror("Unable to subscribe.\n");
        return;
    }
}

void init_mqtt()
{
    // Initialize the Mosquitto library
    mosquitto_lib_init();

    // Create a new Mosquitto client
    client = mosquitto_new(nullptr, true, nullptr);
    if (!client)
    {
        perror("Error: Out of memory.\n");
        return;
    }

    // Set the callback function for incoming messages
    mosquitto_message_callback_set(client, onMQTTMessage);

    // Connect to the Mosquitto broker
    if (mosquitto_connect(client, "173.40.0.10", 1883, 60))
    {
        perror("Unable to connect.\n");
        return;
    }

    // Subscribe to own REPLY topic
    mqtt_subscribe("REPLY/" + numberToString(worker_nr));
    // cout << "MQTT: Subscribed to topic REPLY/" << worker_nr << endl;

    // Subscribe to all but own REQUEST topics
    for (int i = 0; i < number_workers; ++i)
    {
        // Skip own worker number
        if (i != worker_nr)
        {
            // cout << "MQTT: Subscribed to topic REQUEST/" << i << endl;
            mqtt_subscribe("REQUEST/" + numberToString(i));
        }
    }

    sem_post(&sem_mqtt);
}

void *mqtt_loop(void *args)
{
    init_mqtt(); // Has to be done after registration at controller, so the worker knows its unique number
    cout << "Initialized MQTT" << endl;
    mosquitto_loop_forever(client, -1, 1);
}

int main()
{
    program_start_time = chrono::steady_clock::now();
    cout << "Worker is running" << endl;

    sem_init(&sem_mqtt, 0, 0);
    sem_init(&sem_mqtt_ready, 0, 0);
    sem_init(&sem_write_database, 0, 0);

    // Create RPC server thread
    handler->setHTTPFunc(writeToDatabase);
    handler->setMQTTSem(sem_mqtt);
    handler->setMQTTReadySem(sem_mqtt_ready);
    handler->setCalculationFinished(finished_calculation);
    pthread_t thread_RPC;
    pthread_create(&thread_RPC, nullptr, rpc_serve, nullptr);

    if (init_udp() < 0)
    {
        return -1;
    }
    registerAtController();
    readNumberWorkers();

    // Create healthcheck response thread
    pthread_t thread_hc_responder;
    pthread_create(&thread_hc_responder, nullptr, healthcheck, nullptr);

    // Start thread to run the MQTT client loop for processing incoming messages
    pthread_t thread_mqtt_responder;
    pthread_create(&thread_mqtt_responder, nullptr, mqtt_loop, nullptr);

    setTCPSockTimeoutSeconds(PROGR_TIMEOUT / 1000); // Set tcp socket timeout to PROGR_TIMEOUT in seconds to make timeout work
    while (1)                                       // Program loop
    {
        if (chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - program_start_time).count() > PROGR_TIMEOUT)
        {
            cout << "Worker timed out" << endl;
            break;
        }

        if (finished_calculation)
        {
            sleep(4); // Wait 4 seconds to let the controller output its results before the workers do
            break;
        }
    }

#ifdef DEBUG_MUTEX
    cout << "13: lock mutex_lamport_time" << endl;
#endif
    pthread_mutex_lock(&mutex_lamport_time);
#ifdef DEBUG_MUTEX
    cout << "13: locked mutex_lamport_time" << endl;
#endif
    int final_lamport = lamport_time;
    pthread_mutex_unlock(&mutex_lamport_time);
#ifdef DEBUG_MUTEX
    cout << "13: unlocked mutex_lamport_time" << endl;
#endif

    // Output results
    mqtt_request();
    cout << endl
         << "---------------------------------------------------------" << endl;
    cout << "Worker " << worker_nr << ": " << endl;
    cout << "This worker computed " << count_calculations << " tasks." << endl << endl;
    cout << "Maximum of measured HTTP RTT: " << max_rtt_hc << " us" << endl;
    cout << "Mean of HTTP RTT values: " << sum_rtt_hc / (rtt_count > 0 ? rtt_count : 1) << " us" << endl << endl;
    if (count_mqtt_requests > 0)
    {
        cout << "MQTT REQUEST times :" << endl;
        cout << "Minimum: " << min_mqtt_request_times << " us, maximum: " << max_mqtt_request_times << " us, ";
        cout << "mean: " << sum_mqtt_request_times / count_mqtt_requests << " us" << endl << endl;
    }
    cout << "Lamport time after calculation: " << final_lamport << endl;
    cout << "---------------------------------------------------------" << endl
         << endl;
    sleep(1);
    mqtt_respond_to_open_requests();

    int wait_secs = number_workers + 1;
    sleep(wait_secs); // Neccessary to make the lamport algorithm work until all workers printed their results

    pthread_cancel(thread_hc_responder);
    pthread_cancel(thread_RPC);
    pthread_cancel(thread_mqtt_responder);

    // Clean up MQTT
    mosquitto_destroy(client);
    mosquitto_lib_cleanup();

    return 0;
}
