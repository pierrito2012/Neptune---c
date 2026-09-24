#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cctype>
#include <unordered_set>
#include <cmath>

using namespace std;

// ============================================================
// VALUE
// ============================================================

struct Value {

    enum Type {
        NUMBER,
        STRING,
        TABLE,
        NONE
    };

    Type type = NONE;

    double number = 0;
    string str;

    unordered_map<string, Value> table;

    Value() {}

    Value(double n) {
        type = NUMBER;
        number = n;
    }

    Value(const string& s) {
        type = STRING;
        str = s;
    }

    static Value makeTable() {
        Value v;
        v.type = TABLE;
        return v;
    }
};


// ============================================================
// FUNCTION
// ============================================================

struct Function {

    vector<string> parameters;
    vector<string> body;
};


// ============================================================
// RETURN
// ============================================================

struct ReturnSignal {

    Value value;

    ReturnSignal(const Value& v)
        : value(v) {}
};


// ============================================================
// NEPTUNE
// ============================================================

class Neptune {

private:

    unordered_map<string, Value> variables;

    unordered_map<string, Function> functions;

    // Imported .nep libraries: alias -> function name -> function
    unordered_map<string, unordered_map<string, Function>> libraries;
    unordered_set<string> loadedLibraries;


    // ========================================================
    // TRIM
    // ========================================================

    string trim(const string& s) {

        size_t first =
            s.find_first_not_of(
                " \t\r\n"
            );

        if (first == string::npos)
            return "";

        size_t last =
            s.find_last_not_of(
                " \t\r\n"
            );

        return s.substr(
            first,
            last - first + 1
        );
    }


    // ========================================================
    // NUMBER
    // ========================================================

    bool isNumber(
        const string& s
    ) {

        if (s.empty())
            return false;

        size_t i = 0;

        bool dot = false;

        if (s[0] == '-')
            i = 1;

        if (i >= s.size())
            return false;

        for (; i < s.size(); i++) {

            if (s[i] == '.') {

                if (dot)
                    return false;

                dot = true;
            }

            else if (
                !isdigit(
                    (unsigned char)s[i]
                )
            ) {

                return false;
            }
        }

        return true;
    }


    // ========================================================
    // VALUE -> STRING
    // ========================================================

    string valueToString(
        const Value& v
    ) {

        if (v.type ==
            Value::NUMBER) {

            if (
                v.number ==
                (long long)v.number
            ) {

                return to_string(
                    (long long)v.number
                );
            }

            ostringstream out;

            out << v.number;

            return out.str();
        }


        if (v.type ==
            Value::STRING) {

            return v.str;
        }


        if (v.type ==
            Value::TABLE) {

            string result = "{ ";

            bool first = true;

            for (
                const auto& item :
                v.table
            ) {

                if (!first)
                    result += ", ";

                result += item.first;
                result += " = ";
                result += valueToString(
                    item.second
                );

                first = false;
            }

            result += " }";

            return result;
        }


        return "";
    }


    // ========================================================
    // SPLIT ARGUMENTS
    // ========================================================

    vector<string> splitArguments(
        const string& text
    ) {

        vector<string> result;

        string current;

        int parentheses = 0;
        int braces = 0;

        bool stringMode = false;


        for (char c : text) {

            if (c == '"') {

                stringMode =
                    !stringMode;

                current += c;

                continue;
            }


            if (!stringMode) {

                if (c == '(')
                    parentheses++;

                else if (c == ')')
                    parentheses--;

                else if (c == '{')
                    braces++;

                else if (c == '}')
                    braces--;


                if (
                    c == ',' &&
                    parentheses == 0 &&
                    braces == 0
                ) {

                    result.push_back(
                        trim(current)
                    );

                    current.clear();

                    continue;
                }
            }


            current += c;
        }


        if (!trim(current).empty()) {

            result.push_back(
                trim(current)
            );
        }


        return result;
    }


    // ========================================================
    // FIND OPERATOR
    // ========================================================

