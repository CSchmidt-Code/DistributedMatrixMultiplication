#include "Matrix.h"
#include "./gen-cpp/RPCSendMatrixTasks_types.h"
#include <vector>
#include <iostream>
#include <random>
#include <queue>
#include <map>
#include <mutex>

using namespace std;

// matrix calculation data
#define NUMBER_MATRICES number_matrices   // Has to be greater than 0
#define MAX_CHUNK_SIZE max_chunk_size    // Maximum size of matrix chunk that gets send to a worker

// Number of rows and cols of the two matrices
#define MATRIX_A_ROWS a_rows
#define MATRIX_A_COLS a_cols
#define MATRIX_B_ROWS MATRIX_A_COLS
#define MATRIX_B_COLS b_cols
#define RESULT_ROWS MATRIX_A_ROWS
#define RESULT_COLS MATRIX_B_COLS

/* 
*   When remaining_chunk_size is smaller than this fraction of a chunk,
*   the remainder gets spread among the other chunks
*/ 
#define MAX_REMAINDER_FRACTION (float(1) / 7)

struct ResultField
{
    int resultMatrix;
    long long row;
    long long col;
};

/*
*   This class creates at least two matrices filled with random numbers to be multiplied by the workers.
*   When NUMBER_MATRICES is greater than 1, multiple matrix pairs to be multiplied are created.
*   Upon initialization it calculates the chunk size and prepares all the tasks, 
*   that get send to the workers during calculation.
*/
class MatrixData
{
public:
    MatrixData()
    {
    }

    void initialize(int &number_workers, long long &a_rows, long long &a_cols, long long &b_cols, int &number_matrices, long long &max_chunk_size) // Generate matrices and tasks
    {
        this->number_workers = number_workers;
        this->a_rows = a_rows;
        this->a_cols = a_cols;
        this->b_cols = b_cols;
        this->number_matrices = number_matrices;
        this->max_chunk_size = max_chunk_size;

        // Fill matrices with random data
        std::random_device dev;
        std::mt19937 rng(dev());
        std::uniform_int_distribution<std::mt19937::result_type> dist(-100, 100);
        for (int result_matrix = 0; result_matrix < NUMBER_MATRICES; ++result_matrix)
        {
            Matrix<int> *matrix_a = new Matrix<int>(MATRIX_A_ROWS, MATRIX_A_COLS);
            Matrix<int> *matrix_b = new Matrix<int>(MATRIX_B_ROWS, MATRIX_B_COLS);
            matrices_a.push_back(*matrix_a);
            matrices_b.push_back(*matrix_b);
            
            Matrix<queue<RPCMatrixTask>> *task_queue_matrix = new Matrix<queue<RPCMatrixTask>>(MATRIX_A_ROWS, MATRIX_B_COLS);
            Matrix<int> *stored_chunks_matrix = new Matrix<int>(MATRIX_A_ROWS, MATRIX_B_COLS, 0);
            matrix_tasks.push_back(*task_queue_matrix);
            matrix_stored_chunks.push_back(*stored_chunks_matrix);

            // Fill matrix B
            for (long long row = 0; row < MATRIX_A_ROWS; ++row)
            {
                for (long long col = 0; col < MATRIX_A_COLS; ++col)
                {
                    matrices_a.at(result_matrix).set(row, col, int(dist(rng)));
                }
            }

            // Fill matrix A
            for (long long row = 0; row < MATRIX_B_ROWS; ++row)
            {
                for (long long col = 0; col < MATRIX_B_COLS; ++col)
                {
                    matrices_b.at(result_matrix).set(row, col, int(dist(rng)));
                }
            }

            // Fill next_task_field queue for current result matrix
            for (long long row = 0; row < RESULT_ROWS; ++row)
            {
                for (long long col = 0; col < RESULT_COLS; ++col)
                {
                    ResultField tmp;
                    tmp.resultMatrix = result_matrix;
                    tmp.row = row;
                    tmp.col = col;
                    next_task_field.push(tmp);
                }
            }
        }

        // Prepare calculation tasks
        this->prepareTasks();
    }

