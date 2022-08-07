#ifndef UTILS_HPP
#define UTILS_HPP

#include <mysql++.h>

using namespace std;

unsigned randomNumber();
string htmlEntities(const string &data);
string urlEncode(const string &data);
const string getVar(const string &varName, const char *fallback = NULL);
bool setVar(const string &varName, const string &varVal);
bool deleteVar(const string &varName);
void invoke_hook(string hook, void *args);
vector<string> &get_modules();

#endif