    int findOperator(
        const string& expression,
        const vector<string>& operators
    ) {

        int parentheses = 0;

        bool stringMode = false;


        for (
            int i =
                (int)expression.size() - 1;
            i >= 0;
            i--
        ) {

            char c =
                expression[i];


            if (c == '"') {

                stringMode =
                    !stringMode;

                continue;
            }


            if (stringMode)
                continue;


            if (c == ')')
                parentheses++;

            else if (c == '(')
                parentheses--;


            if (parentheses != 0)
                continue;


            for (
                const string& op :
                operators
            ) {

                int start =
                    i -
                    (int)op.size() +
                    1;


                if (start >= 0) {

                    if (
                        expression.substr(
                            start,
                            op.size()
                        ) == op
                    ) {

                        return start;
                    }
                }
            }
        }


        return -1;
    }


    // ========================================================
    // COMPARE
    // ========================================================

    bool compare(
        const Value& a,
        const Value& b,
        const string& op
    ) {

        if (
            a.type ==
                Value::NUMBER &&
            b.type ==
                Value::NUMBER
        ) {

            if (op == "==")
                return a.number ==
                       b.number;

            if (op == "!=")
                return a.number !=
                       b.number;

            if (op == ">")
                return a.number >
                       b.number;

            if (op == "<")
                return a.number <
                       b.number;

            if (op == ">=")
                return a.number >=
                       b.number;

            if (op == "<=")
                return a.number <=
                       b.number;
        }


        string x =
            valueToString(a);

        string y =
            valueToString(b);


        if (op == "==")
            return x == y;

        if (op == "!=")
            return x != y;


        return false;
    }


    // ========================================================
    // EVALUATE
    // ========================================================