    bool getNextTask(RPCMatrixTask& task, int worker)
    {
        //cout << "MatrixData: Call of getNextTask() for worker " << worker << endl;
        pthread_mutex_lock(&mutex_tasks);
        if (next_task_field.size() > 0)
        {
            ResultField field = next_task_field.front(); // Where are the next tasks?
            //cout << "getNextTask(): Next task will be taken from matrix " << field.resultMatrix << ", row " << field.row << ", column " << field.col << endl;
            // Fetch task
            if(matrix_tasks.at(field.resultMatrix).at(field.row, field.col).size() == 0) 
            {
                cout << "ERROR: MatrixData (getNextTask()): There were no tasks left at the specified field" << endl;
                return false;
            }

            //cout << "MatrixData (getNextTask()): Loaded a task from the task matrix" << endl;
            task = matrix_tasks.at(field.resultMatrix).at(field.row, field.col).front();
            matrix_tasks.at(field.resultMatrix).at(field.row, field.col).pop(); // Pop task from queue at field position
            if (matrix_tasks.at(field.resultMatrix).at(field.row, field.col).size() == 0)
            {
                //cout << "MatrixData (getNextTask()): Current task field is now empty" << endl;
                next_task_field.pop(); // No more tasks at current field
            }
            pthread_mutex_unlock(&mutex_tasks);
            return true;
        }
        else {
            pthread_mutex_unlock(&mutex_tasks);
            return false;
        }
    }

    /*
    *   If the controller detects an unhealthy worker, it blocks the worker 
    *   and uses this function to push its task back into the task queue
    */
    void reassignTask(const RPCMatrixTask &task)
    {
        cout << "MatrixData: Call of reassignTask()" << endl;
        pthread_mutex_lock(&mutex_tasks);
        // push task back into corresponding task matrix
        matrix_tasks.at(task.result_matrix).at(task.row, task.col).push(task);
        if (matrix_tasks.at(task.result_matrix).at(task.row, task.col).size() == 1)
        {
            // If task field was empty, add it to the next task fields again
            ResultField field;
            field.resultMatrix = task.result_matrix;
            field.row = task.row;
            field.col = task.col;
            next_task_field.push(field);
        }
        pthread_mutex_unlock(&mutex_tasks);
    }

    // Returns the number of not multiplied chunks in the next task field
    int getNumberTaskFields()
    {
        return next_task_field.size();
    }

    bool allTasksFinished()
    {
        if (finished_fields == RESULT_ROWS * RESULT_COLS * NUMBER_MATRICES)
        {
            return true;
        }
        return false;
    }

    /*
    *   Returns true if task contains the last chunk of a result field.
    *   If this function returns true, the controller tells the calculating worker to inform the database,
    *   that it now has all the chunks of the result field.
    */ 
    bool lastChunk(RPCMatrixTask& task) {
        int stored_chunks = matrix_stored_chunks.at(task.result_matrix).at(task.row, task.col);
        if(stored_chunks == chunks_per_row - 1) {
            return true;
        }
        return false;
    }

    /*
    *   Used to inform MatrixData class about the finished calculation of a task
    */
    void informAboutStoredResult(RPCMatrixTask& task) {
        pthread_mutex_lock(&mutex_stored_result);
        int stored_chunks = matrix_stored_chunks.at(task.result_matrix).at(task.row, task.col);
        ++stored_chunks;
        matrix_stored_chunks.at(task.result_matrix).set(task.row, task.col, stored_chunks);
        if(stored_chunks == chunks_per_row) {
            ++finished_fields;
        }
        pthread_mutex_unlock(&mutex_stored_result);
    }

private:
    int number_workers;
    long long a_rows;
    long long a_cols;
    long long b_cols;
    int number_matrices;
    long long max_chunk_size;

    long long total_multiplications;
    long long calculations_per_worker;
    long long chunks_per_row;
    long long chunk_size;
    bool remainder_chunk = false; // True if there is a smaller chunk at the end of each row
    long long remaining_chunk_size;
    long long total_number_tasks = 0;
    
