#include <vector>
#include <stack>

using namespace std;

class ResultMatrix
{
private:
    vector<vector<int>> result_matrix;
    vector<vector<bool>> final_value;
    int matrix_index;
    int count_console_output = 0;

public:
    ResultMatrix(int target_matrix)
    {
        matrix_index = target_matrix;
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

    string getResult(int row, int col)
    {
        if (result_matrix.size() < row + 1 || result_matrix.at(row).size() < col + 1) // Position not yet calculated
        { 
            return "X";
        }

        if (!final_value.at(row).at(col)) // Value not final
        { 
            return "X";
        }

        return numberToString(result_matrix.at(row).at(col));
    }

    string getHTMLMatrix()
    {
        string str_matrix = "<table border=\"1px\">";

        for (int i = 0; i < result_matrix.size(); ++i)
        {
            str_matrix.append("<tr>");
            for (int j = 0; j < result_matrix.at(i).size(); ++j)
            {
                str_matrix.append("<td>" + this->getResult(i, j) + "</td>");
            }
            str_matrix.append("</tr>");
        }

        str_matrix.append("</table>");

        return str_matrix;
    }

    string getHTMLRow(const int &row)
    {
        string str_matrix;

        if (row >= result_matrix.size())
        {
            str_matrix = "<h2>Requested row is not yet calculated</h2>";
            return str_matrix;
        }

        str_matrix = "<table border=\"1px\"><tr>";
        for (int i = 0; i < result_matrix.at(row).size(); ++i)
        {
            str_matrix.append("<td>" + this->getResult(row, i) + "</td>");
        }
        str_matrix.append("</tr></table>");

        return str_matrix;
    }

    string getHTMLCol(const int &col)
    {
        string str_matrix = "<h2>Single columns can't be printed, try single rows instead</h2>";
        return str_matrix;
    }

    void addResult(long long value, int row, int col, bool value_final)
    {
        while (result_matrix.size() < row + 1) // Add rows
        { 
            result_matrix.push_back(vector<int>());
            final_value.push_back(vector<bool>());
        }

        while (result_matrix.at(row).size() < col + 1) // Add columns
        { 
            result_matrix.at(row).push_back(0);
            final_value.at(row).push_back(false);
        }

        result_matrix.at(row).at(col) += value;
        cout << ++count_console_output << ":\tStored value " << value << " to matrix " << matrix_index << " at row " << row << ", col " << col << endl;
        
        if (value_final)
        {
            final_value.at(row).at(col) = true;
        }
    }
};