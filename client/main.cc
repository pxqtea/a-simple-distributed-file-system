/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall hello.c `pkg-config fuse --cflags --libs` -o hello
*/

#define FUSE_USE_VERSION 26

#include <sys/stat.h>
#include <sys/types.h>
#include <memory>
#include <iostream>
#include <libgen.h>
#include <fstream>
#include <fuse.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <grpc/grpc.h>
#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/credentials.h>
#include "fs.grpc.pb.h"

//#define DEBUG

using namespace std;

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using proto::DirListing;
using proto::Contents;
using proto::FileInfo;
using proto::PutParameters;
using proto::RenameParameters;
using proto::FileSystem;
using proto::Path;
using proto::Success;

struct MYFILE {
    MYFILE(int d, const char *cfp, const char *tfp) : descriptor(d), cache_filepath(cfp), tmp_filepath(tfp), written_to(0) {}
    ~MYFILE() {
        delete cache_filepath;
        delete tmp_filepath;
    }

    int descriptor;
    const char *cache_filepath;
    const char *tmp_filepath;
    bool written_to;
};

static const char *cache_path = "cache";
static const char *server; 
static ofstream logfile;

static unique_ptr<FileSystem::Stub> stub;

int cp(const char *from, const char *to)
{
    int fd_to, fd_from;
    char buf[4096];
    ssize_t nread;
    int saved_errno;

    fd_from = open(from, O_RDONLY);
    if (fd_from < 0)
        return -1;

    fd_to = open(to, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd_to < 0)
        goto out_error;

    while (nread = read(fd_from, buf, sizeof buf), nread > 0)
    {
        char *out_ptr = buf;
        ssize_t nwritten;

        do {
            nwritten = write(fd_to, out_ptr, nread);

            if (nwritten >= 0)
            {
                nread -= nwritten;
                out_ptr += nwritten;
            }
            else if (errno != EINTR)
            {
                goto out_error;
            }
        } while (nread > 0);
    }

    if (nread == 0)
    {
        if (close(fd_to) < 0)
        {
            fd_to = -1;
            goto out_error;
        }
        close(fd_from);

        /* Success! */
        return 0;
    }

  out_error:
    saved_errno = errno;

    close(fd_from);
    if (fd_to >= 0)
        close(fd_to);

    errno = saved_errno;
    return -1;
}

static int mkdirp(const char *path) {
    struct stat status;
    if(stat(path, &status) == -1) {
        if (mkdir(path, 0777 ) == -1) {
            if (errno == ENOENT) {
                if (mkdirp(dirname(strdup(path))) == -1) {
                    return -1;
                }
                if (mkdir(path, 0777 ) == -1) {
                    return -1;
                }
            } else {
                return -1;
            }
        }
    }
}

static int createFullPath(const char *path) {
    if (mkdirp(dirname(strdup(path))) == -1) { return -1; }
    FILE *f = fopen(path, "w");
    fclose(f);
}

static const char *adjustPath(const char *relPath) {
    char *path = new char[strlen(relPath)+strlen(cache_path)+2]();
    strcat(path, cache_path);
    strcat(path, "/");
    strcat(path, relPath);

    return path;
}

static int fs_truncate(const char *path, off_t size) {
    if (size != 0) {
        return -EOPNOTSUPP;
    }

    Success s;
    bool success = false;
    while (!success) {
        ClientContext context;
        PutParameters params;
        params.set_path(path);
        params.set_contents("");
        Status status = stub->Put(&context, params, &s);
        if (status.ok()) {
            success = true;
        }
    }

    return 0;
}

static int fs_getattr(const char *path, struct stat *stbuf)
{
    FileInfo info;
    int res = 0;

    bool success = false;
    while (!success) {
        Path p;
        p.set_path(path);
        ClientContext context;
        Status status = stub->Stat(&context, p, &info);
        if (status.ok()) {
            success = true;
        }
    }
    if (info.direxists()) {
        return -ENOENT;
    }

    memset(stbuf, 0, sizeof(struct stat));


    if (info.isdir()) {
        stbuf->st_mode = S_IFDIR | 0777;
        stbuf->st_nlink = 2;
    } else {
        stbuf->st_mode = S_IFREG | 0777;
        stbuf->st_nlink = 1;
        stbuf->st_size = info.size();
        stbuf->st_mtime = info.modtime();
    }

    return res;
}

