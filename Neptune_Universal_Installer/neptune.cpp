#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

struct Value {
    enum Type { NONE, NUMBER, STRING, BOOLEAN, ARRAY, MAP, TABLE, FUNCTION };

    Type type = NONE;
    double number = 0.0;
    string str;
    shared_ptr<vector<Value>> array;
    shared_ptr<unordered_map<string, Value>> fields;
    string functionName;

    Value() = default;
    explicit Value(double n) : type(NUMBER), number(n) {}
    explicit Value(const string& s) : type(STRING), str(s) {}
    explicit Value(const char* s) : type(STRING), str(s) {}

    static Value makeBool(bool b) {
        Value v;
        v.type = BOOLEAN;
        v.number = b ? 1.0 : 0.0;
        return v;
    }

    static Value makeArray() {
        Value v;
        v.type = ARRAY;
        v.array = make_shared<vector<Value>>();
        return v;
    }

    static Value makeMap() {
        Value v;
        v.type = MAP;
        v.fields = make_shared<unordered_map<string, Value>>();
        return v;
    }

    static Value makeTable() {
        Value v;
        v.type = TABLE;
        v.fields = make_shared<unordered_map<string, Value>>();
        return v;
    }

    static Value makeFunction(const string& name) {
        Value v;
        v.type = FUNCTION;
        v.functionName = name;
        return v;
    }
};

struct Function {
    vector<string> parameters;
    vector<string> body;
    string namespaceName;
};

struct StructDefinition {
    vector<pair<string, Value>> fields;
};

struct ReturnSignal { Value value; };
struct BreakSignal {};
struct ContinueSignal {};

struct NeptuneError {
    string message;
};

class JsonParser {
    const string& source;
    size_t pos = 0;

    void skipSpace() {
        while (pos < source.size() && isspace(static_cast<unsigned char>(source[pos]))) ++pos;
    }

    bool consume(char c) {
        skipSpace();
        if (pos < source.size() && source[pos] == c) {
            ++pos;
            return true;
        }
        return false;
    }

    [[noreturn]] void error(const string& message) {
        throw NeptuneError{"JSON error at position " + to_string(pos) + ": " + message};
    }

    string parseString() {
        skipSpace();
        if (pos >= source.size() || source[pos] != '"') error("expected string");
        ++pos;
        string out;
        while (pos < source.size()) {
            char c = source[pos++];
            if (c == '"') return out;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos >= source.size()) error("invalid escape");
            char e = source[pos++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                default: error("unsupported escape");
            }
        }
        error("unterminated string");
    }

    Value parseNumber() {
        skipSpace();
        size_t start = pos;
        if (pos < source.size() && (source[pos] == '-' || source[pos] == '+')) ++pos;
        bool hasDigit = false;
        while (pos < source.size() && isdigit(static_cast<unsigned char>(source[pos]))) {
            hasDigit = true;
            ++pos;
        }
        if (pos < source.size() && source[pos] == '.') {
            ++pos;
            while (pos < source.size() && isdigit(static_cast<unsigned char>(source[pos]))) {
                hasDigit = true;
                ++pos;
            }
        }
        if (pos < source.size() && (source[pos] == 'e' || source[pos] == 'E')) {
            ++pos;
            if (pos < source.size() && (source[pos] == '+' || source[pos] == '-')) ++pos;
            while (pos < source.size() && isdigit(static_cast<unsigned char>(source[pos]))) ++pos;
        }
        if (!hasDigit) error("invalid number");
        return Value(strtod(source.substr(start, pos - start).c_str(), nullptr));
    }

    Value parseValue() {
        skipSpace();
        if (pos >= source.size()) error("unexpected end");

        if (source[pos] == '"') return Value(parseString());
        if (source[pos] == '[') return parseArray();
        if (source[pos] == '{') return parseObject();
        if (source.compare(pos, 4, "true") == 0) {
            pos += 4;
            return Value::makeBool(true);
        }
        if (source.compare(pos, 5, "false") == 0) {
            pos += 5;
            return Value::makeBool(false);
        }
        if (source.compare(pos, 4, "null") == 0) {
            pos += 4;
            return Value();
        }
        return parseNumber();
    }

    Value parseArray() {
        consume('[');
        Value result = Value::makeArray();
        skipSpace();
        if (consume(']')) return result;
        while (true) {
            result.array->push_back(parseValue());
            skipSpace();
            if (consume(']')) return result;
            if (!consume(',')) error("expected ','");
        }
    }

    Value parseObject() {
        consume('{');
        Value result = Value::makeMap();
        skipSpace();
        if (consume('}')) return result;
        while (true) {
            string key = parseString();
            if (!consume(':')) error("expected ':'");
            (*result.fields)[key] = parseValue();
            skipSpace();
            if (consume('}')) return result;
            if (!consume(',')) error("expected ','");
        }
    }

public:
    explicit JsonParser(const string& text) : source(text) {}

    Value parse() {
        Value v = parseValue();
        skipSpace();
        if (pos != source.size()) error("unexpected trailing characters");
        return v;
    }
};

class Neptune {
    using NativeFunction = function<Value(const vector<Value>&)>;

    vector<unordered_map<string, Value>> scopes;
    unordered_map<string, Function> functions;
    unordered_map<string, StructDefinition> structs;
    unordered_map<string, NativeFunction> nativeFunctions;
    unordered_set<string> loadedLibraries;
    vector<string> programArguments;

    string currentNamespace;
    uint64_t randomState;

    static string trim(const string& s) {
        size_t first = s.find_first_not_of(" \t\r\n");
        if (first == string::npos) return "";
        size_t last = s.find_last_not_of(" \t\r\n");
        return s.substr(first, last - first + 1);
    }

    static bool startsWith(const string& s, const string& prefix) {
        return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
    }

    static bool isIdentifier(const string& s) {
        if (s.empty()) return false;
        if (!isalpha(static_cast<unsigned char>(s[0])) && s[0] != '_') return false;
        for (size_t i = 1; i < s.size(); ++i) {
            if (!isalnum(static_cast<unsigned char>(s[i])) && s[i] != '_') return false;
        }
        return true;
    }

    static bool isNumberLiteral(const string& s) {
        if (s.empty()) return false;
        size_t i = 0;
        bool dot = false;
        bool digit = false;
        if (s[i] == '-' || s[i] == '+') ++i;
        for (; i < s.size(); ++i) {
            char c = s[i];
            if (isdigit(static_cast<unsigned char>(c))) {
                digit = true;
            } else if (c == '.' && !dot) {
                dot = true;
            } else {
                return false;
            }
        }
        return digit;
    }

