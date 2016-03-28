#include <stdio.h>
#include <gelf.h>
#include <libelf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

#include <queue>
#include <vector>
#include <string>
#include "my_ldd.h"
using namespace std;

static
bool fileExist(string filepath) {
   return !access(filepath.c_str(), F_OK);
}
static
bool isDir(string dirpath) {
    struct stat s;
    if (stat(dirpath.c_str(), &s) == 0) {
        if (S_ISDIR(s.st_mode)) {
            return true;
        }
    }
    return false;
}

static 
vector<string> splitString(string str) {
    vector<string> ret;
    size_t begin = 0;
    while(1) {
        size_t pos = str.find(":", begin);
        if (pos == string::npos) {
            ret.push_back(str.substr(begin));
            return ret;
        }
        ret.push_back(str.substr(begin, pos-begin));
        begin = pos + 1;
    }
}

/* path included in /etc/ld.so.conf*/
static
vector<string> getPathInFile(string filename) {
    vector<string> ret;
    FILE *fp = fopen(filename.c_str(), "r");
    if (fp == NULL) {
        return ret;
    }
    char buf[256];
    while(!feof(fp)) {
        memset(buf, 0, sizeof(buf));
        if (fgets(buf, sizeof(buf), fp) == NULL)
            break;
        char *p = buf;
        /*rm whitespaces*/
        while(p!='\0' && *p == ' ')
            p++;
        char *e = strchr(p, ' ');
        if (e != NULL) {
            *e = '\0';
        }
        buf[strlen(buf)-1] = '\0';
        //printf("path:-%s-\n", p);
        string path(p);
        if (isDir(path)) {
            ret.push_back(path);
        }
    }
    
    fclose(fp);
    return ret;
}

static
vector<string> getLdSoPath() {
    vector<string> ret;
    vector<string> files;
    files.push_back("/etc/ld.so.conf");
    struct dirent *dent;
    struct stat s;
    DIR *dir;

    dir = opendir("/etc/ld.so.conf.d/");
    if (dir == NULL) {
        return ret;
    }
    while((dent = readdir(dir)) != NULL) {
        if (!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, ".."))
        continue;
        string path;
        path = string("/etc/ld.so.conf.d/") + string(dent->d_name);
        lstat(path.c_str(), &s);
        if (S_ISREG(s.st_mode)) {
            files.push_back(path);
        }
    }
    closedir(dir);
    size_t i;
    vector<string> tmp;
    for (i=0; i<files.size(); i++) {
        tmp = getPathInFile(files[i]); 
        ret.insert(ret.begin(), tmp.begin(), tmp.end());
        tmp.clear();
    }
    return ret;
}


MyLdd::MyLdd(string filename) {
    this->filename = filename;
    setSearchPath();
}

vector<string> &MyLdd::GetSharedLibs() {
    getDeps();
    return depsLibs;
}

vector<string> &MyLdd::GetMissedLibs() {
    return missLibs;
}

void MyLdd::getDeps() {
    queue<string> q;
    
    q.push(filename);
    visited[filename] = true;

    while(!q.empty()) {
        vector<string> sharedLibs;
        vector<string> rpath_tmp;
        vector<string> spath;

        string newfilename = q.front();
        q.pop();
        getDepsInfo(newfilename, sharedLibs, rpath_tmp);
        rpath.insert(rpath.begin(), rpath_tmp.begin(), rpath_tmp.end());
        spath = rpath;
        spath.insert(spath.end(), searchPath.begin(), searchPath.end());
        size_t i;
        size_t j;
#if 0
        for (i=0; i<spath.size(); i++) {
            printf("spath: %s\n", spath[i].c_str());
        } 
#endif
        for (i=0; i<sharedLibs.size(); i++) {
            string libPath;
            for (j=0; j<spath.size(); j++) {
                libPath = spath[j] + "/" + sharedLibs[i];
                if (fileExist(libPath)) {
                    if (!visited.count(libPath)) {
                        q.push(libPath);
                        depsLibs.push_back(libPath);
                        visited[libPath] = true;
                    }
                    break;
                }
            }
            if (j == spath.size()) {
                missLibs.push_back(sharedLibs[i]);
                visited[libPath] = true;
            }
        }
        sharedLibs.clear();
        rpath_tmp.clear();
        spath.clear();
    }
}