static int fs_mknod(const char *path, mode_t mode, dev_t dev) {

    Success s;
    bool success = false;
    while (!success) {
        ClientContext context;
        PutParameters params;
        params.set_path(path);
        params.set_contents("");
        Status status = stub->Put(&context, params, &s);
        if (status.ok()) {
            success = true;
        }
    }

    return 0;
}
static int fs_mkdir(const char *path, mode_t mode) {
    ClientContext context;
    Path p;
    p.set_path(path);
    Success success;
    Status status = stub->MkDir(&context, p, &success);
    if (!status.ok()) {
        return -ENOENT;
    }

    return 0;
}
static int fs_unlink(const char *path) {
    ClientContext context;
    Path p;
    p.set_path(path);
    Success success;
    Status status = stub->Unlink(&context, p, &success);
    if (!status.ok()) {
        return -ENOENT;
    }

    return 0;
}
static int fs_rmdir(const char *path) {
    ClientContext context;
    Path p;
    p.set_path(path);
    Success success;
    Status status = stub->RmDir(&context, p, &success);
    if (!status.ok()) {
        return -ENOENT;
    }

    return 0;
}
static int fs_rename(const char *path, const char *newpath) {
    ClientContext context;
    RenameParameters params;
    params.set_old(path);
    params.set_new_(newpath);
    Success success;
    Status status = stub->Rename(&context, params, &success);
    if (!status.ok()) {
        return -ENOENT;
    }

    return 0;
}


static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
    #ifdef DEBUG
    logfile.open("client.log", ios::app);
    logfile << "reading dir\n";
    #endif
    (void) offset;
    (void) fi;

    Path p;
    p.set_path(path);


    DirListing dirListing;
    ClientContext context;
    Status status = stub->ReadDir(&context, p, &dirListing);

    if (!status.ok()) {
        #ifdef DEBUG
        logfile << "readdir rpc failed.\n";
        logfile << status.error_message();
        #endif
        return -EACCES;
    }

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    for (int i = 0; i < dirListing.names_size(); i++) {
        filler(buf, dirListing.names(i).c_str(), NULL, 0);
    }
    #ifdef DEBUG
    logfile.close();
    #endif

    return 0;
}

static MYFILE *open_atomic(const char *path, int flags) {
    char *tmptemplate = new char[11];
    strcpy(tmptemplate, "tmp/XXXXXX");
    char *tmpfilepath = mktemp(tmptemplate);
    if (tmpfilepath == NULL) {
        return NULL;
    }

    if (cp(path, tmpfilepath) == -1) {
        // Should probably check if tmp exists
        return NULL;
    }

    int fd = open(tmpfilepath, flags);
    if (fd == -1) {
        return NULL;
    }
    return new MYFILE(fd, path, tmpfilepath);
}

static int close_atomic(MYFILE *f) {

    if (fsync(f->descriptor) == -1 
        || close(f->descriptor) == -1
        || rename(f->tmp_filepath, f->cache_filepath) == -1
        ) {
        delete f;
        return -1;
    } 

    delete f;
    return 0;
}

static char *get_file_contents_atomic(MYFILE *f) {
    struct stat info;
    int readfd;
    if (   fsync(f->descriptor) == -1
        || (readfd = open(f->tmp_filepath, O_RDONLY)) == -1
        || fstat(readfd, &info) == -1) {
        close(readfd);
        return NULL;
    }

    char *contents = (char*)malloc(info.st_size+1); // +1 for terminating char

    if (read(readfd, contents, info.st_size) != info.st_size) {
        delete contents;
        close(readfd);
        return NULL;
    }
    contents[info.st_size] = 0;
    close(readfd);

    return contents;
}