    Value evaluate(
        string expression
    ) {

        expression =
            trim(expression);


        if (expression.empty())
            return Value();


        // ----------------------------------------------------
        // STRING
        // ----------------------------------------------------

        if (
            expression.size() >= 2 &&
            expression.front() == '"' &&
            expression.back() == '"'
        ) {

            return Value(
                expression.substr(
                    1,
                    expression.size() - 2
                )
            );
        }


        // ----------------------------------------------------
        // NUMBER
        // ----------------------------------------------------

        if (isNumber(expression)) {

            return Value(
                stod(expression)
            );
        }


        // ----------------------------------------------------
        // PARENTHESES
        // ----------------------------------------------------

        if (
            expression.size() >= 2 &&
            expression.front() == '(' &&
            expression.back() == ')'
        ) {

            return evaluate(
                expression.substr(
                    1,
                    expression.size() - 2
                )
            );
        }


        // ----------------------------------------------------
        // COMPARISON
        // ----------------------------------------------------

        vector<string> comparisonOps = {
            "==",
            "!=",
            ">=",
            "<=",
            ">",
            "<"
        };


        int pos =
            findOperator(
                expression,
                comparisonOps
            );


        if (pos != -1) {

            string op;


            for (
                const string& candidate :
                comparisonOps
            ) {

                if (
                    expression.substr(
                        pos,
                        candidate.size()
                    ) == candidate
                ) {

                    op = candidate;

                    break;
                }
            }


            Value left =
                evaluate(
                    expression.substr(
                        0,
                        pos
                    )
                );


            Value right =
                evaluate(
                    expression.substr(
                        pos +
                        op.size()
                    )
                );


            return Value(
                compare(
                    left,
                    right,
                    op
                )
                ? 1
                : 0
            );
        }


        // ----------------------------------------------------
        // + -
        // ----------------------------------------------------

        vector<string> addOps = {
            "+",
            "-"
        };


        pos =
            findOperator(
                expression,
                addOps
            );


        if (pos > 0) {

            string op =
                expression.substr(
                    pos,
                    1
                );


            Value left =
                evaluate(
                    expression.substr(
                        0,
                        pos
                    )
                );


            Value right =
                evaluate(
                    expression.substr(
                        pos + 1
                    )
                );


            if (
                left.type ==
                    Value::NUMBER &&
                right.type ==
                    Value::NUMBER
            ) {

                if (op == "+") {

                    return Value(
                        left.number +
                        right.number
                    );
                }


                return Value(
                    left.number -
                    right.number
                );
            }
        }


        // ----------------------------------------------------
        // * /
        // ----------------------------------------------------

        vector<string> mulOps = {
            "*",
            "/",
            "%"
        };


        pos =
            findOperator(
                expression,
                mulOps
            );


        if (pos > 0) {

            string op =
                expression.substr(
                    pos,
                    1
                );


            Value left =
                evaluate(
                    expression.substr(
                        0,
                        pos
                    )
                );


            Value right =
                evaluate(
                    expression.substr(
                        pos + 1
                    )
                );


            if (
                left.type ==
                    Value::NUMBER &&
                right.type ==
                    Value::NUMBER
            ) {

                if (op == "*") {

                    return Value(
                        left.number *
                        right.number
                    );
                }

                if (op == "%") {
                    if (right.number == 0) {
                        cerr << "Neptune: modulo by zero.\n";
                        return Value();
                    }

                    return Value(
                        fmod(left.number, right.number)
                    );
                }

                if (right.number == 0) {

                    cerr
                        << "Neptune: division by zero.\n";

                    return Value();
                }


                return Value(
                    left.number /
                    right.number
                );
            }
        }


        // ----------------------------------------------------
        // FUNCTION CALL / LIBRARY.FUNCTION CALL
        // ----------------------------------------------------

        size_t open = expression.find('(');

        if (
            open != string::npos &&
            expression.back() == ')'
        ) {
            string functionName =
                trim(expression.substr(0, open));

            string inside =
                expression.substr(
                    open + 1,
                    expression.size() - open - 2
                );

            vector<string> args = splitArguments(inside);

            const Function* function = nullptr;

            // Normal function: teste(...)
            auto normalFunction = functions.find(functionName);
            if (normalFunction != functions.end()) {
                function = &normalFunction->second;
            }

            // Imported function: biblioteca.funcao(...)
            if (function == nullptr) {
                size_t dot = functionName.find('.');

                if (dot != string::npos) {
                    string libraryName = trim(functionName.substr(0, dot));
                    string memberName = trim(functionName.substr(dot + 1));

                    auto library = libraries.find(libraryName);
                    if (library != libraries.end()) {
                        auto member = library->second.find(memberName);
                        if (member != library->second.end()) {
                            function = &member->second;
                        }
                        else {
                            cerr << "Neptune: unknown function in library: "
                                 << functionName << "\n";
                            return Value();
                        }
                    }
                }
            }

            if (function != nullptr) {
                unordered_map<string, pair<bool, Value>> savedParameters;

                for (size_t i = 0; i < function->parameters.size(); i++) {
                    string parameter = trim(function->parameters[i]);

                    bool existed =
                        variables.find(parameter) != variables.end();

                    Value oldValue;
                    if (existed)
                        oldValue = variables[parameter];

                    savedParameters[parameter] = { existed, oldValue };

                    if (i < args.size()) {
                        string argument = trim(args[i]);

                        auto variable = variables.find(argument);

                        if (variable != variables.end())
                            variables[parameter] = variable->second;
                        else
                            variables[parameter] = evaluate(argument);
                    }
                    else {
                        variables[parameter] = Value();
                    }
                }

                Value returnValue;

                try {
                    executeBlock(function->body);
                }
                catch (const ReturnSignal& signal) {
                    returnValue = signal.value;
                }

                for (const auto& item : savedParameters) {
                    const string& name = item.first;
                    bool existed = item.second.first;

                    if (existed)
                        variables[name] = item.second.second;
                    else
                        variables.erase(name);
                }

                return returnValue;
            }

            // If it looked like a library call but the library was not loaded,
            // give a specific error instead of treating it as a variable.
            if (functionName.find('.') != string::npos) {
                cerr << "Neptune: unknown library function: "
                     << functionName << "\n";
                return Value();
            }

            cerr
                << "Neptune: unknown function: "
                << functionName
                << "\n";

            return Value();
        }

        // ----------------------------------------------------
        // VARIABLE
        // ----------------------------------------------------

        auto variable =
            variables.find(
                expression
            );


        if (
            variable !=
            variables.end()
        ) {

            return variable->second;
        }


        cerr
            << "Neptune: unknown value: "
            << expression
            << "\n";


        return Value();
    }