void MyLdd::setSearchPath() {
    char *ldlib;
    ldlib = getenv("LD_LIBRARY_PATH");
    if (ldlib != NULL) {
        searchPath = splitString(ldlib);
    }
    vector<string> tmp = getLdSoPath();
    searchPath.insert(searchPath.begin(), tmp.begin(), tmp.end());
    /*/etc/ld.so.conf /etc/ld.so.conf.d*/
    searchPath.push_back("/lib64");
    searchPath.push_back("/usr/lib64");
    searchPath.push_back("/lib");
    searchPath.push_back("/usr/lib");
    searchPath.push_back("/usr/local/lib");
}

int MyLdd::getDepsInfo(string filename, vector<string> &sharedLibName, vector<string>& rpath) {
	int ret = -1;
	int fd;
    int cnt = 0;
	Elf *elf;
	GElf_Ehdr ehdr;
	GElf_Shdr shdr;
    Elf_Data *data_dynamic = NULL;
    Elf_Data *data_dynstr = NULL;

	Elf_Scn *sec_dynamic = NULL; /*.dynamic section*/
    Elf_Scn *sec_dynstr = NULL; /*.dynstr section*/
    Elf_Scn *sec = NULL;
    GElf_Dyn *dyn = NULL;

	Elf_Kind ek;

	elf_version(EV_CURRENT);

	fd = open(filename.c_str(), O_RDONLY);
	if (!fd) {
		ret = -1;
		goto out;
	}

	elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);
	if (elf == NULL) {
		fprintf(stderr, "%s: cannot read %s ELF file.\n", __func__, filename.c_str());
		ret = -1;
		goto out_close;
	}

	ek = elf_kind(elf);
	if (ek != ELF_K_ELF) {
		ret = -1;
		goto out_elf;
	}

	if (gelf_getehdr(elf, &ehdr) == NULL) {
		fprintf(stderr, "%s: cannot get elf header.\n", __func__);
		ret = -1;
		goto out_elf;
	}

    /* Elf is corrupted/truncated, avoid calling elf_strptr. */
	if (!elf_rawdata(elf_getscn(elf, ehdr.e_shstrndx), NULL))
        goto out_elf;

	while ((sec = elf_nextscn(elf, sec)) != NULL) {
		char *str;

		gelf_getshdr(sec, &shdr);
		str = elf_strptr(elf, ehdr.e_shstrndx, shdr.sh_name);
		if (!strcmp(".dynamic", str)) {
			sec_dynamic = sec;
			break;
		}
        if (!strcmp(".dynstr", str)) {
            sec_dynstr = sec;
        }
	}
    if (!sec_dynamic || !sec_dynstr) {
        ret = -1;
        goto out_elf;
    }

	data_dynamic = elf_getdata(sec_dynamic, NULL);
	if (data_dynamic == NULL) {
		ret = -1;
		goto out_elf;
	}
    
    data_dynstr = elf_getdata(sec_dynstr, NULL);
	if (data_dynstr == NULL) {
		ret = -1;
		goto out_elf;
	}
  
    dyn = (GElf_Dyn *)malloc(sizeof(GElf_Dyn));
    
    gelf_getdyn(data_dynamic, 0, dyn);
    for (;dyn->d_tag != DT_NULL; ) {
        if (dyn->d_tag == DT_NEEDED) {
            sharedLibName.push_back(string((char *)(data_dynstr->d_buf)+dyn->d_un.d_val));
        }
        else if (dyn->d_tag == DT_RPATH) {
            rpath = splitString(string((char *)(data_dynstr->d_buf)+dyn->d_un.d_val));
        }
        else if (dyn->d_tag == DT_SONAME) {
        }
        gelf_getdyn(data_dynamic, ++cnt, dyn);
        if (dyn->d_tag == DT_NULL)
            break;
    }
    free(dyn);	
    ret = 0;

out_elf:
	elf_end(elf);
out_close:
	close(fd);
out:
	return ret;
}

#if 1
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: ./myldd executable-file\n");
        return -1;
    } 
    vector<string> libs;
    vector<string> mlibs;

    MyLdd deps(argv[1]);
    libs = deps.GetSharedLibs();
    
    size_t i;
    for (i=0; i<libs.size(); i++) {
        printf("%s\n", libs[i].c_str());
    }
    mlibs = deps.GetMissedLibs();
    for (i=0; i<mlibs.size(); i++) {
        if (i == 0)
            printf("----------- miss  ---------\n");
        printf("%s\n", mlibs[i].c_str());
    }
    /*
    vector<string> path;
    path = getLdSoPath();
    //size_t i;
    for (i=0; i<path.size(); i++) {
        printf("%s\n", path[i].c_str());
    }
    */
    return 0;
}
#endif
