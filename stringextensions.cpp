#include <iostream>
#include <string>
#include <vector>
using namespace std;

string toLower(string input) {
	for (size_t i = 0; i < input.size(); i++) {
		if ((input[i] > 64)&&(input[i] < 91)) {
			input[i] += 32;
		}
	}
	return input;
}

vector<string> split(const string &input, char splitChar) {
	vector<string> rval;
	string s;
	for (size_t i = 0; i < input.size(); i++) {
		if (input[i] == splitChar) {
			rval.push_back(s);
			s.clear();
		} else {
			s += input[i];
		}
	}
	rval.push_back(s);
	return rval;
}

template<typename T>
string implode(const vector<string> &input, T separator) {
	string r;
	for (size_t i = 0; i < input.size(); i++) {
		r += input[i];
		if (i != input.size() - 1) r += separator;
	}
	return r;
}

bool isNumeric(const string &s) {
	char *p;
	strtol(s.c_str(), &p, 10);
	// if *p is null, we're at the end of the string, so it's numeric.
	return !*p;
}