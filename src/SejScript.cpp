#include <iostream>
#include <string>
#include <regex>
#include <fstream>
#include <map>
#include <vector>
#include <cctype>
#include <chrono>
#include <thread>
#include <cmath>

extern "C" {
    #include "tinyexpr.h"
}

#ifdef _WIN32
    #include <windows.h>
    void clearScreen(void) {
        HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        COORD topLeft = {0, 0};
        DWORD dwCount, dwSize;
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(hOutput, &csbi);
        dwSize = csbi.dwSize.X * csbi.dwSize.Y;
        FillConsoleOutputCharacter(hOutput, 0x20, dwSize, topLeft, &dwCount);
        FillConsoleOutputAttribute(hOutput, 0x07, dwSize, topLeft, &dwCount);
        SetConsoleCursorPosition(hOutput, topLeft);
    }
#endif /* _WIN32 */

#ifdef __unix__
    #include <stdio.h>
    void clearScreen(void) {
        printf("\E[H\E[2J");
    }
#endif /* __unix__ */

using namespace std;

bool continueOnErr = false;

void error(int f_line, string err_msg) {
    cout << "[SejScript Error] On line " << f_line << " :: " << err_msg << endl;
    if (!continueOnErr) {
        exit(1);
    }
}

void parse_ln(int &f_line, string line, bool inline_parse);

string keyWords[] = {
    "make_var",
    "set_var",
    "get_var_pos",
    "goto",
    "if_true",
    "if_false",
    "print",
    "print_no_linebreak",
    "clear_scr",
    "get_input",
    "read_file",
    "write_file",
    "wait",
    "exit",
    "run",
    "eval",
    "let",
    "::",
    "toggle_continue_on_err"
};

vector<string> filesRunning;
map<string, string> variables;
map<string, int> codepoints;

int count_spaces(string _str) {
    int spaces = 0;
    for (int i = 0; i < _str.size(); i++) {
        if (_str[i] == ' ') {
            spaces++;
        }
    }
    return spaces;
}

vector<string> split_str(string _str) {
    vector<string> str;

    string current_str = "";
    bool dbl_quotes = false;

    for (int i = 0; i < _str.size(); i++) {
        if (_str[i] == '"') {
            dbl_quotes = !dbl_quotes;
            current_str += _str[i];
        } else if (_str[i] == ' ' && !dbl_quotes) {
            if (current_str != "") {
                str.push_back(current_str);
                current_str = "";
            }
        } else {
            current_str += _str[i];
        }
    }

    if (current_str != "") {
        str.push_back(current_str);
    }

    return str;
}

bool does_var_exist(string var) {
    if (variables.find(var) == variables.end()) {
        return false;
    } else {
        return true;
    }
}

string get_var(int f_ln, string var) {
    if (!does_var_exist(var)) {
        error(f_ln, "Variable doesn't exist");
        return "";
    }
    return variables.find(var)->second;
}

bool dblquotes_matched(string str) {
    bool found_start_dblquote = false;
    bool found_data = false;
    bool found_end_dblquote = false;

    for (int i = 0; i < str.size(); i++) {
        if (str[i] == '"') {
            if (!found_start_dblquote) {
                if (found_end_dblquote) {
                    error(-1, "Found end dblquote before start dblquote");
                    return false;
                }
                if (found_data) {
                    error(-1, "Found data before start dblquote");
                    return false;
                }
                found_start_dblquote = true;
            } else {
                if (found_end_dblquote) {
                    error(-1, "Unexpected dblquote");
                    return false;
                }
                found_end_dblquote = true;
            }
        } else {
            if (!found_start_dblquote || found_end_dblquote) {
                error(-1, "Unexpected data");
                return false;
            }
            found_data = true;
        }
    }

    return (found_start_dblquote && found_end_dblquote);
}

string tokenise(int ln, string _args) {
    bool dblQuote = false;
    bool fullyAlNum = true;
    int numbersFound = 0;
    bool fullyNum = false;

    for (int i = 0; i < _args.size(); i++) {
        if (!isalnum(_args[i])) {
            fullyAlNum = false;
        } else if (isdigit(_args[i])) {
            numbersFound++;
        }
    }

    if (numbersFound == _args.size()) {
        fullyNum = true;
    }

    string tokenisedString;

    for (int i = 0; i < _args.size(); i++) {
        if (_args[i] == '"') {
            dblQuote = true;
        } else {
            tokenisedString += _args[i];
        }
    }

    if (dblQuote && !dblquotes_matched(_args)) {
        error(ln, "Mismatched double quote (\")");
        return "";
    }

    if (!dblQuote && fullyAlNum) {
        if (!fullyNum) {
            tokenisedString = get_var(ln, tokenisedString);
        }
    }

    return tokenisedString;
}

