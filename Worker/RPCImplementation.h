#include "./gen-cpp/RPCSendMatrixTasks.h"
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <iostream>

using namespace ::std;
using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

struct httpTask {
  int result_matrix;
  long long row;
  long long col;
  long long result;
  bool last_chunk;
};

class RPCSendMatrixTasksHandler : virtual public RPCSendMatrixTasksIf
{
public:
  RPCSendMatrixTasksHandler()
  {
    
  }

  void setHTTPFunc(bool (*httpFunc)(httpTask)) {
    this->httpFunc = httpFunc;
  }

  void setMQTTSem(sem_t &sem_mqtt) {
    this->sem_mqtt = &sem_mqtt;
  }

  void setMQTTReadySem(sem_t &sem_mqtt_ready) {
    this->sem_mqtt_ready = &sem_mqtt_ready;
  }

  void setCalculationFinished(bool &calculation_finished) {
    this->calculation_finished = &calculation_finished;
  }

  bool calcTask(const RPCMatrixTask &rpc_matrix_task)
  {
    //cout << "Call of RPC calcTask()" << endl;
    current_task = rpc_matrix_task;
    current_result = 0;

    for (int i = 0; i < rpc_matrix_task.matrix_chunk_a.size(); ++i)
    {
      current_result += rpc_matrix_task.matrix_chunk_a.at(i) * rpc_matrix_task.matrix_chunk_b.at(i);
    }

    return true;
  }

  bool sendTaskResult(const bool last_chunk)
  {
    //cout << "Call of RPC sendTaskResult()" << endl;
    httpTask task;
    task.result_matrix = current_task.result_matrix;
    task.row = current_task.row;
    task.col = current_task.col;
    task.result = current_result;
    task.last_chunk = last_chunk;
    return httpFunc(task);
  }

  // Waits for MQTT to be initialized and returns true after that
  bool isMQTTInitialized() {
    // cout << "Call of RPC isMQTTInitialized()" << endl;
    sem_wait(sem_mqtt);
    return true;
  }

  // Is called by the controller when all workers finished initializing MQTT
  void MQTTReady() {
    //cout << "Call of RPC MQTTReady()" << endl;
    sem_post(sem_mqtt_ready);
  }

  // Is called by the controller when all tasks have been calculated
  void calculationIsFinished() {
    //cout << "Call of RPC calculationIsFinished()" << endl;
    *calculation_finished = true;
  }

private:
  RPCMatrixTask current_task;
  long long current_result;
  bool (*httpFunc)(httpTask);
  sem_t *sem_mqtt;
  sem_t *sem_mqtt_ready;
  bool *calculation_finished;
};