    // ========================================================
    // PARSE ONE FUNCTION
    // ========================================================

    bool parseOneFunction(
        const vector<string>& lines,
        size_t& i,
        string& name,
        Function& function
    ) {
        string line = trim(lines[i]);

        if (line.rfind("efunc ", 0) != 0)
            return false;

        size_t open = line.find('(');
        size_t close = line.rfind(')');

        if (open == string::npos || close == string::npos || close < open) {
            cerr << "Neptune: invalid function declaration.\n";
            return false;
        }

        name = trim(line.substr(6, open - 6));
        string parameterText = line.substr(open + 1, close - open - 1);

        function = Function();
        function.parameters = splitArguments(parameterText);

        for (string& parameter : function.parameters)
            parameter = trim(parameter);

        int depth = 1;
        i++;

        while (i < lines.size() && depth > 0) {
            string current = trim(lines[i]);

            if (current.rfind("efunc ", 0) == 0 ||
                current.rfind("if(", 0) == 0 ||
                current.rfind("while(", 0) == 0 ||
                current.rfind("for(", 0) == 0) {

                depth++;
                function.body.push_back(lines[i]);
            }
            else if (current == "end") {
                depth--;

                if (depth > 0)
                    function.body.push_back(lines[i]);
            }
            else {
                function.body.push_back(lines[i]);
            }

            i++;
        }

        if (depth != 0) {
            cerr << "Neptune: missing end for function: " << name << "\n";
            return false;
        }

        return true;
    }


    // ========================================================
    // PARSE NORMAL FUNCTIONS
    // ========================================================

    void parseFunctions(
        const vector<string>& lines
    ) {
        for (size_t i = 0; i < lines.size(); i++) {
            string line = trim(lines[i]);

            if (line.rfind("efunc ", 0) != 0)
                continue;

            string name;
            Function function;

            if (parseOneFunction(lines, i, name, function))
                functions[name] = function;
        }
    }


    // ========================================================
    // LOAD /Include(file.nep)
    // ========================================================

    bool loadLibrary(const string& filename) {
        if (loadedLibraries.find(filename) != loadedLibraries.end())
            return true;

        ifstream file(filename);

        if (!file) {
            cerr << "Neptune: could not include file: "
                 << filename << "\n";
            return false;
        }

        vector<string> lines;
        string line;

        while (getline(file, line))
            lines.push_back(line);

        string libraryName;

        for (size_t i = 0; i < lines.size(); i++) {
            string current = trim(lines[i]);

            if (current.rfind("/define(", 0) == 0 &&
                current.back() == ')') {

                size_t open = current.find('(');
                libraryName = trim(
                    current.substr(open + 1, current.size() - open - 2)
                );

                break;
            }
        }

        if (libraryName.empty()) {
            cerr << "Neptune: library has no /define(name): "
                 << filename << "\n";
            return false;
        }

        if (libraries.find(libraryName) != libraries.end()) {
            cerr << "Neptune: library already defined: "
                 << libraryName << "\n";
            return false;
        }

        unordered_map<string, Function> libraryFunctions;

        for (size_t i = 0; i < lines.size(); i++) {
            string current = trim(lines[i]);

            if (current.rfind("efunc ", 0) != 0)
                continue;

            string name;
            Function function;

            if (parseOneFunction(lines, i, name, function))
                libraryFunctions[name] = function;
        }

        libraries[libraryName] = libraryFunctions;
        loadedLibraries.insert(filename);

        return true;
    }


    // ========================================================
    // PROCESS INCLUDES
    // ========================================================

    void processIncludes(
        const vector<string>& lines
    ) {
        for (const string& raw : lines) {
            string line = trim(raw);

            if (line.rfind("/Include(", 0) != 0 ||
                line.back() != ')')
                continue;

            size_t open = line.find('(');
            string filename = trim(
                line.substr(open + 1, line.size() - open - 2)
            );

            if (filename.size() >= 2 &&
                filename.front() == '"' &&
                filename.back() == '"') {
                filename = filename.substr(
                    1,
                    filename.size() - 2
                );
            }

            loadLibrary(filename);
        }
    }