string trim(string s) {
    regex e("^\\s+|\\s+$");
    return regex_replace(s, e, "");
}

// make_var [VARNAME]
void cmd_make_var(int &f_ln, string args) {
    if (split_str(args).size() != 1) {
        return error(f_ln, "Invalid number of arguments");
    }
    if (does_var_exist(args)) {
        return error(f_ln, "Variable already exists");
    }
    variables.insert(pair<string, string>(args, ""));
}

// set_var [VARNAME] [VALUE]
void cmd_set_var(int &f_ln, string args) {
    vector vars = split_str(args);
    if (vars.size() != 2) {
        return error(f_ln, "Invalid number of arguments");
    }
    if (!does_var_exist(vars[0])) {
        return error(f_ln, "Variable doesn't exist");
    }
    string value = vars[1];
    if (trim(tokenise(f_ln, value)) == "") {
        return;
    }
    value = trim(tokenise(f_ln, value));
    auto itr = variables.find(vars[0]);
    itr->second = value;
}

// print [TXT] or print_no_linebreak [TXT]
void cmd_print(int &f_ln, string args, bool linebreak = true) {
    if (split_str(args).size() < 1) {
        return error(f_ln, "Invalid number of arguments");
    }
    string print_str = tokenise(f_ln, args);
    cout << print_str;
    if (linebreak) cout << endl;
}

// get_var_pos [DEST_VARNAME] [SRC_VARNAME] [INDEX]
void cmd_get_var_pos(int &f_ln, string args) {
    string dest_var;
    string src_var;
    int index;

    if (count_spaces(args) != 2) {
        return error(f_ln, "Invalid number of arguments");
    }

    vector args_split = split_str(args);

    dest_var = args_split[0];
    src_var = args_split[1];
    string _index = args_split[2];
    
    for (int i = 0; i < _index.size(); i++) {
        if (!isdigit(_index[i])) {
            return error(f_ln, "Not a Number (NaN)");
        }
    }

    index = stoi(_index);

    if (!does_var_exist(dest_var) || !does_var_exist(src_var)) {
        return error(f_ln, "Variable doesn't exist");
    }

    string src_var_val = get_var(f_ln, src_var);
    if (index > (src_var_val.size() - 1)) {
        return error(f_ln, "Index is higher than source variable size");
    }
    if (index < 0) {
        return error(f_ln, "Index cannot be less than zero");
    }

    variables.find(dest_var)->second = src_var_val[index];
}

void add_codepoint(int &f_ln, string args) {
    if (split_str(args).size() != 1) {
        return error(f_ln, "Invalid number of arguments");
    }

    if (codepoints.find(args) == codepoints.end()) {
        codepoints.insert(pair<string, int>(args, f_ln));
    } else {
        return error(f_ln, "CodePoint already exists");
    }
}

// goto [CODEPOINT_NAME]
void cmd_goto(int &f_ln, string args) {
    if (split_str(args).size() != 1) {
        return error(f_ln, "Invalid number of arguments");
    }
    if (codepoints.find(args) == codepoints.end()) {
        return error(f_ln, "CodePoint doesn't exist");
    } else {
        f_ln = codepoints.find(args)->second;
    }
}

// clear_scr
void cmd_clear_scr(int &f_ln, string args) {
    clearScreen();
}

// get_input [DEST_VAR]
void cmd_get_input(int &f_ln, string args) {
    vector args_split = split_str(args);
    if (args_split.size() != 1) {
        return error(f_ln, "Invalid number of arguments");
    }
    if (!does_var_exist(args_split[0])) {
        return error(f_ln, "Variables doesn't exist");
    }
    string _input;
    getline(cin, _input);
    variables.find(args_split[0])->second = _input;
}

