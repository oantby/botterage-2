#ifndef STRING_EXTENSIONS
#define STRING_EXTENSIONS

#include <vector>

using namespace std;

string toLower(string input);

vector<string> split(const string &input, char splitChar);

template<typename T>
string implode(const vector<string> &input, T separator);

bool isNumeric(const string &s);


#endif