    // ========================================================
    // FIND MATCHING END
    // ========================================================

    size_t findMatchingEnd(
        const vector<string>& lines,
        size_t start
    ) {

        int depth = 1;


        for (
            size_t i = start;
            i < lines.size();
            i++
        ) {

            string line =
                trim(lines[i]);


            if (
                line.rfind(
                    "if(",
                    0
                ) == 0 ||

                line.rfind(
                    "while(",
                    0
                ) == 0 ||

                line.rfind(
                    "for(",
                    0
                ) == 0
            ) {

                depth++;
            }


            else if (
                line == "end"
            ) {

                depth--;

                if (depth == 0)
                    return i;
            }
        }


        return lines.size();
    }


    // ========================================================
    // EXECUTE IF
    // ========================================================

    size_t executeIf(
        const vector<string>& lines,
        size_t index
    ) {

        string line =
            trim(lines[index]);


        size_t doPosition =
            line.rfind(")do");


        if (
            doPosition ==
            string::npos
        ) {

            cerr
                << "Neptune: invalid if syntax.\n";

            return index + 1;
        }


        string condition =
            line.substr(
                3,
                doPosition - 3
            );


        vector<string> trueBlock;
        vector<string> falseBlock;


        bool inElse = false;

        int depth = 1;


        size_t i =
            index + 1;


        while (
            i < lines.size() &&
            depth > 0
        ) {

            string current =
                trim(lines[i]);


            // --------------------------------------------
            // Nested block
            // --------------------------------------------

            if (
                current.rfind(
                    "if(",
                    0
                ) == 0 ||

                current.rfind(
                    "while(",
                    0
                ) == 0 ||

                current.rfind(
                    "for(",
                    0
                ) == 0
            ) {

                depth++;


                if (inElse)
                    falseBlock.push_back(
                        lines[i]
                    );
                else
                    trueBlock.push_back(
                        lines[i]
                    );


                i++;

                continue;
            }


            // --------------------------------------------
            // END
            // --------------------------------------------

            if (
                current == "end"
            ) {

                depth--;


                if (depth == 0)
                    break;


                if (inElse)
                    falseBlock.push_back(
                        lines[i]
                    );
                else
                    trueBlock.push_back(
                        lines[i]
                    );


                i++;

                continue;
            }


            // --------------------------------------------
            // ELSE
            //
            // Only an ELSE belonging to THIS IF.
            // --------------------------------------------

            if (
                current == "else" &&
                depth == 1
            ) {

                inElse = true;

                i++;

                continue;
            }


            if (inElse)
                falseBlock.push_back(
                    lines[i]
                );
            else
                trueBlock.push_back(
                    lines[i]
                );


            i++;
        }


        Value result =
            evaluate(
                condition
            );


        bool conditionTrue =
            false;


        if (
            result.type ==
            Value::NUMBER
        ) {

            conditionTrue =
                result.number != 0;
        }


        if (conditionTrue) {

            executeBlock(
                trueBlock
            );
        }

        else {

            executeBlock(
                falseBlock
            );
        }


        return i + 1;
    }


    // ========================================================
    // EXECUTE WHILE
    // ========================================================

    size_t executeWhile(
        const vector<string>& lines,
        size_t index
    ) {

        string line =
            trim(lines[index]);


        size_t doPosition =
            line.rfind(")do");


        if (
            doPosition ==
            string::npos
        ) {

            cerr
                << "Neptune: invalid while syntax.\n";

            return index + 1;
        }


        string condition =
            line.substr(
                6,
                doPosition - 6
            );


        size_t end =
            findMatchingEnd(
                lines,
                index + 1
            );


        vector<string> body;


        for (
            size_t i = index + 1;
            i < end;
            i++
        ) {

            body.push_back(
                lines[i]
            );
        }


        int safety = 0;


        while (true) {

            Value result =
                evaluate(
                    condition
                );


            if (
                result.type !=
                Value::NUMBER
            )
                break;


            if (
                result.number == 0
            )
                break;


            executeBlock(
                body
            );


            safety++;


            if (
                safety > 1000000
            ) {

                cerr
                    << "Neptune: while exceeded the safety limit.\n";

                break;
            }
        }


        return end + 1;
    }