// wait [SEC]
void cmd_wait(int &f_ln, string args) {
    vector args_split = split_str(args);
    if (args_split.size() != 1) {
        return error(f_ln, "Invalid number of arguments");
    }
    string _time = args_split[0];
    for (int i = 0; i < _time.size(); i++) {
        if (!isdigit(_time[i])) {
            return error(f_ln, "Not a Number (NaN)");
        }
    }
    int time = stoi(_time);
    if (time < 0) {
        return error(f_ln, "Invalid number");
    }
    this_thread::sleep_for(chrono::seconds{time});
}

string tokeniseExpr(int &f_ln, string expr) {
    string tokenisedExpr = "";
    string _tmpStr = "";

    for (int i = 0; i < expr.size(); i++) {
        if (expr[i] == '"') {
            _tmpStr += expr[i];
        } else if (!isalnum(expr[i])) {
            if (_tmpStr != "") {
                tokenisedExpr += tokenise(f_ln, _tmpStr);
                _tmpStr = "";
            }
            tokenisedExpr += expr[i];
        } else {
            _tmpStr += expr[i];
        }
    }
    if (_tmpStr != "") {
        tokenisedExpr += tokenise(f_ln, _tmpStr);
    }

    return tokenisedExpr;
}

// let [DEST_VAR] [EXPR]
void cmd_let(int &f_ln, string args) {
    vector args_split = split_str(args);
    if (args_split.size() != 2) {
        return error(f_ln, "Invalid number of arguments");
    }
    string dest_var = args_split[0];
    string expr = args_split[1];
    if (!does_var_exist(dest_var)) {
        return error(f_ln, "Destination variable doesn't exist");
    }
    
    string tokenisedExpr = tokeniseExpr(f_ln, expr);

    double result = te_interp(tokenisedExpr.c_str(), 0);
    if (isnan(result)) {
        return error(f_ln, "Not a Number (NaN)");
    }
    variables.find(dest_var)->second = to_string(result);
}

bool if_parser(int &f_ln, string expr) {
    // i == 3

    string operandOne = "";
    string operandTwo = "";

    char op_begin[] = {'=', '<', '>', '!'};
    
    bool op_begun = false;
    string full_op = "";

    for (int i = 0; i < expr.size(); i++) {
        bool foundOperator = false;
        for (int j = 0; j < sizeof(op_begin); j++) {
            if (expr[i] == op_begin[j]) {
                op_begun = true;
                foundOperator = true;
                full_op += expr[i];
            }
        }
        if (!op_begun) {
            operandOne += expr[i];
        } else if (op_begun && !foundOperator) {
            operandTwo += expr[i];
        }
    }

    double _dblOpOne, _dblOpTwo;

    operandOne = trim(operandOne);
    operandTwo = trim(operandTwo);
    full_op = trim(full_op);

    if (operandOne == "" || operandTwo == "" || full_op == "") {
        error(f_ln, "Malformed expression");
        return NULL;
    }

    // check the operator
    string valid_ops[] = {"==", "<=", ">=", "!=", "<", ">"};
    bool valid_op_found = false;
    for (int i = 0; i < sizeof(valid_ops)/sizeof(valid_ops[0]); i++) {
        if (valid_ops[i] == full_op) {
            valid_op_found = true;
        }
    }
    if (!valid_op_found) {
        error(f_ln, "Invalid operator");
        return NULL;
    }

    bool isFullyNum = true;

    for (int i = 0; i < operandOne.size(); i++) {
        if (!isdigit(operandOne[i])) {
            isFullyNum = false;
        }
    }
    for (int i = 0; i < operandTwo.size(); i++) {
        if (!isdigit(operandTwo[i])) {
            isFullyNum = false;
        }
    }

    _dblOpOne = te_interp(operandOne.c_str(), 0);
    _dblOpTwo = te_interp(operandTwo.c_str(), 0);

    bool maths_parsed = false;

    if (!isnan(_dblOpOne) && !isnan(_dblOpTwo)) {
        isFullyNum = true;
        maths_parsed = true;
    }

    if (full_op == ">=" || full_op == "<=" || full_op == ">" || full_op == "<") {
        if (!isFullyNum) {
            error(f_ln, "Operand not suited for operator");
            return NULL;
        } else if (!maths_parsed) {
            _dblOpOne = stod(operandOne);
            _dblOpTwo = stod(operandTwo);
        }
    }

    bool expr_res = NULL;

    if (full_op == "==") {
        if (isFullyNum) {
            expr_res = (_dblOpOne == _dblOpTwo);
        } else {
            expr_res = (operandOne == operandTwo);
        }
    } else if (full_op == "!=") {
        if (isFullyNum) {
            expr_res = (_dblOpOne != _dblOpTwo);
        } else {
            expr_res = (operandOne != operandTwo);
        }
    } else if (full_op == "<=") {
        expr_res = (_dblOpOne <= _dblOpTwo);
    } else if (full_op == ">=") {
        expr_res = (_dblOpOne >= _dblOpTwo);
    } else if (full_op == "<") {
        expr_res = (_dblOpOne < _dblOpTwo);
    } else if (full_op == ">") {
        expr_res = (_dblOpOne > _dblOpTwo);
    }

    return expr_res;
}