    pthread_mutex_t mutex_tasks = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t mutex_stored_result = PTHREAD_MUTEX_INITIALIZER;

    // Matrices stored in vectors so multiple matrix multiplications can be executed during a single program execution
    vector<Matrix<int>> matrices_a;
    vector<Matrix<int>> matrices_b;

    vector<Matrix<queue<RPCMatrixTask>>> matrix_tasks; // Multiple result matrices with queues of multiple tasks for each field
    queue<ResultField> next_task_field;                // Stores result matrix, row, col of fields with remaining tasks
    vector<Matrix<int>> matrix_stored_chunks;          // Number of stored chunks in database (if equals chunks_per_row -> result field finished)
    long long finished_fields = 0;                     // Number fields whose results are stored in the database

    void prepareTasks()
    {
        total_multiplications = RESULT_ROWS * RESULT_COLS * MATRIX_A_COLS * NUMBER_MATRICES;
        if(this->number_workers == 0) {
            ++this->number_workers;
            cout << "MatrixData: number of workers can't be zero" << endl;
        }
        calculations_per_worker = total_multiplications / this->number_workers; // Number of calculations to be performed by every worker
        if(calculations_per_worker == 0) {
            calculations_per_worker = 1;
        }

        if (calculations_per_worker < MATRIX_A_COLS || MAX_CHUNK_SIZE < MATRIX_A_COLS) // Split matrix A rows and matrix B cols into chunks
        {
            chunk_size = (MAX_CHUNK_SIZE > MATRIX_A_COLS) ? MATRIX_A_COLS : MAX_CHUNK_SIZE; // Set initial chunk_size (may be further adjusted)

            if (calculations_per_worker < chunk_size) // Chunks don't need to be that big because there are more than enough worker
            {
                chunk_size = calculations_per_worker; // Adjust the chunk size
            }

            if(chunk_size == 0) {
                cout << "MatrixData: chunk_size can't be zero here" << endl;
            }      
            chunks_per_row = MATRIX_A_COLS / chunk_size;       // Number of chunks per row in matrix A
            remaining_chunk_size = MATRIX_A_COLS % chunk_size; // Number of values remaining when row was split into chunks

            if (remaining_chunk_size > 0) // Chunksize doesn't divide a row of matrix A perfectly
            {
                if(chunk_size == 0) {
                    cout << "MatrixData: chunk_size can't be zero here" << endl;
                }  
                
                // If remaining chunk is smaller than MAX_REMAINDER_FRACTION of chunksize
                if ((float(remaining_chunk_size) / chunk_size) < MAX_REMAINDER_FRACTION)
                {
                    remainder_chunk = false;

                    // Split up the remaining chunk resizing the other chunks
                    while (remaining_chunk_size > chunks_per_row)
                    {
                        ++chunk_size; // Make each chunk bigger by one value (MAX_CHUNK_SIZE can get slighly exceeded)
                        remaining_chunk_size -= chunks_per_row;
                    }
                    // If remaining chunk size is still bigger than one, the remaining value will later on be added to the first few chunks of the row
                }
                else // Leave remaining chunk as an additional smaller chunk
                {
                    remainder_chunk = true;
                    ++chunks_per_row; // One more chunk per row
                }
            }
        }
        else // Pack tasks with full rows of matrix A and cols of matrix B
        {
            chunks_per_row = 1;
            chunk_size = MATRIX_A_COLS;
            remaining_chunk_size = 0;
            remainder_chunk = false;
        }

        this->createTasks();
    }