    // ========================================================
    // EXECUTE FOR
    //
    // Syntax:
    //
    // for(i = 0, 10, 1)do
    //
    // Same basic idea as Lua-style numeric for,
    // using Neptune syntax.
    // ========================================================

    size_t executeFor(
        const vector<string>& lines,
        size_t index
    ) {

        string line =
            trim(lines[index]);


        size_t doPosition =
            line.rfind(")do");


        if (
            doPosition ==
            string::npos
        ) {

            cerr
                << "Neptune: invalid for syntax.\n";

            return index + 1;
        }


        string inside =
            line.substr(
                4,
                doPosition - 4
            );


        vector<string> args =
            splitArguments(
                inside
            );


        if (
            args.size() < 2 ||
            args.size() > 3
        ) {

            cerr
                << "Neptune: invalid for syntax.\n";

            return index + 1;
        }


        string initialization =
            trim(args[0]);


        size_t equal =
            initialization.find('=');


        if (
            equal == string::npos
        ) {

            cerr
                << "Neptune: invalid for initialization.\n";

            return index + 1;
        }


        string variableName =
            trim(
                initialization.substr(
                    0,
                    equal
                )
            );


        double start =
            evaluate(
                initialization.substr(
                    equal + 1
                )
            ).number;


        double finish =
            evaluate(
                args[1]
            ).number;


        double step = 1;


        if (
            args.size() == 3
        ) {

            step =
                evaluate(
                    args[2]
                ).number;
        }


        if (step == 0) {

            cerr
                << "Neptune: for step cannot be zero.\n";

            return index + 1;
        }


        size_t end =
            findMatchingEnd(
                lines,
                index + 1
            );


        vector<string> body;


        for (
            size_t i = index + 1;
            i < end;
            i++
        ) {

            body.push_back(
                lines[i]
            );
        }


        if (step > 0) {

            for (
                double n = start;
                n <= finish;
                n += step
            ) {

                variables[
                    variableName
                ] =
                    Value(n);


                executeBlock(
                    body
                );
            }
        }

        else {

            for (
                double n = start;
                n >= finish;
                n += step
            ) {

                variables[
                    variableName
                ] =
                    Value(n);


                executeBlock(
                    body
                );
            }
        }


        return end + 1;
    }


    // ========================================================
    // EXECUTE LINE
    // ========================================================

