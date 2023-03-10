// This autogenerated skeleton file illustrates how to build a server.
// You should copy it to another filename to avoid overwriting it.

#include "RPCSendMatrixTasks.h"
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

class RPCSendMatrixTasksHandler : virtual public RPCSendMatrixTasksIf {
 public:
  RPCSendMatrixTasksHandler() {
    // Your initialization goes here
  }

  bool calcTask(const RPCMatrixTask& rpc_matrix_task) {
    // Your implementation goes here
    printf("calcTask\n");
  }

  bool sendTaskResult(const bool last_chunk) {
    // Your implementation goes here
    printf("sendTaskResult\n");
  }

  bool isMQTTInitialized() {
    // Your implementation goes here
    printf("isMQTTInitialized\n");
  }

  void MQTTReady() {
    // Your implementation goes here
    printf("MQTTReady\n");
  }

  void calculationIsFinished() {
    // Your implementation goes here
    printf("calculationIsFinished\n");
  }

};

int main(int argc, char **argv) {
  int port = 9090;
  ::std::shared_ptr<RPCSendMatrixTasksHandler> handler(new RPCSendMatrixTasksHandler());
  ::std::shared_ptr<TProcessor> processor(new RPCSendMatrixTasksProcessor(handler));
  ::std::shared_ptr<TServerTransport> serverTransport(new TServerSocket(port));
  ::std::shared_ptr<TTransportFactory> transportFactory(new TBufferedTransportFactory());
  ::std::shared_ptr<TProtocolFactory> protocolFactory(new TBinaryProtocolFactory());

  TSimpleServer server(processor, serverTransport, transportFactory, protocolFactory);
  server.serve();
  return 0;
}

