/**
 * Autogenerated by Thrift Compiler (0.17.0)
 *
 * DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING
 *  @generated
 */
#include "RPCSendMatrixTasks_types.h"

#include <algorithm>
#include <ostream>

#include <thrift/TToString.h>




RPCMatrixTask::~RPCMatrixTask() noexcept {
}


void RPCMatrixTask::__set_result_matrix(const int32_t val) {
  this->result_matrix = val;
}

void RPCMatrixTask::__set_row(const int64_t val) {
  this->row = val;
}

void RPCMatrixTask::__set_col(const int64_t val) {
  this->col = val;
}

void RPCMatrixTask::__set_matrix_chunk_a(const std::vector<int32_t> & val) {
  this->matrix_chunk_a = val;
}

void RPCMatrixTask::__set_matrix_chunk_b(const std::vector<int32_t> & val) {
  this->matrix_chunk_b = val;
}
std::ostream& operator<<(std::ostream& out, const RPCMatrixTask& obj)
{
  obj.printTo(out);
  return out;
}


uint32_t RPCMatrixTask::read(::apache::thrift::protocol::TProtocol* iprot) {

  ::apache::thrift::protocol::TInputRecursionTracker tracker(*iprot);
  uint32_t xfer = 0;
  std::string fname;
  ::apache::thrift::protocol::TType ftype;
  int16_t fid;

  xfer += iprot->readStructBegin(fname);

  using ::apache::thrift::protocol::TProtocolException;


  while (true)
  {
    xfer += iprot->readFieldBegin(fname, ftype, fid);
    if (ftype == ::apache::thrift::protocol::T_STOP) {
      break;
    }
    switch (fid)
    {
      case 1:
        if (ftype == ::apache::thrift::protocol::T_I32) {
          xfer += iprot->readI32(this->result_matrix);
          this->__isset.result_matrix = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 2:
        if (ftype == ::apache::thrift::protocol::T_I64) {
          xfer += iprot->readI64(this->row);
          this->__isset.row = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 3:
        if (ftype == ::apache::thrift::protocol::T_I64) {
          xfer += iprot->readI64(this->col);
          this->__isset.col = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 4:
        if (ftype == ::apache::thrift::protocol::T_LIST) {
          {
            this->matrix_chunk_a.clear();
            uint32_t _size0;
            ::apache::thrift::protocol::TType _etype3;
            xfer += iprot->readListBegin(_etype3, _size0);
            this->matrix_chunk_a.resize(_size0);
            uint32_t _i4;
            for (_i4 = 0; _i4 < _size0; ++_i4)
            {
              xfer += iprot->readI32(this->matrix_chunk_a[_i4]);
            }
            xfer += iprot->readListEnd();
          }
          this->__isset.matrix_chunk_a = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 5:
        if (ftype == ::apache::thrift::protocol::T_LIST) {
          {
            this->matrix_chunk_b.clear();
            uint32_t _size5;
            ::apache::thrift::protocol::TType _etype8;
            xfer += iprot->readListBegin(_etype8, _size5);
            this->matrix_chunk_b.resize(_size5);
            uint32_t _i9;
            for (_i9 = 0; _i9 < _size5; ++_i9)
            {
              xfer += iprot->readI32(this->matrix_chunk_b[_i9]);
            }
            xfer += iprot->readListEnd();
          }
          this->__isset.matrix_chunk_b = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      default:
        xfer += iprot->skip(ftype);
        break;
    }
    xfer += iprot->readFieldEnd();
  }

  xfer += iprot->readStructEnd();

  return xfer;
}

uint32_t RPCMatrixTask::write(::apache::thrift::protocol::TProtocol* oprot) const {
  uint32_t xfer = 0;
  ::apache::thrift::protocol::TOutputRecursionTracker tracker(*oprot);
  xfer += oprot->writeStructBegin("RPCMatrixTask");

  xfer += oprot->writeFieldBegin("result_matrix", ::apache::thrift::protocol::T_I32, 1);
  xfer += oprot->writeI32(this->result_matrix);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldBegin("row", ::apache::thrift::protocol::T_I64, 2);
  xfer += oprot->writeI64(this->row);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldBegin("col", ::apache::thrift::protocol::T_I64, 3);
  xfer += oprot->writeI64(this->col);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldBegin("matrix_chunk_a", ::apache::thrift::protocol::T_LIST, 4);
  {
    xfer += oprot->writeListBegin(::apache::thrift::protocol::T_I32, static_cast<uint32_t>(this->matrix_chunk_a.size()));
    std::vector<int32_t> ::const_iterator _iter10;
    for (_iter10 = this->matrix_chunk_a.begin(); _iter10 != this->matrix_chunk_a.end(); ++_iter10)
    {
      xfer += oprot->writeI32((*_iter10));
    }
    xfer += oprot->writeListEnd();
  }
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldBegin("matrix_chunk_b", ::apache::thrift::protocol::T_LIST, 5);
  {
    xfer += oprot->writeListBegin(::apache::thrift::protocol::T_I32, static_cast<uint32_t>(this->matrix_chunk_b.size()));
    std::vector<int32_t> ::const_iterator _iter11;
    for (_iter11 = this->matrix_chunk_b.begin(); _iter11 != this->matrix_chunk_b.end(); ++_iter11)
    {
      xfer += oprot->writeI32((*_iter11));
    }
    xfer += oprot->writeListEnd();
  }
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldStop();
  xfer += oprot->writeStructEnd();
  return xfer;
}

void swap(RPCMatrixTask &a, RPCMatrixTask &b) {
  using ::std::swap;
  swap(a.result_matrix, b.result_matrix);
  swap(a.row, b.row);
  swap(a.col, b.col);
  swap(a.matrix_chunk_a, b.matrix_chunk_a);
  swap(a.matrix_chunk_b, b.matrix_chunk_b);
  swap(a.__isset, b.__isset);
}

RPCMatrixTask::RPCMatrixTask(const RPCMatrixTask& other12) {
  result_matrix = other12.result_matrix;
  row = other12.row;
  col = other12.col;
  matrix_chunk_a = other12.matrix_chunk_a;
  matrix_chunk_b = other12.matrix_chunk_b;
  __isset = other12.__isset;
}
RPCMatrixTask& RPCMatrixTask::operator=(const RPCMatrixTask& other13) {
  result_matrix = other13.result_matrix;
  row = other13.row;
  col = other13.col;
  matrix_chunk_a = other13.matrix_chunk_a;
  matrix_chunk_b = other13.matrix_chunk_b;
  __isset = other13.__isset;
  return *this;
}
void RPCMatrixTask::printTo(std::ostream& out) const {
  using ::apache::thrift::to_string;
  out << "RPCMatrixTask(";
  out << "result_matrix=" << to_string(result_matrix);
  out << ", " << "row=" << to_string(row);
  out << ", " << "col=" << to_string(col);
  out << ", " << "matrix_chunk_a=" << to_string(matrix_chunk_a);
  out << ", " << "matrix_chunk_b=" << to_string(matrix_chunk_b);
  out << ")";
}