    void executeLine(
        string line
    ) {

        line =
            trim(line);


        if (line.empty())
            return;

        if (line.rfind("/Include(", 0) == 0 ||
            line.rfind("/define(", 0) == 0)
            return;

        // ----------------------------------------------------
        // VAR
        // ----------------------------------------------------

        if (
            line.rfind(
                "var ",
                0
            ) == 0
        ) {

            string rest =
                trim(
                    line.substr(4)
                );


            size_t equal =
                rest.find('=');


            if (
                equal ==
                string::npos
            ) {

                cerr
                    << "Neptune: invalid var syntax.\n";

                return;
            }


            string name =
                trim(
                    rest.substr(
                        0,
                        equal
                    )
                );


            string expression =
                trim(
                    rest.substr(
                        equal + 1
                    )
                );


            variables[name] =
                evaluate(
                    expression
                );


            return;
        }


        // ----------------------------------------------------
        // RETURN
        // ----------------------------------------------------

        if (
            line.rfind(
                "return ",
                0
            ) == 0
        ) {

            string expression =
                trim(
                    line.substr(7)
                );

            throw ReturnSignal(
                evaluate(
                    expression
                )
            );
        }

        if (
            line.rfind("return(", 0) == 0 &&
            line.back() == ')'
        ) {
            string expression =
                line.substr(7, line.size() - 8);

            throw ReturnSignal(
                evaluate(expression)
            );
        }


        // ----------------------------------------------------
        // ECHO
        // ----------------------------------------------------

        if (
            line.rfind(
                "echo(",
                0
            ) == 0 &&
            line.back() == ')'
        ) {

            string inside =
                line.substr(
                    5,
                    line.size() - 6
                );


            vector<string> args =
                splitArguments(
                    inside
                );


            for (
                size_t i = 0;
                i < args.size();
                i++
            ) {

                cout
                    << valueToString(
                        evaluate(
                            args[i]
                        )
                    );


                if (
                    i + 1 <
                    args.size()
                )
                    cout << " ";
            }


            cout << "\n";

            return;
        }


        // ----------------------------------------------------
        // CIN
        // ----------------------------------------------------

        if (
            line.rfind(
                "cin(",
                0
            ) == 0 &&
            line.back() == ')'
        ) {

            string name =
                trim(
                    line.substr(
                        4,
                        line.size() - 5
                    )
                );


            string input;


            getline(
                cin,
                input
            );


            input =
                trim(input);


            if (
                isNumber(input)
            ) {

                variables[name] =
                    Value(
                        stod(input)
                    );
            }

            else {

                variables[name] =
                    Value(input);
            }


            return;
        }


        // ----------------------------------------------------
        // ASSIGNMENT
        // ----------------------------------------------------

        size_t equal =
            line.find('=');


        if (
            equal !=
            string::npos &&
            line.find("==") ==
                string::npos
        ) {

            string name =
                trim(
                    line.substr(
                        0,
                        equal
                    )
                );


            string expression =
                trim(
                    line.substr(
                        equal + 1
                    )
                );


            if (
                !name.empty()
            ) {

                variables[name] =
                    evaluate(
                        expression
                    );

                return;
            }
        }


        // ----------------------------------------------------
        // FUNCTION CALL
        // ----------------------------------------------------

        evaluate(line);
    }


    // ========================================================
    // EXECUTE BLOCK
    // ========================================================

    void executeBlock(
        const vector<string>& lines
    ) {

        size_t i = 0;


        while (
            i < lines.size()
        ) {

            string line =
                trim(lines[i]);


            if (
                line.empty()
            ) {

                i++;

                continue;
            }


            // ------------------------------------------------
            // IF
            // ------------------------------------------------

            if (
                line.rfind(
                    "if(",
                    0
                ) == 0
            ) {

                i =
                    executeIf(
                        lines,
                        i
                    );

                continue;
            }


            // ------------------------------------------------
            // WHILE
            // ------------------------------------------------

            if (
                line.rfind(
                    "while(",
                    0
                ) == 0
            ) {

                i =
                    executeWhile(
                        lines,
                        i
                    );

                continue;
            }


            // ------------------------------------------------
            // FOR
            // ------------------------------------------------

            if (
                line.rfind(
                    "for(",
                    0
                ) == 0
            ) {

                i =
                    executeFor(
                        lines,
                        i
                    );

                continue;
            }


            // ------------------------------------------------
            // ELSE / END
            //
            // They should NEVER reach here.
            // They belong to the block currently being parsed.
            // ------------------------------------------------

            if (
                line == "else" ||
                line == "end"
            ) {

                i++;

                continue;
            }


            // ------------------------------------------------
            // NORMAL LINE
            // ------------------------------------------------

            executeLine(line);


            i++;
        }
    }


    // ========================================================
    // EXECUTE PROGRAM
    // ========================================================