    static string stripComment(const string& line) {
        bool inString = false;
        bool escaped = false;
        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (c == '"' && !escaped) inString = !inString;
            if (!inString && c == '#') return line.substr(0, i);
            escaped = (c == '\\' && !escaped);
            if (c != '\\') escaped = false;
        }
        return line;
    }

    static string valueToString(const Value& v) {
        switch (v.type) {
            case Value::NUMBER: {
                if (v.number == static_cast<long long>(v.number))
                    return to_string(static_cast<long long>(v.number));
                ostringstream out;
                out << setprecision(15) << v.number;
                return out.str();
            }
            case Value::STRING: return v.str;
            case Value::BOOLEAN: return v.number != 0.0 ? "true" : "false";
            case Value::FUNCTION: return v.functionName;
            case Value::ARRAY: {
                string out = "[";
                if (v.array) {
                    for (size_t i = 0; i < v.array->size(); ++i) {
                        if (i) out += ", ";
                        out += valueToString((*v.array)[i]);
                    }
                }
                return out + "]";
            }
            case Value::MAP:
            case Value::TABLE: {
                string out = "{";
                bool first = true;
                if (v.fields) {
                    for (const auto& [key, value] : *v.fields) {
                        if (!first) out += ", ";
                        first = false;
                        out += key + " = " + valueToString(value);
                    }
                }
                return out + "}";
            }
            case Value::NONE: default: return "none";
        }
    }

    static string typeName(const Value& v) {
        switch (v.type) {
            case Value::NUMBER: return "number";
            case Value::STRING: return "string";
            case Value::BOOLEAN: return "boolean";
            case Value::ARRAY: return "array";
            case Value::MAP: return "map";
            case Value::TABLE: return "table";
            case Value::FUNCTION: return "function";
            case Value::NONE: default: return "none";
        }
    }

    Value lookup(const string& name) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return found->second;
        }
        return Value();
    }

    bool exists(const string& name) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
            if (it->find(name) != it->end()) return true;
        return false;
    }

    Value& lookupRef(const string& name) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return found->second;
        }
        return scopes.front()[name];
    }

    void declareVar(const string& name, const Value& value) {
        scopes.back()[name] = value;
    }

    void assignVar(const string& name, const Value& value) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                found->second = value;
                return;
            }
        }
        scopes.front()[name] = value;
    }

    static vector<string> splitArguments(const string& text) {
        vector<string> result;
        string current;
        int parentheses = 0;
        int braces = 0;
        int brackets = 0;
        bool inString = false;
        bool escaped = false;

        for (char c : text) {
            if (c == '"' && !escaped) {
                inString = !inString;
                current += c;
                escaped = false;
                continue;
            }
            if (!inString) {
                if (c == '(') ++parentheses;
                else if (c == ')') --parentheses;
                else if (c == '{') ++braces;
                else if (c == '}') --braces;
                else if (c == '[') ++brackets;
                else if (c == ']') --brackets;
                if (c == ',' && parentheses == 0 && braces == 0 && brackets == 0) {
                    result.push_back(trim(current));
                    current.clear();
                    escaped = false;
                    continue;
                }
            }
            current += c;
            escaped = (c == '\\' && !escaped);
            if (c != '\\') escaped = false;
        }
        if (!trim(current).empty()) result.push_back(trim(current));
        return result;
    }

    static int findOperator(const string& expression, const vector<string>& operators) {
        int parentheses = 0;
        int brackets = 0;
        int braces = 0;
        bool inString = false;
        bool escaped = false;

        for (int i = static_cast<int>(expression.size()) - 1; i >= 0; --i) {
            char c = expression[static_cast<size_t>(i)];
            if (c == '"' && !escaped) inString = !inString;
            if (inString) {
                escaped = (c == '\\' && !escaped);
                if (c != '\\') escaped = false;
                continue;
            }
            if (c == ')') ++parentheses;
            else if (c == '(') --parentheses;
            else if (c == ']') ++brackets;
            else if (c == '[') --brackets;
            else if (c == '}') ++braces;
            else if (c == '{') --braces;
            if (parentheses != 0 || brackets != 0 || braces != 0) continue;

            for (const string& op : operators) {
                int start = i - static_cast<int>(op.size()) + 1;
                if (start >= 0 && expression.compare(static_cast<size_t>(start), op.size(), op) == 0) {
                    return start;
                }
            }
            escaped = false;
        }
        return -1;
    }

    bool compareValues(const Value& a, const Value& b, const string& op) {
        if (a.type == Value::NUMBER && b.type == Value::NUMBER) {
            if (op == "==") return a.number == b.number;
            if (op == "!=") return a.number != b.number;
            if (op == ">") return a.number > b.number;
            if (op == "<") return a.number < b.number;
            if (op == ">=") return a.number >= b.number;
            if (op == "<=") return a.number <= b.number;
        }
        if (a.type == Value::BOOLEAN && b.type == Value::BOOLEAN) {
            if (op == "==") return a.number == b.number;
            if (op == "!=") return a.number != b.number;
        }
        string x = valueToString(a);
        string y = valueToString(b);
        if (op == "==") return x == y;
        if (op == "!=") return x != y;
        return false;
    }

    static bool truthy(const Value& v) {
        if (v.type == Value::BOOLEAN || v.type == Value::NUMBER) return v.number != 0.0;
        if (v.type == Value::STRING) return !v.str.empty();
        if (v.type == Value::ARRAY) return v.array && !v.array->empty();
        if (v.type == Value::MAP || v.type == Value::TABLE) return v.fields && !v.fields->empty();
        return false;
    }

    uint64_t nextRandom() {
        uint64_t x = randomState;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        randomState = x;
        return x * 2685821657736338717ULL;
    }

    long long randomInt(long long minValue, long long maxValue) {
        if (minValue > maxValue) swap(minValue, maxValue);
        uint64_t range = static_cast<uint64_t>(maxValue - minValue) + 1ULL;
        return minValue + static_cast<long long>(nextRandom() % range);
    }

    bool splitIndexChain(const string& expression, string& base, vector<string>& indexes) {
        size_t first = expression.find('[');
        if (first == string::npos) return false;
        base = trim(expression.substr(0, first));
        if (base.empty()) return false;
        size_t p = first;
        while (p < expression.size()) {
            if (expression[p] != '[') return false;
            int depth = 1;
            bool inString = false;
            size_t q = p + 1;
            for (; q < expression.size(); ++q) {
                char c = expression[q];
                if (c == '"') inString = !inString;
                if (inString) continue;
                if (c == '[') ++depth;
                else if (c == ']') {
                    --depth;
                    if (depth == 0) break;
                }
            }
            if (q >= expression.size()) return false;
            indexes.push_back(trim(expression.substr(p + 1, q - p - 1)));
            p = q + 1;
        }
        return p == expression.size();
    }

    Value getIndex(Value root, const vector<string>& indexes) {
        for (const string& indexExpr : indexes) {
            Value index = evaluate(indexExpr);
            if (root.type == Value::ARRAY && index.type == Value::NUMBER) {
                long long i = static_cast<long long>(index.number);
                if (!root.array || i < 0 || i >= static_cast<long long>(root.array->size()))
                    throw NeptuneError{"array index out of range"};
                root = (*root.array)[static_cast<size_t>(i)];
            } else if ((root.type == Value::MAP || root.type == Value::TABLE) && index.type == Value::STRING) {
                if (!root.fields) throw NeptuneError{"invalid map"};
                auto it = root.fields->find(index.str);
                if (it == root.fields->end()) return Value();
                root = it->second;
            } else {
                throw NeptuneError{"invalid index operation"};
            }
        }
        return root;
    }

    bool setIndex(Value& root, const vector<string>& indexes, const Value& value, size_t level = 0) {
        if (level >= indexes.size()) {
            root = value;
            return true;
        }
        Value index = evaluate(indexes[level]);
        if (root.type == Value::ARRAY && index.type == Value::NUMBER) {
            if (!root.array) return false;
            long long i = static_cast<long long>(index.number);
            if (i < 0 || i >= static_cast<long long>(root.array->size())) return false;
            return setIndex((*root.array)[static_cast<size_t>(i)], indexes, value, level + 1);
        }
        if ((root.type == Value::MAP || root.type == Value::TABLE) && index.type == Value::STRING) {
            if (!root.fields) root.fields = make_shared<unordered_map<string, Value>>();
            return setIndex((*root.fields)[index.str], indexes, value, level + 1);
        }
        return false;
    }

    Value getProperty(Value object, const string& key) {
        if ((object.type == Value::MAP || object.type == Value::TABLE) && object.fields) {
            auto it = object.fields->find(key);
            if (it != object.fields->end()) return it->second;
        }
        if (object.type == Value::ARRAY && key == "length") return Value(static_cast<double>(object.array ? object.array->size() : 0));
        if (object.type == Value::STRING && key == "length") return Value(static_cast<double>(object.str.size()));
        return Value();
    }

    bool setProperty(Value& object, const string& key, const Value& value) {
        if (object.type != Value::MAP && object.type != Value::TABLE) return false;
        if (!object.fields) object.fields = make_shared<unordered_map<string, Value>>();
        (*object.fields)[key] = value;
        return true;
    }

    Value callMethod(const string& objectExpr, const string& method, const vector<string>& rawArgs) {
        Value object = evaluate(objectExpr);

        if (method == "length" && rawArgs.empty()) {
            if (object.type == Value::ARRAY) return Value(static_cast<double>(object.array ? object.array->size() : 0));
            if (object.type == Value::STRING) return Value(static_cast<double>(object.str.size()));
            if (object.type == Value::MAP || object.type == Value::TABLE) return Value(static_cast<double>(object.fields ? object.fields->size() : 0));
        }

        if (object.type == Value::ARRAY) {
            if (method == "add" && rawArgs.size() == 1) {
                if (!object.array) object.array = make_shared<vector<Value>>();
                object.array->push_back(evaluate(rawArgs[0]));
                return object;
            }
            if (method == "remove" && rawArgs.size() == 1) {
                Value index = evaluate(rawArgs[0]);
                if (index.type != Value::NUMBER) throw NeptuneError{"array.remove requires a numeric index"};
                long long i = static_cast<long long>(index.number);
                if (!object.array || i < 0 || i >= static_cast<long long>(object.array->size()))
                    throw NeptuneError{"array.remove index out of range"};
                object.array->erase(object.array->begin() + i);
                return object;
            }
            if (method == "clear" && rawArgs.empty()) {
                if (!object.array) object.array = make_shared<vector<Value>>();
                object.array->clear();
                return object;
            }
            if (method == "contains" && rawArgs.size() == 1) {
                Value needle = evaluate(rawArgs[0]);
                if (object.array) for (const Value& v : *object.array) if (compareValues(v, needle, "==")) return Value::makeBool(true);
                return Value::makeBool(false);
            }
        }

        if (object.type == Value::STRING) {
            if (method == "upper" && rawArgs.empty()) {
                string x = object.str;
                transform(x.begin(), x.end(), x.begin(), [](unsigned char c) { return static_cast<char>(toupper(c)); });
                return Value(x);
            }
            if (method == "lower" && rawArgs.empty()) {
                string x = object.str;
                transform(x.begin(), x.end(), x.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
                return Value(x);
            }
            if (method == "contains" && rawArgs.size() == 1)
                return Value::makeBool(object.str.find(valueToString(evaluate(rawArgs[0]))) != string::npos);
            if (method == "replace" && rawArgs.size() == 2) {
                string from = valueToString(evaluate(rawArgs[0]));
                string to = valueToString(evaluate(rawArgs[1]));
                if (!from.empty()) {
                    size_t p = 0;
                    while ((p = object.str.find(from, p)) != string::npos) {
                        object.str.replace(p, from.size(), to);
                        p += to.size();
                    }
                }
                return Value(object.str);
            }
            if (method == "substring" && (rawArgs.size() == 1 || rawArgs.size() == 2)) {
                Value startValue = evaluate(rawArgs[0]);
                if (startValue.type != Value::NUMBER) throw NeptuneError{"substring start must be a number"};
                long long start = static_cast<long long>(startValue.number);
                start = max(0LL, min(start, static_cast<long long>(object.str.size())));
                if (rawArgs.size() == 1) return Value(object.str.substr(static_cast<size_t>(start)));
                Value lenValue = evaluate(rawArgs[1]);
                if (lenValue.type != Value::NUMBER) throw NeptuneError{"substring length must be a number"};
                long long len = max(0LL, static_cast<long long>(lenValue.number));
                return Value(object.str.substr(static_cast<size_t>(start), static_cast<size_t>(len)));
            }
        }

        if (object.type == Value::MAP || object.type == Value::TABLE) {
            if (method == "has" && rawArgs.size() == 1) {
                string key = valueToString(evaluate(rawArgs[0]));
                return Value::makeBool(object.fields && object.fields->find(key) != object.fields->end());
            }
        }

        throw NeptuneError{"unknown method: " + method};
    }

    string resolveFunctionName(const string& name) const {
        if (functions.find(name) != functions.end()) return name;
        if (!currentNamespace.empty()) {
            string namespaced = currentNamespace + "." + name;
            if (functions.find(namespaced) != functions.end()) return namespaced;
        }
        return name;
    }

    Value callFunction(const string& originalName, const vector<string>& rawArgs) {
        string name = resolveFunctionName(originalName);

        auto functionIt = functions.find(name);
        if (functionIt == functions.end()) {
            auto structIt = structs.find(name);
            if (structIt != structs.end() && rawArgs.empty()) {
                Value instance = Value::makeMap();
                for (const auto& [field, defaultValue] : structIt->second.fields)
                    (*instance.fields)[field] = defaultValue;
                (*instance.fields)["__struct"] = Value(name);
                return instance;
            }
            throw NeptuneError{"unknown function: " + originalName};
        }

        const Function& function = functionIt->second;
        if (rawArgs.size() != function.parameters.size()) {
            throw NeptuneError{"function " + originalName + " expects " + to_string(function.parameters.size()) + " arguments"};
        }

        unordered_map<string, Value> callerSnapshot;
        string previousNamespace = currentNamespace;
        currentNamespace = function.namespaceName;
        scopes.push_back({});

        for (size_t i = 0; i < function.parameters.size(); ++i)
            scopes.back()[function.parameters[i]] = evaluate(rawArgs[i]);

        Value returnValue;
        try {
            executeBlock(function.body);
        } catch (const ReturnSignal& signal) {
            returnValue = signal.value;
        }

        scopes.pop_back();
        currentNamespace = previousNamespace;
        (void)callerSnapshot;
        return returnValue;
    }

    Value callBuiltin(const string& name, const vector<string>& rawArgs, bool& handled) {
        handled = true;

        if (name == "type") {
            if (rawArgs.size() != 1) throw NeptuneError{"type() expects 1 argument"};
            return Value(typeName(evaluate(rawArgs[0])));
        }

        if (name == "native.call") {
            if (rawArgs.empty()) throw NeptuneError{"native.call() needs a function name"};
            string nativeName = valueToString(evaluate(rawArgs[0]));
            vector<Value> args;
            for (size_t i = 1; i < rawArgs.size(); ++i) args.push_back(evaluate(rawArgs[i]));
            auto it = nativeFunctions.find(nativeName);
            if (it == nativeFunctions.end()) throw NeptuneError{"unknown native function: " + nativeName};
            return it->second(args);
        }

        if (name == "native.has") {
            if (rawArgs.size() != 1) throw NeptuneError{"native.has() expects 1 argument"};
            string nativeName = valueToString(evaluate(rawArgs[0]));
            return Value::makeBool(nativeFunctions.find(nativeName) != nativeFunctions.end());
        }

        if (startsWith(name, "file.")) {
            string op = name.substr(5);
            if (op == "exists" && rawArgs.size() == 1)
                return Value::makeBool(fs::exists(valueToString(evaluate(rawArgs[0]))));
            if (op == "read" && rawArgs.size() == 1) {
                ifstream file(valueToString(evaluate(rawArgs[0])), ios::binary);
                if (!file) throw NeptuneError{"could not read file"};
                stringstream buffer;
                buffer << file.rdbuf();
                return Value(buffer.str());
            }
            if ((op == "write" || op == "append") && rawArgs.size() == 2) {
                string path = valueToString(evaluate(rawArgs[0]));
                string data = valueToString(evaluate(rawArgs[1]));
                ios::openmode mode = ios::out | ios::binary;
                if (op == "append") mode |= ios::app;
                ofstream file(path, mode);
                if (!file) throw NeptuneError{"could not write file"};
                file << data;
                return Value::makeBool(true);
            }
            if (op == "delete" && rawArgs.size() == 1)
                return Value::makeBool(fs::remove(valueToString(evaluate(rawArgs[0]))));
            if (op == "copy" && rawArgs.size() == 2) {
                fs::copy_file(valueToString(evaluate(rawArgs[0])), valueToString(evaluate(rawArgs[1])), fs::copy_options::overwrite_existing);
                return Value::makeBool(true);
            }
            if (op == "move" && rawArgs.size() == 2) {
                fs::rename(valueToString(evaluate(rawArgs[0])), valueToString(evaluate(rawArgs[1])));
                return Value::makeBool(true);
            }
            handled = false;
            return Value();
        }

        if (startsWith(name, "directory.")) {
            string op = name.substr(10);
            if (op == "exists" && rawArgs.size() == 1)
                return Value::makeBool(fs::is_directory(valueToString(evaluate(rawArgs[0]))));
            if (op == "create" && rawArgs.size() == 1)
                return Value::makeBool(fs::create_directories(valueToString(evaluate(rawArgs[0]))));
            if (op == "remove" && rawArgs.size() == 1)
                return Value::makeBool(fs::remove_all(valueToString(evaluate(rawArgs[0]))) > 0);
            if (op == "list" && rawArgs.size() == 1) {
                Value result = Value::makeArray();
                for (const auto& entry : fs::directory_iterator(valueToString(evaluate(rawArgs[0]))))
                    result.array->push_back(Value(entry.path().filename().string()));
                return result;
            }
            handled = false;
            return Value();
        }

        if (startsWith(name, "system.")) {
            string op = name.substr(7);
            if (op == "argc" && rawArgs.empty()) return Value(static_cast<double>(programArguments.size()));
            if (op == "args" && rawArgs.empty()) {
                Value result = Value::makeArray();
                for (const string& arg : programArguments) result.array->push_back(Value(arg));
                return result;
            }
            if (op == "arg" && rawArgs.size() == 1) {
                Value index = evaluate(rawArgs[0]);
                if (index.type != Value::NUMBER) throw NeptuneError{"system.arg index must be a number"};
                long long i = static_cast<long long>(index.number);
                if (i < 0 || i >= static_cast<long long>(programArguments.size())) return Value();
                return Value(programArguments[static_cast<size_t>(i)]);
            }
            if (op == "env" && rawArgs.size() == 1) {
                string key = valueToString(evaluate(rawArgs[0]));
                const char* value = getenv(key.c_str());
                return value ? Value(string(value)) : Value();
            }
            if (op == "cwd" && rawArgs.empty()) return Value(fs::current_path().string());
            if (op == "chdir" && rawArgs.size() == 1) {
                fs::current_path(valueToString(evaluate(rawArgs[0])));
                return Value::makeBool(true);
            }
            if (op == "exec" && rawArgs.size() == 1) {
                string command = valueToString(evaluate(rawArgs[0]));
                int code = std::system(command.c_str());
                return Value(static_cast<double>(code));
            }
            handled = false;
            return Value();
        }

        if (startsWith(name, "time.")) {
            string op = name.substr(5);
            auto now = chrono::system_clock::now();
            time_t raw = chrono::system_clock::to_time_t(now);
            tm localTime{};
#ifdef _WIN32
            localtime_s(&localTime, &raw);
#else
            localtime_r(&raw, &localTime);
#endif
            if (op == "timestamp" && rawArgs.empty()) return Value(static_cast<double>(raw));
            if (op == "year" && rawArgs.empty()) return Value(static_cast<double>(localTime.tm_year + 1900));
            if (op == "month" && rawArgs.empty()) return Value(static_cast<double>(localTime.tm_mon + 1));
            if (op == "day" && rawArgs.empty()) return Value(static_cast<double>(localTime.tm_mday));
            if (op == "hour" && rawArgs.empty()) return Value(static_cast<double>(localTime.tm_hour));
            if (op == "minute" && rawArgs.empty()) return Value(static_cast<double>(localTime.tm_min));
            if (op == "second" && rawArgs.empty()) return Value(static_cast<double>(localTime.tm_sec));
            if (op == "sleep" && rawArgs.size() == 1) {
                Value ms = evaluate(rawArgs[0]);
                if (ms.type != Value::NUMBER) throw NeptuneError{"time.sleep expects milliseconds"};
                this_thread::sleep_for(chrono::milliseconds(static_cast<long long>(max(0.0, ms.number))));
                return Value::makeBool(true);
            }
            handled = false;
            return Value();
        }

        if (startsWith(name, "convert.")) {
            string op = name.substr(8);
            if (op == "string" && rawArgs.size() == 1) return Value(valueToString(evaluate(rawArgs[0])));
            if (op == "number" && rawArgs.size() == 1) {
                Value v = evaluate(rawArgs[0]);
                if (v.type == Value::NUMBER) return v;
                char* end = nullptr;
                double n = strtod(valueToString(v).c_str(), &end);
                if (!end || *end != '\0') return Value();
                return Value(n);
            }
            if (op == "boolean" && rawArgs.size() == 1) return Value::makeBool(truthy(evaluate(rawArgs[0])));
            handled = false;
            return Value();
        }

        if (startsWith(name, "json.")) {
            string op = name.substr(5);
            if (op == "parse" && rawArgs.size() == 1) return JsonParser(valueToString(evaluate(rawArgs[0]))).parse();
            if (op == "stringify" && rawArgs.size() == 1) return Value(jsonStringify(evaluate(rawArgs[0])));
            if (op == "read" && rawArgs.size() == 1) {
                string path = valueToString(evaluate(rawArgs[0]));
                return JsonParser(fileRead(path)).parse();
            }
            if (op == "write" && rawArgs.size() == 2) {
                string path = valueToString(evaluate(rawArgs[0]));
                string data = jsonStringify(evaluate(rawArgs[1]));
                fileWrite(path, data, false);
                return Value::makeBool(true);
            }
            handled = false;
            return Value();
        }

        handled = false;
        return Value();
    }

    static string jsonEscape(const string& text) {
        string out;
        for (char c : text) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                default: out += c; break;
            }
        }
        return out;
    }

    static string jsonStringify(const Value& v) {
        switch (v.type) {
            case Value::NONE: return "null";
            case Value::BOOLEAN: return v.number != 0.0 ? "true" : "false";
            case Value::NUMBER: return valueToString(v);
            case Value::STRING: return "\"" + jsonEscape(v.str) + "\"";
            case Value::FUNCTION: return "\"" + jsonEscape(v.functionName) + "\"";
            case Value::ARRAY: {
                string out = "[";
                if (v.array) {
                    for (size_t i = 0; i < v.array->size(); ++i) {
                        if (i) out += ",";
                        out += jsonStringify((*v.array)[i]);
                    }
                }
                return out + "]";
            }
            case Value::MAP:
            case Value::TABLE: {
                string out = "{";
                bool first = true;
                if (v.fields) {
                    for (const auto& [key, value] : *v.fields) {
                        if (!first) out += ",";
                        first = false;
                        out += "\"" + jsonEscape(key) + "\":" + jsonStringify(value);
                    }
                }
                return out + "}";
            }
        }
        return "null";
    }

    static string fileRead(const string& path) {
        ifstream file(path, ios::binary);
        if (!file) throw NeptuneError{"could not read file: " + path};
        stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    static void fileWrite(const string& path, const string& data, bool append) {
        ios::openmode mode = ios::out | ios::binary;
        if (append) mode |= ios::app;
        ofstream file(path, mode);
        if (!file) throw NeptuneError{"could not write file: " + path};
        file << data;
    }

    Value evaluate(string expression) {
        expression = trim(stripComment(expression));
        if (expression.empty()) return Value();

        if (expression == "true") return Value::makeBool(true);
        if (expression == "false") return Value::makeBool(false);

        if (expression.size() >= 2 && expression.front() == '[' && expression.back() == ']') {
            Value result = Value::makeArray();
            string inside = expression.substr(1, expression.size() - 2);
            for (const string& item : splitArguments(inside)) result.array->push_back(evaluate(item));
            return result;
        }

        if (expression.size() >= 2 && expression.front() == '{' && expression.back() == '}') {
            Value result = Value::makeMap();
            string inside = expression.substr(1, expression.size() - 2);
            for (const string& item : splitArguments(inside)) {
                size_t colon = findOperator(item, {":"});
                if (colon == string::npos) throw NeptuneError{"map entry requires ':'"};
                string key = trim(item.substr(0, static_cast<size_t>(colon)));
                if (key.size() >= 2 && key.front() == '"' && key.back() == '"') key = key.substr(1, key.size() - 2);
                (*result.fields)[key] = evaluate(item.substr(static_cast<size_t>(colon) + 1));
            }
            return result;
        }

        if (expression.size() >= 2 && expression.front() == '"' && expression.back() == '"') {
            string content = expression.substr(1, expression.size() - 2);
            string out;
            bool escaped = false;
            for (char c : content) {
                if (escaped) {
                    switch (c) {
                        case 'n': out += '\n'; break;
                        case 'r': out += '\r'; break;
                        case 't': out += '\t'; break;
                        case '"': out += '"'; break;
                        case '\\': out += '\\'; break;
                        default: out += c; break;
                    }
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else {
                    out += c;
                }
            }
            return Value(out);
        }

        if (isNumberLiteral(expression)) return Value(strtod(expression.c_str(), nullptr));

        if (expression.front() == '(' && expression.back() == ')') {
            int depth = 0;
            bool inString = false;
            bool wraps = true;
            for (size_t i = 0; i < expression.size(); ++i) {
                char c = expression[i];
                if (c == '"') inString = !inString;
                if (inString) continue;
                if (c == '(') ++depth;
                else if (c == ')') {
                    --depth;
                    if (depth == 0 && i != expression.size() - 1) {
                        wraps = false;
                        break;
                    }
                }
            }
            if (wraps) return evaluate(expression.substr(1, expression.size() - 2));
        }

        // logical OR / AND
        int pos = findOperator(expression, {"||"});
        if (pos >= 0) return Value::makeBool(truthy(evaluate(expression.substr(0, pos))) || truthy(evaluate(expression.substr(pos + 2))));
        pos = findOperator(expression, {"&&"});
        if (pos >= 0) return Value::makeBool(truthy(evaluate(expression.substr(0, pos))) && truthy(evaluate(expression.substr(pos + 2))));

        // comparison
        pos = findOperator(expression, {"==", "!=", ">=", "<=", ">", "<"});
        if (pos >= 0) {
            string op;
            for (const string& candidate : {string("=="), string("!="), string(">="), string("<="), string(">"), string("<")}) {
                if (expression.compare(static_cast<size_t>(pos), candidate.size(), candidate) == 0) {
                    op = candidate;
                    break;
                }
            }
            Value a = evaluate(expression.substr(0, static_cast<size_t>(pos)));
            Value b = evaluate(expression.substr(static_cast<size_t>(pos) + op.size()));
            return Value::makeBool(compareValues(a, b, op));
        }

        // random range
        pos = findOperator(expression, {"@"});
        if (pos >= 0) {
            Value a = evaluate(expression.substr(0, static_cast<size_t>(pos)));
            Value b = evaluate(expression.substr(static_cast<size_t>(pos) + 1));
            if (a.type != Value::NUMBER || b.type != Value::NUMBER) throw NeptuneError{"@ requires numeric values"};
            return Value(static_cast<double>(randomInt(static_cast<long long>(ceil(min(a.number, b.number))), static_cast<long long>(floor(max(a.number, b.number))))));
        }

        // addition / subtraction
        pos = findOperator(expression, {"+", "-"});
        if (pos > 0) {
            char op = expression[static_cast<size_t>(pos)];
            Value a = evaluate(expression.substr(0, static_cast<size_t>(pos)));
            Value b = evaluate(expression.substr(static_cast<size_t>(pos) + 1));
            if (op == '+' && (a.type == Value::STRING || b.type == Value::STRING)) return Value(valueToString(a) + valueToString(b));
            if (a.type != Value::NUMBER || b.type != Value::NUMBER) throw NeptuneError{"+ and - require numbers, except string concatenation with +"};
            return Value(op == '+' ? a.number + b.number : a.number - b.number);
        }

        // multiplication / division / modulo
        pos = findOperator(expression, {"*", "/", "%"});
        if (pos > 0) {
            char op = expression[static_cast<size_t>(pos)];
            Value a = evaluate(expression.substr(0, static_cast<size_t>(pos)));
            Value b = evaluate(expression.substr(static_cast<size_t>(pos) + 1));
            if (a.type != Value::NUMBER || b.type != Value::NUMBER) throw NeptuneError{"*, / and % require numbers"};
            if ((op == '/' || op == '%') && b.number == 0.0) throw NeptuneError{"division by zero"};
            if (op == '*') return Value(a.number * b.number);
            if (op == '/') return Value(a.number / b.number);
            return Value(fmod(a.number, b.number));
        }

        // unary operators
        if (expression[0] == '-') {
            Value v = evaluate(expression.substr(1));
            if (v.type != Value::NUMBER) throw NeptuneError{"unary - requires a number"};
            return Value(-v.number);
        }
        if (expression[0] == '!') return Value::makeBool(!truthy(evaluate(expression.substr(1))));

        // index access
        string base;
        vector<string> indexes;
        if (splitIndexChain(expression, base, indexes)) {
            return getIndex(evaluate(base), indexes);
        }

        // calls and dot operations
        size_t open = expression.find('(');
        if (open != string::npos && expression.back() == ')') {
            string callName = trim(expression.substr(0, open));
            string inside = expression.substr(open + 1, expression.size() - open - 2);
            vector<string> args = splitArguments(inside);

            bool handled = false;
            Value built = callBuiltin(callName, args, handled);
            if (handled) return built;

            // Namespaced functions such as math.sum(...) are resolved before object methods.
            string resolvedCall = resolveFunctionName(callName);
            if (functions.find(resolvedCall) != functions.end())
                return callFunction(callName, args);

            size_t dot = callName.rfind('.');
            if (dot != string::npos) {
                string left = trim(callName.substr(0, dot));
                string method = trim(callName.substr(dot + 1));
                Value object = evaluate(left);
                if (object.type == Value::MAP || object.type == Value::TABLE || object.type == Value::ARRAY || object.type == Value::STRING) {
                    // Arrays/maps use shared storage, so mutating methods persist naturally.
                    return callMethod(left, method, args);
                }
            }

            auto functionRef = functions.find(resolvedCall);
            if (functionRef != functions.end()) return callFunction(callName, args);

            auto structRef = structs.find(resolveFunctionName(callName));
            if (structRef != structs.end() && args.empty()) return callFunction(callName, args);

            // A function value can be called.
            Value possibleFunction = lookup(callName);
            if (possibleFunction.type == Value::FUNCTION) return callFunction(possibleFunction.functionName, args);

            throw NeptuneError{"unknown function: " + callName};
        }

        // A bare namespaced function can be stored as a function value.
        string resolvedExpression = resolveFunctionName(expression);
        if (functions.find(resolvedExpression) != functions.end())
            return Value::makeFunction(resolvedExpression);

        // property access: obj.key
        size_t dot = expression.find('.');
        if (dot != string::npos && expression.find('.', dot + 1) == string::npos) {
            string left = trim(expression.substr(0, dot));
            string key = trim(expression.substr(dot + 1));
            if (!left.empty() && !key.empty()) {
                Value object = evaluate(left);
                Value property = getProperty(object, key);
                if (property.type != Value::NONE) return property;
                string qualified = left + "." + key;
                if (functions.find(qualified) != functions.end()) return Value::makeFunction(qualified);
            }
        }

        Value variable = lookup(expression);
        if (variable.type != Value::NONE || exists(expression)) return variable;
        if (functions.find(resolveFunctionName(expression)) != functions.end()) return Value::makeFunction(resolveFunctionName(expression));
        if (structs.find(resolveFunctionName(expression)) != structs.end()) return Value::makeFunction(resolveFunctionName(expression));

        throw NeptuneError{"unknown value: " + expression};
    }

    static size_t findMatchingEnd(const vector<string>& lines, size_t start) {
        int depth = 1;
        for (size_t i = start; i < lines.size(); ++i) {
            string line = trim(stripComment(lines[i]));
            if (startsWith(line, "if(") || startsWith(line, "while(") || startsWith(line, "for(") || startsWith(line, "efunc ") || startsWith(line, "try") || startsWith(line, "struct ") || startsWith(line, "vartable ")) ++depth;
            else if (line == "end") {
                --depth;
                if (depth == 0) return i;
            }
        }
        return lines.size();
    }

    static bool parseOneFunction(const vector<string>& lines, size_t& i, string& name, Function& function, const string& ns) {
        string line = trim(stripComment(lines[i]));
        if (!startsWith(line, "efunc ")) return false;
        size_t open = line.find('(');
        size_t close = line.rfind(')');
        if (open == string::npos || close == string::npos || close < open) throw NeptuneError{"invalid function declaration"};

        name = trim(line.substr(6, open - 6));
        string parameterText = line.substr(open + 1, close - open - 1);
        function.parameters = splitArguments(parameterText);
        function.namespaceName = ns;
        for (string& parameter : function.parameters) parameter = trim(parameter);

        int depth = 1;
        ++i;
        while (i < lines.size() && depth > 0) {
            string current = trim(stripComment(lines[i]));
            if (startsWith(current, "efunc ") || startsWith(current, "if(") || startsWith(current, "while(") || startsWith(current, "for(") || startsWith(current, "try") || startsWith(current, "struct ") || startsWith(current, "vartable ")) {
                ++depth;
                function.body.push_back(lines[i]);
            } else if (current == "end") {
                --depth;
                if (depth > 0) function.body.push_back(lines[i]);
            } else {
                function.body.push_back(lines[i]);
            }
            ++i;
        }
        if (depth != 0) throw NeptuneError{"missing end for function: " + name};
        return true;
    }

    void parseFunctions(const vector<string>& lines, const string& ns) {
        for (size_t i = 0; i < lines.size(); ++i) {
            string line = trim(stripComment(lines[i]));
            if (!startsWith(line, "efunc ")) continue;
            string name;
            Function function;
            size_t cursor = i;
            if (parseOneFunction(lines, cursor, name, function, ns)) {
                string key = ns.empty() ? name : ns + "." + name;
                functions[key] = function;
                i = cursor;
            }
        }
    }

    void parseStructs(const vector<string>& lines) {
        for (size_t i = 0; i < lines.size(); ++i) {
            string line = trim(stripComment(lines[i]));
            if (!startsWith(line, "struct ")) continue;
            string name = trim(line.substr(7));
            StructDefinition def;
            size_t end = findMatchingEnd(lines, i + 1);
            for (size_t j = i + 1; j < end; ++j) {
                string field = trim(stripComment(lines[j]));
                if (!startsWith(field, "var ")) continue;
                string rest = trim(field.substr(4));
                size_t eq = rest.find('=');
                string fieldName = eq == string::npos ? trim(rest) : trim(rest.substr(0, eq));
                Value defaultValue;
                if (eq != string::npos) defaultValue = evaluate(rest.substr(eq + 1));
                if (!fieldName.empty()) def.fields.emplace_back(fieldName, defaultValue);
            }
            structs[name] = def;
            i = end;
        }
    }

    bool extractDefine(const vector<string>& lines, string& ns) const {
        for (const string& raw : lines) {
            string line = trim(stripComment(raw));
            if (startsWith(line, "/define(") && line.back() == ')') {
                ns = trim(line.substr(8, line.size() - 9));
                if (ns.size() >= 2 && ns.front() == '"' && ns.back() == '"') ns = ns.substr(1, ns.size() - 2);
                return !ns.empty();
            }
        }
        return false;
    }

    void loadLibrary(const fs::path& requested, const fs::path& baseDir) {
        fs::path path = requested;
        if (path.is_relative()) path = baseDir / path;
        path = fs::weakly_canonical(path);
        string key = path.string();
        if (loadedLibraries.find(key) != loadedLibraries.end()) return;

        vector<string> lines;
        ifstream file(path);
        if (!file) throw NeptuneError{"could not include file: " + path.string()};
        string line;
        while (getline(file, line)) lines.push_back(line);

        string ns;
        if (!extractDefine(lines, ns)) throw NeptuneError{"library needs /define(name): " + path.string()};
        loadedLibraries.insert(key);
        parseStructs(lines);
        parseFunctions(lines, ns);

        // Nested includes from the library.
        for (const string& raw : lines) {
            string current = trim(stripComment(raw));
            if (startsWith(current, "/Include(") && current.back() == ')') {
                string inside = trim(current.substr(9, current.size() - 10));
                if (inside.size() >= 2 && inside.front() == '"' && inside.back() == '"') inside = inside.substr(1, inside.size() - 2);
                loadLibrary(fs::path(inside), path.parent_path());
            }
        }
    }

    void processIncludes(const vector<string>& lines, const fs::path& baseDir) {
        for (const string& raw : lines) {
            string current = trim(stripComment(raw));
            if (startsWith(current, "/Include(") && current.back() == ')') {
                string inside = trim(current.substr(9, current.size() - 10));
                if (inside.size() >= 2 && inside.front() == '"' && inside.back() == '"') inside = inside.substr(1, inside.size() - 2);
                loadLibrary(fs::path(inside), baseDir);
            }
        }
    }

    void executeIf(const vector<string>& lines, size_t& i) {
        string line = trim(stripComment(lines[i]));
        size_t doPos = line.rfind(")do");
        if (doPos == string::npos) throw NeptuneError{"invalid if syntax"};
        string condition = trim(line.substr(3, doPos - 3));

        vector<string> trueBlock;
        vector<string> falseBlock;
        bool inElse = false;
        int depth = 1;
        size_t j = i + 1;

        while (j < lines.size() && depth > 0) {
            string current = trim(stripComment(lines[j]));
            if (startsWith(current, "if(") || startsWith(current, "while(") || startsWith(current, "for(") || startsWith(current, "try") || startsWith(current, "struct ") || startsWith(current, "vartable ")) {
                ++depth;
                (inElse ? falseBlock : trueBlock).push_back(lines[j]);
            } else if (current == "end") {
                --depth;
                if (depth > 0) (inElse ? falseBlock : trueBlock).push_back(lines[j]);
            } else if (current == "else" && depth == 1) {
                inElse = true;
            } else {
                (inElse ? falseBlock : trueBlock).push_back(lines[j]);
            }
            ++j;
        }

        if (depth != 0) throw NeptuneError{"missing end for if"};
        executeBlock(truthy(evaluate(condition)) ? trueBlock : falseBlock);
        // j aponta para a linha depois do end; o chamador incrementa i.
        i = j - 1;
    }

    void executeWhile(const vector<string>& lines, size_t& i) {
        string line = trim(stripComment(lines[i]));
        size_t doPos = line.rfind(")do");
        if (doPos == string::npos) throw NeptuneError{"invalid while syntax"};
        string condition = trim(line.substr(6, doPos - 6));
        size_t end = findMatchingEnd(lines, i + 1);
        if (end >= lines.size()) throw NeptuneError{"missing end for while"};
        vector<string> body(lines.begin() + static_cast<long long>(i + 1), lines.begin() + static_cast<long long>(end));

        size_t safety = 0;
        while (truthy(evaluate(condition))) {
            try {
                executeBlock(body);
            } catch (const ContinueSignal&) {
            } catch (const BreakSignal&) {
                break;
            }
            if (++safety > 1000000) throw NeptuneError{"while exceeded safety limit"};
        }
        i = end;
    }

    void executeFor(const vector<string>& lines, size_t& i) {
        string line = trim(stripComment(lines[i]));
        size_t doPos = line.rfind(")do");
        if (doPos == string::npos) throw NeptuneError{"invalid for syntax"};
        string inside = trim(line.substr(4, doPos - 4));

        int inPos = findOperator(inside, {" in "});
        size_t end = findMatchingEnd(lines, i + 1);
        if (end >= lines.size()) throw NeptuneError{"missing end for for"};
        vector<string> body(lines.begin() + static_cast<long long>(i + 1), lines.begin() + static_cast<long long>(end));

        if (inPos >= 0) {
            string variableName = trim(inside.substr(0, static_cast<size_t>(inPos)));
            string arrayExpr = trim(inside.substr(static_cast<size_t>(inPos) + 4));
            Value collection = evaluate(arrayExpr);
            if (collection.type != Value::ARRAY) throw NeptuneError{"for-in requires an array"};
            // Um iterador novo deve nascer no escopo atual quando ainda não existe.
            // Isso evita que um for-in dentro de uma função altere uma variável global
            // apenas por causa do nome do iterador.
            if (!exists(variableName)) declareVar(variableName, Value());
            for (const Value& item : *collection.array) {
                assignVar(variableName, item);
                try { executeBlock(body); }
                catch (const ContinueSignal&) { continue; }
                catch (const BreakSignal&) { break; }
            }
            i = end;
            return;
        }

        vector<string> args = splitArguments(inside);
        if (args.size() < 2 || args.size() > 3) throw NeptuneError{"invalid for syntax"};
        size_t equal = args[0].find('=');
        if (equal == string::npos) throw NeptuneError{"invalid for initialization"};
        string variableName = trim(args[0].substr(0, equal));
        double start = evaluate(args[0].substr(equal + 1)).number;
        double finish = evaluate(args[1]).number;
        double step = args.size() == 3 ? evaluate(args[2]).number : 1.0;
        if (step == 0.0) throw NeptuneError{"for step cannot be zero"};

        if (step > 0) {
            for (double n = start; n <= finish; n += step) {
                assignVar(variableName, Value(n));
                try { executeBlock(body); }
                catch (const ContinueSignal&) { continue; }
                catch (const BreakSignal&) { break; }
            }
        } else {
            for (double n = start; n >= finish; n += step) {
                assignVar(variableName, Value(n));
                try { executeBlock(body); }
                catch (const ContinueSignal&) { continue; }
                catch (const BreakSignal&) { break; }
            }
        }
        i = end;
    }

    void executeTry(const vector<string>& lines, size_t& i) {
        vector<string> tryBlock, catchBlock, finallyBlock;
        string catchName;
        size_t j = i + 1;
        int depth = 1;
        enum Section { TRY, CATCH, FINALLY } section = TRY;

        while (j < lines.size() && depth > 0) {
            string current = trim(stripComment(lines[j]));
            if (startsWith(current, "if(") || startsWith(current, "while(") || startsWith(current, "for(") || startsWith(current, "try") || startsWith(current, "struct ") || startsWith(current, "vartable ")) {
                ++depth;
                (section == TRY ? tryBlock : section == CATCH ? catchBlock : finallyBlock).push_back(lines[j]);
            } else if (current == "end") {
                --depth;
                if (depth > 0) (section == TRY ? tryBlock : section == CATCH ? catchBlock : finallyBlock).push_back(lines[j]);
            } else if (depth == 1 && startsWith(current, "catch")) {
                section = CATCH;
                size_t open = current.find('(');
                size_t close = current.rfind(')');
                if (open != string::npos && close != string::npos && close > open) catchName = trim(current.substr(open + 1, close - open - 1));
            } else if (depth == 1 && current == "finally") {
                section = FINALLY;
            } else {
                (section == TRY ? tryBlock : section == CATCH ? catchBlock : finallyBlock).push_back(lines[j]);
            }
            ++j;
        }

        if (depth != 0) throw NeptuneError{"missing end for try"};

        try {
            executeBlock(tryBlock);
        } catch (const NeptuneError& error) {
            if (!catchName.empty()) declareVar(catchName, Value(error.message));
            executeBlock(catchBlock);
        }

        executeBlock(finallyBlock);
        // j aponta para a linha depois do end; o chamador incrementa i.
        i = j - 1;
    }

    void executeLine(const string& rawLine) {
        string line = trim(stripComment(rawLine));
        if (line.empty()) return;
        if (line == "else" || line == "end" || line == "finally") return;
        if (startsWith(line, "/Include(") || startsWith(line, "/include(") || startsWith(line, "/define(")) return;

        if (line == "break") throw BreakSignal{};
        if (line == "continue") throw ContinueSignal{};

        if (startsWith(line, "throw(")) {
            if (line.back() != ')') throw NeptuneError{"invalid throw syntax"};
            string inside = line.substr(6, line.size() - 7);
            throw NeptuneError{valueToString(evaluate(inside))};
        }

        if (startsWith(line, "global ")) {
            string rest = trim(line.substr(7));
            size_t equal = rest.find('=');
            if (equal == string::npos) throw NeptuneError{"global requires ="};
            string name = trim(rest.substr(0, equal));
            scopes.front()[name] = evaluate(rest.substr(equal + 1));
            return;
        }

        if (startsWith(line, "var ")) {
            string rest = trim(line.substr(4));
            size_t equal = rest.find('=');
            if (equal == string::npos) declareVar(rest, Value());
            else declareVar(trim(rest.substr(0, equal)), evaluate(rest.substr(equal + 1)));
            return;
        }

        if (startsWith(line, "struct ")) return;
        if (startsWith(line, "vartable ")) return;

        if (startsWith(line, "return")) {
            string expression;
            if (line == "return") expression = "";
            else if (startsWith(line, "return(")) expression = line.substr(7, line.size() - 8);
            else expression = trim(line.substr(6));
            throw ReturnSignal{expression.empty() ? Value() : evaluate(expression)};
        }

        if (startsWith(line, "echo(") && line.back() == ')') {
            vector<string> args = splitArguments(line.substr(5, line.size() - 6));
            for (size_t i = 0; i < args.size(); ++i) {
                cout << valueToString(evaluate(args[i]));
                if (i + 1 < args.size()) cout << ' ';
            }
            cout << '\n';
            return;
        }

        if (startsWith(line, "cin(") && line.back() == ')') {
            string name = trim(line.substr(4, line.size() - 5));
            string input;
            if (!getline(cin, input)) throw NeptuneError{"input failed"};
            input = trim(input);
            if (isNumberLiteral(input)) assignVar(name, Value(strtod(input.c_str(), nullptr)));
            else if (input == "true" || input == "false") assignVar(name, Value::makeBool(input == "true"));
            else assignVar(name, Value(input));
            return;
        }

        // array/map/property assignment and ordinary assignment
        vector<string> assignmentOps = {"+=", "-=", "*=", "/=", "="};
        int assignmentPos = -1;
        string assignmentOp;
        for (const string& op : assignmentOps) {
            int p = findOperator(line, {op});
            if (p >= 0) { assignmentPos = p; assignmentOp = op; break; }
        }
        if (assignmentPos >= 0) {
            string lhs = trim(line.substr(0, static_cast<size_t>(assignmentPos)));
            string rhsText = trim(line.substr(static_cast<size_t>(assignmentPos) + assignmentOp.size()));
            Value rhs = evaluate(rhsText);
            if (assignmentOp != "=") {
                Value old = evaluate(lhs);
                if (old.type != Value::NUMBER || rhs.type != Value::NUMBER) throw NeptuneError{"compound assignment requires numbers"};
                if (assignmentOp == "+=") rhs = Value(old.number + rhs.number);
                else if (assignmentOp == "-=") rhs = Value(old.number - rhs.number);
                else if (assignmentOp == "*=") rhs = Value(old.number * rhs.number);
                else if (assignmentOp == "/=") {
                    if (rhs.number == 0) throw NeptuneError{"division by zero"};
                    rhs = Value(old.number / rhs.number);
                }
            }

            string base;
            vector<string> indexes;
            if (splitIndexChain(lhs, base, indexes)) {
                Value& target = lookupRef(base);
                if (!setIndex(target, indexes, rhs)) throw NeptuneError{"invalid indexed assignment"};
                return;
            }

            size_t dot = lhs.find('.');
            if (dot != string::npos) {
                string baseName = trim(lhs.substr(0, dot));
                string key = trim(lhs.substr(dot + 1));
                Value& target = lookupRef(baseName);
                if (!setProperty(target, key, rhs)) throw NeptuneError{"property assignment requires map/table"};
                return;
            }

            if (!isIdentifier(lhs)) throw NeptuneError{"invalid assignment target"};
            assignVar(lhs, rhs);
            return;
        }

        evaluate(line);
    }

    void executeStruct(const vector<string>& lines, size_t& i) {
        string line = trim(stripComment(lines[i]));
        if (!startsWith(line, "struct ")) return;
        string name = trim(line.substr(7));
        StructDefinition def;
        size_t end = findMatchingEnd(lines, i + 1);
        for (size_t j = i + 1; j < end; ++j) {
            string field = trim(stripComment(lines[j]));
            if (!startsWith(field, "var ")) continue;
            string rest = trim(field.substr(4));
            size_t eq = rest.find('=');
            string fieldName = eq == string::npos ? trim(rest) : trim(rest.substr(0, eq));
            Value defaultValue = eq == string::npos ? Value() : evaluate(rest.substr(eq + 1));
            def.fields.emplace_back(fieldName, defaultValue);
        }
        structs[name] = def;
        i = end;
    }

    void executeVarTable(const vector<string>& lines, size_t& i) {
        string line = trim(stripComment(lines[i]));
        if (!startsWith(line, "vartable ")) return;
        string name = trim(line.substr(9));
        if (!name.empty() && name.back() == '{') name = trim(name.substr(0, name.size() - 1));
        Value table = Value::makeTable();
        ++i;
        while (i < lines.size()) {
            string current = trim(stripComment(lines[i]));
            if (current == "}") break;
            if (startsWith(current, "var ")) {
                string rest = trim(current.substr(4));
                size_t equal = rest.find('=');
                string key = equal == string::npos ? trim(rest) : trim(rest.substr(0, equal));
                Value value = equal == string::npos ? Value() : evaluate(rest.substr(equal + 1));
                (*table.fields)[key] = value;
            }
            ++i;
        }
        declareVar(name, table);
    }

    void executeBlock(const vector<string>& lines) {
        size_t i = 0;
        while (i < lines.size()) {
            string line = trim(stripComment(lines[i]));
            if (line.empty()) { ++i; continue; }

            if (startsWith(line, "if(")) { executeIf(lines, i); ++i; continue; }
            if (startsWith(line, "while(")) { executeWhile(lines, i); ++i; continue; }
            if (startsWith(line, "for(")) { executeFor(lines, i); ++i; continue; }
            if (line == "try") { executeTry(lines, i); ++i; continue; }
            if (startsWith(line, "struct ")) { executeStruct(lines, i); ++i; continue; }
            if (startsWith(line, "vartable ")) { executeVarTable(lines, i); ++i; continue; }
            if (line == "else" || line == "end" || line == "catch" || line == "finally") { ++i; continue; }

            executeLine(lines[i]);
            ++i;
        }
    }

    void executeProgram(const vector<string>& lines) {
        size_t i = 0;
        while (i < lines.size()) {
            string line = trim(stripComment(lines[i]));
            if (line.empty()) { ++i; continue; }

            if (startsWith(line, "efunc ")) {
                size_t cursor = i;
                string ignoredName;
                Function ignored;
                parseOneFunction(lines, cursor, ignoredName, ignored, currentNamespace);
                i = cursor;
                continue;
            }
            if (startsWith(line, "struct ")) { executeStruct(lines, i); ++i; continue; }
            if (startsWith(line, "vartable ")) { executeVarTable(lines, i); ++i; continue; }
            if (startsWith(line, "if(")) { executeIf(lines, i); ++i; continue; }
            if (startsWith(line, "while(")) { executeWhile(lines, i); ++i; continue; }
            if (startsWith(line, "for(")) { executeFor(lines, i); ++i; continue; }
            if (line == "try") { executeTry(lines, i); ++i; continue; }
            if (line == "else" || line == "end" || line == "catch" || line == "finally") throw NeptuneError{"unexpected " + line};

            executeLine(lines[i]);
            ++i;
        }
    }

public:
    Neptune()
        : scopes(1), randomState(static_cast<uint64_t>(chrono::high_resolution_clock::now().time_since_epoch().count())) {}

    void setProgramArguments(const vector<string>& args) { programArguments = args; }

    void registerNativeFunction(const string& name, NativeFunction function) {
        nativeFunctions[name] = move(function);
    }

    void run(const vector<string>& lines, const fs::path& baseDir = fs::current_path()) {
        processIncludes(lines, baseDir);
        parseStructs(lines);
        parseFunctions(lines, "");
        executeProgram(lines);
    }

    void runFile(const string& filename) {
        fs::path path = fs::absolute(filename);
        ifstream file(path);
        if (!file) throw NeptuneError{"could not open file: " + filename};
        vector<string> lines;
        string line;
        while (getline(file, line)) lines.push_back(line);
        processIncludes(lines, path.parent_path());
        parseStructs(lines);
        parseFunctions(lines, "");
        executeProgram(lines);
    }

    void repl() {
        cout << "Neptune C--\nInteractive mode. Type 'exit' to quit.\n\n";
        string line;
        while (true) {
            cout << ">>> ";
            if (!getline(cin, line)) break;
            if (trim(line) == "exit") break;
            if (trim(line).empty()) continue;
            try { executeProgram({line}); }
            catch (const NeptuneError& error) { cerr << "Neptune: " << error.message << '\n'; }
            catch (const ReturnSignal&) { cerr << "Neptune: return outside function\n"; }
        }
    }
};

int main(int argc, char* argv[]) {
    Neptune neptune;

    vector<string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    neptune.setProgramArguments(args);

    // Example native bridge function. More can be registered by code embedding Neptune.
    neptune.registerNativeFunction("print_native", [](const vector<Value>& values) {
        for (size_t i = 0; i < values.size(); ++i) {
            const Value& v = values[i];
            if (v.type == Value::STRING) cout << v.str;
            else if (v.type == Value::NUMBER) cout << v.number;
            else if (v.type == Value::BOOLEAN) cout << (v.number != 0.0 ? "true" : "false");
            else cout << "[value]";
            if (i + 1 < values.size()) cout << ' ';
        }
        cout << '\n';
        return Value::makeBool(true);
    });

    try {
        if (argc >= 2 && string(argv[1]) == "--c") {
            neptune.repl();
            return 0;
        }

        if (argc < 2) {
            cout << "Neptune C--\nUsage:\n  neptune --c\n  neptune program.nep [arguments...]\n";
            return 0;
        }

        string filename = argv[1];
        if (filename.size() < 4 || filename.substr(filename.size() - 4) != ".nep")
            throw NeptuneError{"file must have .nep extension"};

        neptune.setProgramArguments(vector<string>(argv + 2, argv + argc));
        neptune.runFile(filename);
        return 0;
    } catch (const NeptuneError& error) {
        cerr << "Neptune: " << error.message << '\n';
        return 1;
    } catch (const ReturnSignal&) {
        cerr << "Neptune: return outside function\n";
        return 1;
    } catch (const exception& error) {
        cerr << "Neptune internal error: " << error.what() << '\n';
        return 1;
    }
}
