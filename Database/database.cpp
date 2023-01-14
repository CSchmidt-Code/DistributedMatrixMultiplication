#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <strings.h>
#include <unistd.h>
#include <chrono>
#include <vector>
#include <string>
#include <stack>
#include <map>
#include "ResultMatrix.h"
#include <arpa/inet.h>

using namespace std;

// DEBUGGING
#define PROGR_TIMEOUT 120000 // Milliseconds after which the program terminates
#define TCP_PORT 80
#define TCP_BUFLEN 50000

#define GET 0
#define POST 1

// Program timeout
chrono::steady_clock::time_point program_start_time;

// TCP data
int sock_TCP, newsock_TCP;
struct sockaddr_in cli_addr, serv_addr;
char *buf_TCP; // Initialize buffer with '\0's

// Result matrix data
map<int, ResultMatrix> result_matrix;

// Data of received HTTP Messages
int http_command, target_matrix, row, col, content;
bool last_chunk;    // Indicates if received result is the last chunk of a date in the result matrix
bool http_valid;

void init_tcp()
{
    // Open TCP socket
    if ((sock_TCP = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Database: can't open stream socket");
    }

    // Bind local address
    bzero((char *)&serv_addr, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(TCP_PORT);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock_TCP, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Database: can't bind local address");
    }

    if (listen(sock_TCP, 5) < 0)
    {
        perror("Database: listen() failed");
    }
}

void printTCPBuf()
{
    for (int i = 0; i < TCP_BUFLEN; ++i)
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

void setTCPSockTimeoutSeconds(int sec)
{
    struct timeval sock_rcv_timeout;
    sock_rcv_timeout.tv_sec = sec;
    sock_rcv_timeout.tv_usec = 0;
    setsockopt(sock_TCP, SOL_SOCKET, SO_RCVTIMEO, &sock_rcv_timeout, sizeof(sockaddr_in));
}

void writeToTCPBuffer(string input)
{
    for (int i = 0; i < input.length(); ++i)
    {
        buf_TCP[i] = input[i];
    }
}

bool byteAddressInTCPBuf(char *address)
{
    // cout << "Buffer base address: " << (void*)&buf_TCP[0] << endl;
    // cout << "Input address: " << (void*)address << endl;

    if (address >= &buf_TCP[0] && (address - &buf_TCP[0]) < TCP_BUFLEN)
    {
        return true;
    }
    return false;
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
        if (!byteAddressInTCPBuf(input + i) ||
            *(input + i) != cmp_str[i]) // Ran out of tcp buffer or found chars that didn't match
        {
            return false;
        }
    }
    return true;
}

// Reads decimal from input until there isn't a number anymore and returns length of string number and -1 input was nullptr
int readNumberFromChars(char *input, int *number, int length = -1)
{
    if (input == nullptr)
    {
        cout << "readNumberFromChars(): input was nullptr" << endl;
        return -1;
    }

    int i = 0;
    (*number) = 0;

    bool negative = false;
    if ((*(input)) == '-')
    {
        negative = true;
        ++i;
    }

    while (byteAddressInTCPBuf(input + i) && (*(input + i)) >= '0' && (*(input + i)) <= '9')
    {
        if (length != -1 && i > length) // Only read length characters
        { 
            break;
        }

        (*number) *= 10;
        (*number) += ((*(input + i)) - '0');
        ++i;
    }
    if (negative)
    {
        (*number) *= -1;
    }

    if (length != -1 && length != i)
    {
        cout << "readNumberFromChars(): Length of readable numbers didn't match given length parameter" << endl;
        return -1;
    }
    return i; // Return number of digits that were read
}

bool httpParse(int *http_command, int *target_matrix, int *row, int *col, bool *last_chunk, int *content)
{
    char *message = &buf_TCP[0];
    *http_command = -1; // 0: GET, 1: POST

    if (strCmpLength(message, "GET /", 5))
    {
        message += 5;
        *http_command = GET;
    }
    else if (strCmpLength(message, "POST /", 6))
    {
        message += 6;
        *http_command = POST;
    }

    if (*http_command == GET || *http_command == POST) // Received GET or POST
    {
        int target_length = readNumberFromChars(message, target_matrix);
        if (target_length <= 0)
        {
            cout << "httpParse(): http message contained no target matrix number" << endl;
            return false;
        }

        message += target_length;

        if (!strCmpLength(message, " HTTP/", 6))
        {
            cout << "httpParse(): http message contained no version number" << endl;
            return false;
        }
        message += 6;

        while (byteAddressInTCPBuf(message) && *message != '\r')
        {
            ++message;
        }

        if (!strCmpLength(message, "\r\n", 2))
        {
            cout << "httpParse(): http message contained no line ending after version number" << endl;
            return false;
        }
        message += 2;

        // Read HTTP fields
        *row = -1;
        *col = -1;
        int content_length = -1;
        *last_chunk = false;
        while (1)
        {
            if (*http_command == GET &&
                strCmpLength(message, "\0", 1)) // Header of GET message may end without blank line
            {
                return true;
            }

            if (strCmpLength(message, "\r\n", 2)) // Is the HTTP header finished?
            {
                message += 2;

                while (strCmpLength(message, "\r\n", 2)) // Is there another blank line?
                {
                    message += 2; // Skip it
                }

                if (*http_command == POST && content_length == -1)
                {
                    cout << "httpParse(): POST message has to specify field Content-Length" << endl;
                    return false;
                }
                if (*http_command == POST)
                {
                    // Read body of POST message
                    if (readNumberFromChars(message, content, content_length) <= 0)
                    {
                        cout << "httpParse(): Length of body didn't match given content length" << endl;
                        return false;
                    }
                }
                // http message" << endl;
                return true;
            }

            if (strCmpLength(message, "Row: ", 5)) // Check for field 'Row'
            {
                if (*row != -1)
                {
                    cout << "httpParse(): Invalid message (repeating occurence of field 'Row')" << endl;
                    return false;
                }

                message += 5;

                int row_number_length = readNumberFromChars(message, row);
                if (row_number_length <= 0)
                {
                    cout << "httpParse(): Field 'Row' has to specify a number" << endl;
                    return false;
                }

                message += row_number_length;

                if (!strCmpLength(message, "\r\n", 2))
                {
                    cout << "httpParse(): Number of field 'Row' has to be followed by line ending" << endl;
                    return false;
                }
                message += 2;
                continue;
            }

            if (strCmpLength(message, "Column: ", 8)) // Check for field 'Column'
            {
                if (*col != -1)
                {
                    cout << "httpParse(): Invalid message (repeating occurence of field 'Column')" << endl;
                    return false;
                }

                message += 8;

                int col_number_length = readNumberFromChars(message, col);
                if (col_number_length <= 0)
                {
                    cout << "httpParse(): Field 'Column' has to specify a number" << endl;
                    return false;
                }

                message += col_number_length;

                if (!strCmpLength(message, "\r\n", 2))
                {
                    cout << "httpParse(): Number of field 'Column' has to be followed by line ending" << endl;
                    return false;
                }
                message += 2;
                continue;
            }

            if (*http_command == POST &&
                (strCmpLength(message, "Content-Length: ", 16) || strCmpLength(message, "content-length: ", 16) ||
                 strCmpLength(message, "Content-length: ", 16))) // Check for field 'Content-Length'
            {
                if (content_length != -1)
                {
                    cout << "httpParse(): Invalid message (repeating occurence of field 'Content-Length')" << endl;
                    return false;
                }

                message += 16;

                int cl_number_length = readNumberFromChars(message, &content_length);
                if (cl_number_length <= 0)
                {
                    cout << "httpParse(): Field 'Content-Length' has to specify a number" << endl;
                    return false;
                }

                message += cl_number_length;

                if (!strCmpLength(message, "\r\n", 2))
                {
                    cout << "httpParse(): Number of field 'Content-Length' has to be followed by line ending" << endl;
                    return false;
                }
                message += 2;
                continue;
            }

            if (*http_command == POST && strCmpLength(message, "Last-Chunk: ", 12)) // Check for field 'Last-Chunk'
            {
                message += 12;

                if (strCmpLength(message, "TRUE\r\n", 6))
                {
                    *last_chunk = true;
                    message += 6;
                    continue;
                }

                if (strCmpLength(message, "FALSE\r\n", 7))
                {
                    *last_chunk = false;
                    message += 7;
                    continue;
                }

                cout << "Field 'Last-Chunk' has to be followed by 'TRUE' or 'FALSE' and a line ending" << endl;
            }

            // If there is a field other than Row, Column or Content-Length search for \r\n (line ending)
            while (1)
            {
                if (!byteAddressInTCPBuf(message))
                {
                    cout << "httpParse(): Invalid ending of http message (ran out of buffer)" << endl;
                    return false;
                }

                if (*message == '\r' && *(message + 1) == '\n')
                {
                    message += 2;
                    break; // Break inner while loop to continue to next line
                }

                ++message; // Next char
            }
        }
    }

    cout << "httpParse(): Database only accepts HTTP GET or POST messages" << endl;
    return false; // No valid http message
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

// Returns true if the HTTP message did fit into the buffer
int writeGETResponseToBuf(const int &target_matrix, const int &row, const int &col)
{
    string msg = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: ";
    string str_content;

    if (result_matrix.count(target_matrix) <= 0) // Result matrix with number target_matrix not yet calculated
    {
        str_content = "<span>Result Matrix " + numberToString(target_matrix) + " could not be found</span>";
    }
    else
    {
        if (row == -1 && col == -1) // Respond with whole matrix
        {
            str_content = "<h2>Result Matrix " + numberToString(target_matrix) + "</h2>" +
                          result_matrix.at(target_matrix).getHTMLMatrix();
        }

        if (row != -1 && col == -1) // Respond with single row
        {
            str_content =
                "<h2>Result Matrix " + numberToString(target_matrix) + ", row " + numberToString(row) + "</h2>" +
                result_matrix.at(target_matrix).getHTMLRow(row);
        }

        if (row == -1 && col != -1) // Respond with single column
        {
            str_content =
                "<h2>Result Matrix " + numberToString(target_matrix) + ", column " + numberToString(col) + "</h2>" +
                result_matrix.at(target_matrix).getHTMLCol(col);
        }

        if (row != -1 && col != -1) // Respond with single value
        {
            str_content =
                "<span>Value of result matrix " + numberToString(target_matrix) + " at row " + numberToString(row) +
                ", column " + numberToString(col) + ": " + result_matrix.at(target_matrix).getResult(row, col) +
                "</span>";
        }
    }

    msg.append(numberToString(str_content.length()));
    msg.append("\r\n\r\n");
    msg.append(str_content);

    writeToTCPBuffer(msg);

    cout << "Length of HTTP response: " << msg.length() << endl;
    return msg.length();
}

int writePOSTResponseToBuf()
{
    string msg = "HTTP/1.1 200 OK\r\n\r\n";
    writeToTCPBuffer(msg);
    return msg.length();
}

int main()
{
    chrono::steady_clock::time_point program_start_time = chrono::steady_clock::now();
    cout << "Database is running" << endl;
    init_tcp();

    setTCPSockTimeoutSeconds(
        PROGR_TIMEOUT / 1000); // Accept times out after PROG_TIMEOUT seconds to make program timeout work

    while (1)
    {
        if (chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - program_start_time).count() >
            PROGR_TIMEOUT)
        {
            cout << "Database timed out" << endl;
            break;
        }

        int addr_len = sizeof(struct sockaddr_in);
        newsock_TCP = accept(sock_TCP, (struct sockaddr *)&cli_addr, (socklen_t *)&addr_len);

        if (newsock_TCP < 0)
        {
            //cout << "Database: No new connection" << endl;
            continue; // If no timeout occurs run accept() again
        }

        // Communication
        buf_TCP = new char[TCP_BUFLEN]();          // Initialize receive buffer with '\0's
        recv(newsock_TCP, buf_TCP, TCP_BUFLEN, 0); // Receive the message
        http_valid = httpParse(&http_command, &target_matrix, &row, &col, &last_chunk, &content); // Parse the received message

        if (http_valid) // Received a valid http message
        {
            int response_length;
            switch (http_command)
            {

            case GET:
                //cout << "Received GET message" << endl;

                response_length = writeGETResponseToBuf(target_matrix, row, col);
                if (response_length > TCP_BUFLEN)
                {
                    cout << "TCP buffer too short for HTTP message" << endl;
                }
                else
                {
                    sendto(newsock_TCP, buf_TCP, response_length, 0, (sockaddr *)&cli_addr,
                           sizeof(struct sockaddr_in));
                }
                break;

            case POST:
                //cout << "Received POST message for target matrix " << target_matrix << endl;
                if (result_matrix.count(target_matrix) <= 0) // If there is no result matrix with key==target_matrix
                {
                    ResultMatrix tmp(target_matrix);
                    result_matrix.insert(make_pair(target_matrix, tmp)); // Add a matrix to the map
                }

                result_matrix.at(target_matrix).addResult(content, row, col, true);

                //cout << "Received valid result data chunk: Target matrix " << target_matrix << ", Row: " << row << ", Column: " << col << endl;

                // Respond
                response_length = writePOSTResponseToBuf();
                sendto(newsock_TCP, buf_TCP, response_length, 0, (sockaddr *)&cli_addr,
                       sizeof(struct sockaddr_in));
                break;
            default:
                cout << "Database doesn't accept HTTP message of that type" << endl;
            }
        }

        delete buf_TCP;

        close(newsock_TCP);
    }

    close(sock_TCP);
    return 0;
}