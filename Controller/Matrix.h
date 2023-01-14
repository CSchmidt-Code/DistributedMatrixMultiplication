#include <iostream>

using namespace std;

template <typename T>
class Matrix
{
private:
    const long long rows;
    const long long columns;
    T** matrix;

public:
    Matrix(const long long &rows, const long long &columns) : rows(rows), columns(columns) 
    {
        matrix = new T*[this->rows];
        for(long long i = 0; i < this->rows; ++i) {
            matrix[i] = new T[columns];
        }
    }

    // All fields of the matrix will be initialized with init_value
    Matrix(const long long &rows, const long long &columns, T init_value) : rows(rows), columns(columns) 
    {
        matrix = new T*[this->rows];
        for(long long i = 0; i < this->rows; ++i) {
            matrix[i] = new T[columns];
            for(long long j = 0; j < columns; ++j) {
                this->set(i, j, init_value);
            }
        }
    }

    ~Matrix() {
        for(long long i = 0; i < this->rows; ++i) {
            //delete[] matrix[i]; // produces mysterious segmentation faults
        }
        //delete[] matrix; // produces mysterious segmentation faults
    }

    long long getRowSize() const
    {
        return this->rows;
    }

    long long getColumnSize() const
    {
        return this->columns;
    }

    T& at(const long long &row, const long long &column) const {
        return this->matrix[row][column];
    }

    void set(const long long &row, const long long &column, const T &val) {
        this->matrix[row][column] = val;
    }

    // Returns the array of a row of the matrix, leaving out the fields of the columns smaller than start_col
    T* getRowArray(const long long &row, const long long &start_col, const int& length) {
        if(this->columns - length >= start_col) {
            return &(this->matrix[row][start_col]);
        }
        return nullptr;
    } 

    // Returns the array of a column of the matrix, leaving out the fields of the rows smaller than start_row
    T* getColArray(const long long &col, const long long &start_row, const int& length) {
        if(this->rows - length >= start_row) {
            T* tmp = new T[length];

            for(int i = start_row; i < start_row + length; ++i) {
                tmp[i - start_row] = this->at(i, col);
            }

            
            return tmp;
        }
        return nullptr;
    } 
};