/*
if_true [COND], [CMD]
if_false [COND], [CMD]
*/
void cmd_if(int &f_ln, string args, bool check = false) {
    string expr, cmd;
    bool end_of_expr = false;
    for (int i = 0; i < args.size(); i++) {
        if (end_of_expr) {
            cmd += args[i];
        } else {
            if (args[i] == ',') {
                end_of_expr = true;
            } else {
                expr += args[i];
            }
        }
    }
    
    expr = trim(expr);
    cmd = trim(cmd);

    expr = tokeniseExpr(f_ln, expr);
    bool result = if_parser(f_ln, expr);

    if (result == check) {
        // result = true for if_true, and false for if_false
        // parse the cmd
        parse_ln(f_ln, cmd, true);
    }
}

void parse_ln(int &f_line, string line, bool inline_parse = false) {
    line = trim(line);
    if (line.size() == 0) return;
    if (line[0] == ';') return;     // Comments begin with ;
    
    string command;
    string args;

    for (int i = 0; i < line.size(); i++) {
        if (line[i] == ' ') {
            args = line.substr(i+1, (line.size() - i));
            break;
        }
        command += line[i];
    }

    bool isCommandValid = false;
    for (int i = 0; i < sizeof(keyWords)/sizeof(keyWords[0]); i++) {
        if (command == keyWords[i]) {
            isCommandValid = true;
        }
    }

    if (!isCommandValid) {
        return error(f_line, "Invalid command, " + command);
    }
    
    if (command == "make_var") {
        cmd_make_var(f_line, args);
    } else if (command == "set_var") {
        cmd_set_var(f_line, args);
    } else if (command == "print") {
        cmd_print(f_line, args);
    } else if (command == "print_no_linebreak") {
        cmd_print(f_line, args, false);
    } else if (command == "get_var_pos") {
        cmd_get_var_pos(f_line, args);
    } else if (command == "::") {
        if (inline_parse) {
            cout << "[SejScript Warning] CodePoints may not work with inline parsing" << endl;
        }
        add_codepoint(f_line, args);
    } else if (command == "goto") {
        cmd_goto(f_line, args);
    } else if (command == "clear_scr") {
        cmd_clear_scr(f_line, args);
    } else if (command == "get_input") {
        cmd_get_input(f_line, args);
    } else if (command == "exit") {
        exit(0);
    } else if (command == "wait") {
        cmd_wait(f_line, args);
    } else if (command == "eval") {
        args = tokenise(f_line, args);
        parse_ln(f_line, args, true);
    } else if (command == "toggle_continue_on_err") {
        continueOnErr = !continueOnErr;
    } else if (command == "let") {
        cmd_let(f_line, args);
    } else if (command == "if_true") {
        cmd_if(f_line, args, true);
    } else if (command == "if_false") {
        cmd_if(f_line, args, false);
    }
}

bool read_nth_line(string &filename, int N, string &line)
{
   ifstream in(filename.c_str());

   string s;
   s.reserve(22000);    

   for(int i = 0; i < N; ++i)
       getline(in, s);

   bool _read = (bool)getline(in,s);
   line = s;
   return _read; 
}

void parse_file(string f_name) {
    filesRunning.push_back(f_name);

    int f_line = 0;
    variables.clear();
    codepoints.clear();

    ifstream file(f_name);

    if (file.is_open()) {
        string line;
        while (read_nth_line(f_name, f_line, line)) {
            f_line++;
            parse_ln(f_line, line);
        }
        file.close();
    } else {
        return error(-1, "Cannot open file for reading");
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        error(-1, "Invalid number of arguments");
        return 1;
    }

    string f_name = argv[1];
    parse_file(f_name);
}