    void executeProgram(
        const vector<string>& lines
    ) {

        size_t i = 0;

        while (i < lines.size()) {

            string line = trim(lines[i]);

            if (line.empty()) {
                i++;
                continue;
            }

            // ------------------------------------------------
            // FUNCTION DEFINITION
            // Skip the complete function, including nested
            // if/while/for blocks.
            // ------------------------------------------------

            if (line.rfind("efunc ", 0) == 0) {

                int depth = 1;
                i++;

                while (i < lines.size() && depth > 0) {

                    string current = trim(lines[i]);

                    if (current.rfind("efunc ", 0) == 0 ||
                        current.rfind("if(", 0) == 0 ||
                        current.rfind("while(", 0) == 0 ||
                        current.rfind("for(", 0) == 0) {

                        depth++;
                    }
                    else if (current == "end") {
                        depth--;
                    }

                    i++;
                }

                continue;
            }

            // ------------------------------------------------
            // VARTABLE
            // ------------------------------------------------

            if (line.rfind("vartable ", 0) == 0) {

                string name = trim(line.substr(9));

                if (!name.empty() && name.back() == '{') {
                    name = trim(name.substr(0, name.size() - 1));
                }

                Value table = Value::makeTable();
                i++;

                while (i < lines.size()) {

                    string current = trim(lines[i]);

                    if (current == "}")
                        break;

                    if (current.rfind("var ", 0) == 0) {

                        string rest = trim(current.substr(4));
                        size_t equal = rest.find('=');

                        if (equal != string::npos) {

                            string key = trim(rest.substr(0, equal));
                            string expression = trim(rest.substr(equal + 1));

                            table.table[key] = evaluate(expression);
                        }
                    }

                    i++;
                }

                variables[name] = table;
                i++;
                continue;
            }

            // ------------------------------------------------
            // TOP-LEVEL BLOCKS
            // ------------------------------------------------

            if (line.rfind("if(", 0) == 0) {
                i = executeIf(lines, i);
                continue;
            }

            if (line.rfind("while(", 0) == 0) {
                i = executeWhile(lines, i);
                continue;
            }

            if (line.rfind("for(", 0) == 0) {
                i = executeFor(lines, i);
                continue;
            }

            // These are only meaningful inside a block. If they
            // reach the top level, report a useful error instead
            // of sending them to evaluate().
            if (line == "else") {
                cerr << "Neptune: unexpected else.\n";
                i++;
                continue;
            }

            if (line == "end") {
                cerr << "Neptune: unexpected end.\n";
                i++;
                continue;
            }

            executeLine(line);
            i++;
        }
    }


public:

    // ========================================================
    // RUN
    // ========================================================

    void run(
        const vector<string>& lines
    ) {

        processIncludes(
            lines
        );

        parseFunctions(
            lines
        );


        executeProgram(
            lines
        );
    }
};


// ============================================================
// MAIN
// ============================================================

int main(
    int argc,
    char* argv[]
) {

    Neptune neptune;


    // ========================================================
    // TERMINAL MODE
    //
    // neptune --c
    // ========================================================

    if (
        argc >= 2 &&
        string(argv[1]) == "--c"
    ) {

        cout
            << "Neptune C--\n";

        cout
            << "Interactive mode.\n";

        cout
            << "Type 'exit' to quit.\n\n";


        string line;


        while (true) {

            cout << ">>> ";


            if (
                !getline(
                    cin,
                    line
                )
            )
                break;


            if (
                line == "exit"
            )
                break;


            if (
                line.empty()
            )
                continue;


            vector<string> code;

            code.push_back(
                line
            );


            neptune.run(
                code
            );
        }


        return 0;
    }


    // ========================================================
    // .NEP FILE
    //
    // neptune jogo.nep
    // ========================================================

    if (
        argc >= 2
    ) {

        string filename =
            argv[1];


        if (
            filename.size() < 4 ||
            filename.substr(
                filename.size() - 4
            ) != ".nep"
        ) {

            cerr
                << "Neptune: file must have .nep extension.\n";

            return 1;
        }


        ifstream file(
            filename
        );


        if (!file) {

            cerr
                << "Neptune: could not open file "
                << filename
                << ".\n";

            return 1;
        }


        vector<string> lines;

        string line;


        while (
            getline(
                file,
                line
            )
        ) {

            lines.push_back(
                line
            );
        }


        neptune.run(
            lines
        );


        return 0;
    }


    // ========================================================
    // NO ARGUMENT
    // ========================================================

    cout
        << "Neptune C--\n";

    cout
        << "Usage:\n";

    cout
        << "  neptune --c\n";

    cout
        << "  neptune program.nep\n";


    return 0;
}
