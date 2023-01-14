/**
 * Autogenerated by Thrift Compiler (0.17.0)
 *
 * DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING
 *  @generated
 */
#ifndef RPCSendMatrixTasks_TYPES_H
#define RPCSendMatrixTasks_TYPES_H

#include <iosfwd>

#include <thrift/Thrift.h>
#include <thrift/TApplicationException.h>
#include <thrift/TBase.h>
#include <thrift/protocol/TProtocol.h>
#include <thrift/transport/TTransport.h>

#include <functional>
#include <memory>




class RPCMatrixTask;

typedef struct _RPCMatrixTask__isset {
  _RPCMatrixTask__isset() : result_matrix(false), row(false), col(false), matrix_chunk_a(false), matrix_chunk_b(false) {}
  bool result_matrix :1;
  bool row :1;
  bool col :1;
  bool matrix_chunk_a :1;
  bool matrix_chunk_b :1;
} _RPCMatrixTask__isset;

class RPCMatrixTask : public virtual ::apache::thrift::TBase {
 public:

  RPCMatrixTask(const RPCMatrixTask&);
  RPCMatrixTask& operator=(const RPCMatrixTask&);
  RPCMatrixTask() noexcept
                : result_matrix(0),
                  row(0),
                  col(0) {
  }

  virtual ~RPCMatrixTask() noexcept;
  int32_t result_matrix;
  int64_t row;
  int64_t col;
  std::vector<int32_t>  matrix_chunk_a;
  std::vector<int32_t>  matrix_chunk_b;

  _RPCMatrixTask__isset __isset;

  void __set_result_matrix(const int32_t val);

  void __set_row(const int64_t val);

  void __set_col(const int64_t val);

  void __set_matrix_chunk_a(const std::vector<int32_t> & val);

  void __set_matrix_chunk_b(const std::vector<int32_t> & val);

  bool operator == (const RPCMatrixTask & rhs) const
  {
    if (!(result_matrix == rhs.result_matrix))
      return false;
    if (!(row == rhs.row))
      return false;
    if (!(col == rhs.col))
      return false;
    if (!(matrix_chunk_a == rhs.matrix_chunk_a))
      return false;
    if (!(matrix_chunk_b == rhs.matrix_chunk_b))
      return false;
    return true;
  }
  bool operator != (const RPCMatrixTask &rhs) const {
    return !(*this == rhs);
  }

  bool operator < (const RPCMatrixTask & ) const;

  uint32_t read(::apache::thrift::protocol::TProtocol* iprot) override;
  uint32_t write(::apache::thrift::protocol::TProtocol* oprot) const override;

  virtual void printTo(std::ostream& out) const;
};

void swap(RPCMatrixTask &a, RPCMatrixTask &b);

std::ostream& operator<<(std::ostream& out, const RPCMatrixTask& obj);



#endif