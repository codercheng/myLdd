#ifndef _DBG_DEP_H
#define _DBG_DEP_H

#include <vector>
#include <string>
#include <map>

using namespace std;

class MyLdd {
public:
    MyLdd(string filename);
    vector<string> &GetSharedLibs();
    vector<string> &GetMissedLibs();
private:
    int getDepsInfo(string filename, vector<string> &sharedLibName, vector<string> &rpath);
    void setSearchPath();
    void getDeps();
private:
    map<string, bool> visited;
    string filename;
    vector<string> searchPath;
    vector<string> rpath;
    vector<string> missLibs;
    vector<string> depsLibs;
};


#endif