static int fs_open(const char *path, struct fuse_file_info *fi) {
    Path p;
    p.set_path(path);

    // Initialize these here so jump doesn't cross
    // initialization
    Contents contents;
    ClientContext context2;
    FILE *tf;
    const char *c;

    // Get modified date
    FileInfo info;
    ClientContext context;
    Status status = stub->Stat(&context, p, &info);
    // If file doesn't exist and directory doesn't exist,
    // indicate as such. This is kind of sloppy; other
    // errors might be shoehorned into this
    if (!status.ok()) {
        return -ENOENT;
    }

    const char *absPath = adjustPath(path);
    // if file doesn't exist, create cache file
    if (info.direxists()) {
        tf = fopen(absPath, "w+");
        fclose(tf);

        goto no_server_read;

    } else {
        struct stat cached_info;
        int stat_error = stat(absPath, &cached_info);
        if (stat_error == -1) {
            // If cache entry doesn't exist, create it
            // and read from server
            if (errno == ENOENT) {
                if (createFullPath(absPath) == -1) {
                    return -EACCES;
                } 

                goto need_server_read;

            } else {
                return -EACCES;
            }
        } else {
            // If cache entry is not stale then don't do server read
            if (info.modtime() <= cached_info.st_mtim.tv_sec) {
                goto no_server_read;
            } else {
                // Otherwise read from server
                goto need_server_read;
            }
        }
    }

need_server_read:
    status = stub->Get(&context2, p, &contents);
    if (!status.ok()) {
        delete absPath;
        return -ENOENT;
    }

    tf = fopen(absPath, "w");
    c = contents.contents().c_str();
    fwrite(c, sizeof(char), strlen(c), tf);
    fclose(tf);
no_server_read:
    MYFILE *mf = open_atomic(absPath, fi->flags);
    if (mf == NULL) {
        return -EACCES;
    }


    fi->fh = reinterpret_cast<uint64_t>(mf);
    return 0;
}

static int fs_read(const char *path, char *buf, size_t size, off_t offset,
        struct fuse_file_info *fi)
{
    MYFILE *mf = reinterpret_cast<MYFILE*>(fi->fh);
    return pread(mf->descriptor, buf, size, offset);
}

int fs_write(const char *path, const char *buf, size_t size, off_t offset,
	     struct fuse_file_info *fi)
{
    MYFILE *mf = reinterpret_cast<MYFILE*>(fi->fh);
    mf->written_to = true;
    return pwrite(mf->descriptor, buf, size, offset);
}


int fs_flush(const char *path, struct fuse_file_info *fi)
{
    MYFILE *mf = reinterpret_cast<MYFILE*>(fi->fh);

    if (mf->written_to) {
        PutParameters params;

        char *contents = get_file_contents_atomic(mf);
        if (contents == NULL) {
            return -EACCES;
        }

        // Send contents to server
        params.set_path(path);
        params.set_contents(contents);

        ClientContext context;
        Success success;
        Status status = stub->Put(&context, params, &success);
        if (!status.ok()) {
            delete contents;
            return -EACCES;
        }

        delete contents;
    }
    return 0;
}

int fs_release(const char *path, struct fuse_file_info *fi)
{
    MYFILE *mf = reinterpret_cast<MYFILE*>(fi->fh);
    return close_atomic(mf);
}

int main(int argc, char *argv[])
{
    if (argc < 4) {
        cerr << "Usage: client <server address> <FUSE args>\n";
        return 1;
    }

    // set server address
    char *s = new char[strlen(argv[1])+5];
    s[0] = 0;
    strcat(s, argv[1]);
    strcat(s, ":7890");
    server = s;

    stub = FileSystem::NewStub(
            grpc::CreateChannel(server, 
                                grpc::InsecureCredentials()));

    static struct fuse_operations fs_oper;
    fs_oper.truncate = fs_truncate;
    fs_oper.mknod = fs_mknod;
    fs_oper.mkdir = fs_mkdir;
    fs_oper.unlink = fs_unlink;
    fs_oper.rename = fs_rename;
    fs_oper.rmdir = fs_rmdir;
    fs_oper.getattr = fs_getattr;
    fs_oper.readdir = fs_readdir;
    fs_oper.open = fs_open;
    fs_oper.read = fs_read;
    fs_oper.write = fs_write;
    fs_oper.flush = fs_flush;
    fs_oper.release = fs_release;

    // don't pass cache_path arg to fuse
    memmove(argv, &argv[1], argc-1);

    return fuse_main(argc-1, &argv[1], &fs_oper, NULL);
}