    void createTasks()
    {
        // Maybe change to be able create tasks during calculation on workers
        for (int result_matrix = 0; result_matrix < NUMBER_MATRICES; ++result_matrix)
        {
            // Iterations over all fields of result matrix with index result_matrix
            for (long long result_row = 0; result_row < RESULT_ROWS; ++result_row)
            {
                for (long long result_col = 0; result_col < RESULT_COLS; ++result_col)
                {
                    // Create tasks for current result field
                    queue<RPCMatrixTask> task_queue; // One queue of tasks for each field in result matrix

                    long long remaining_values = remaining_chunk_size; // Copying remaining_chunk_size
                    long long current_value_index = 0;
                    for (long long chunk = 0; chunk < chunks_per_row; ++chunk)
                    {
                        long long current_chunk_size = chunk_size;
                        if (remaining_values > 0)
                        {
                            if (remainder_chunk)
                            {
                                if (chunk == chunks_per_row - 1) // Reached the remainder chunk
                                {
                                    current_chunk_size = remaining_values; // Correct chunk size for last chunk
                                }
                            }
                            else // There are values that have to be added to other chunks
                            {
                                ++current_chunk_size;
                                --remaining_values;
                            }
                        }

                        int *chunk_a = matrices_a.at(result_matrix).getRowArray(result_row, current_value_index, current_chunk_size);
                        int *chunk_b = matrices_b.at(result_matrix).getColArray(result_col, current_value_index, current_chunk_size);
                        current_value_index += current_chunk_size;

                        if (chunk_a == nullptr || chunk_b == nullptr)
                        {
                            outputResults();
                            if (remainder_chunk)
                            {
                                cout << "There will be an additional chunk" << endl;
                            }
                            cout << "Current chunk: " << chunk << endl;
                            cout << "Current chunk size: " << current_chunk_size << endl;
                            cout << "Current value index: " << current_value_index << endl;
                            cout << "Remaining values: " << remaining_values << " of " << remaining_chunk_size << endl;
                            cout << endl
                                 << "---------------------------------------------------------" << endl
                                 << endl;
                            perror("MatrixData: Segmentation fault");
                        }

                        RPCMatrixTask current_task;
                        current_task.result_matrix = result_matrix;
                        current_task.row = result_row;
                        current_task.col = result_col;
                        current_task.matrix_chunk_a = vector<int>(chunk_a, &chunk_a[current_chunk_size]);
                        current_task.matrix_chunk_b = vector<int>(chunk_b, &chunk_b[current_chunk_size]);

                        task_queue.push(current_task);
                        ++total_number_tasks;
                    }
                    matrix_tasks.at(result_matrix).set(result_row, result_col, task_queue);
                }
            }
        }

        this->outputResults();
    }

    void outputResults()
    {
        cout << endl
             << "---------------------------------------------------------" << endl;
        cout << "Finished dividing matrices into tasks:" << endl;
        cout << "Result matrices will have a size of " << RESULT_ROWS << " x " << RESULT_COLS << "." << endl;
        cout << "Therefore " << total_multiplications << " multiplications have to be computed." << endl;
        cout << "So each of the " << number_workers << " workers will do just about " << calculations_per_worker << " multiplications." << endl;
        cout << endl
             << "Each row of an input matrix A will be split into " << chunks_per_row << " chunk(s)." << endl;
        if (remainder_chunk)
        {
            if (chunks_per_row > 2)
            {
                cout << "The first " << chunks_per_row - 1 << " chunks will be " << chunk_size << " values in size and the last chunk will be " << remaining_chunk_size << " values in size." << endl;
            }
            else
            {
                cout << "The first chunk will be " << chunk_size << " values in size and the second chunk will be " << remaining_chunk_size << " values in size." << endl;
            }
        }
        else
        {
            cout << "Each chunk will have a size of " << chunk_size << " value(s) resulting in " << chunk_size << " multiplication(s) per task." << endl;
            if (remaining_chunk_size > 0)
            {
                cout << "Since " << MATRIX_A_COLS << " value(s) in matrix A divided by " << chunks_per_row << " chunk(s) of size " << chunk_size << " leaves " << remaining_chunk_size << " value(s)," << endl;
                cout << "the remaining value(s) will be equally distributed to the other chunks." << endl;
            }
        }
        cout << "In total " << total_number_tasks << " tasks were created" << endl;
        cout << "---------------------------------------------------------" << endl
             << endl;
    }
};