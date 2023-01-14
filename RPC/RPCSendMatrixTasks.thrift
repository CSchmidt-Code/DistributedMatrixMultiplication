struct RPCMatrixTask {
  1: i32 result_matrix,
  2: i64 row,
  3: i64 col,
  4: list<i32> matrix_chunk_a,
  5: list<i32> matrix_chunk_b
}

service RPCSendMatrixTasks {
  bool calcTask(1: RPCMatrixTask rpc_matrix_task),
  bool sendTaskResult(1: bool last_chunk),
  bool isMQTTInitialized(),
  void MQTTReady(),
  void calculationIsFinished()
}
