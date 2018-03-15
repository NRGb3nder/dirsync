#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>

#define VALID_ARGC 4
#define CHAR_BUF_SIZE 256
#define COPY_BUF_SIZE 512
#define MIN_RUNNING_PROC 2

typedef int smode_t;
struct flist_node {
    char filename[PATH_MAX];
    struct flist_node *next;
};

void printerr(const char *module, const char *errmsg, const char *filename);
bool isdir(const char *path);
bool isreg(const char *path);
int sync_dirs(const char *dir1_path, const char *dir2_path, long max_running_proc);
struct flist_node *flist_node_alloc();
int fill_flist(const char *dirpath, struct flist_node *files);
void free_flist(struct flist_node *files);
bool is_in_flist(const char *filepath, struct flist_node *files);
ssize_t fcopy(const char *filepath_from, const char *dirpath_to);
smode_t fumask(const char *filepath);

char *module;

int main(int argc, char *argv[], char *envp[])
{
    module = basename(argv[0]);

    if (argc < VALID_ARGC) {
        printerr(module, "Too few arguments", NULL);
        return 1;
    }
    if (!isdir(argv[1])) {
        printerr(module, "Not a directory", argv[1]);
        return 1;
    }
    if (!isdir(argv[2])) {
        printerr(module, "Not a directory", argv[2]);
        return 1;
    }

    char dir1_realpath[PATH_MAX];
    char dir2_realpath[PATH_MAX];
    realpath(argv[1], dir1_realpath);
    realpath(argv[2], dir2_realpath);
    if (!strcmp(dir1_realpath, dir2_realpath)) {
        printerr(module, "Can not sync directory with itself", NULL);
        return 1;
    }

    long max_running_proc;
    if (!(max_running_proc = strtol(argv[3], NULL, 10))) {
        printerr(module, "Maximum of running processes is not an integer", NULL);
        return 1;
    }
    if (errno == ERANGE) {
        printerr(module, strerror(errno), NULL);
        return 1;
    }
    if (max_running_proc <= 1) {
        char errmsg[CHAR_BUF_SIZE];
        sprintf(errmsg, "Maximum of running processes must be greater or equal to %d",
            MIN_RUNNING_PROC);

        printerr(module, errmsg, NULL);
        return 1;
    }

    if (!sync_dirs(argv[1], argv[2], max_running_proc)) {
        return 0;
    } else {
        return 1;
    }
}

void printerr(const char *module, const char *errmsg, const char *filename)
{
    fprintf(stderr, "%s: %s %s\n", module, errmsg, filename ? filename : "");
}

bool isdir(const char *path)
{
    struct stat statbuf;

    if (lstat(path, &statbuf) == -1) {
        printerr(module, strerror(errno), path);
        return false;
    }

    return S_ISDIR(statbuf.st_mode);
}

bool isreg(const char *path)
{
    struct stat statbuf;

    if (lstat(path, &statbuf) == -1) {
        printerr(module, strerror(errno), path);
        return false;
    }

    return S_ISREG(statbuf.st_mode);
}

int sync_dirs(const char *dir1_path, const char *dir2_path, long max_running_proc)
{
    struct flist_node *dir1_files = flist_node_alloc();

    // TODO?: I guess, it's a good idea to make this one some kind of a tree, but meeeeeh
    struct flist_node *dir2_files = flist_node_alloc();

    if (fill_flist(dir1_path, dir1_files) == -1) {
        return 1;
    }
    if (fill_flist(dir2_path, dir2_files) == -1) {
        return 1;
    }

    long process_counter = 1;
    struct flist_node *curr_node = dir1_files;

    while (curr_node->next) {
        curr_node = curr_node->next;
        if (!is_in_flist(curr_node->filename, dir2_files)) {
            if (process_counter == max_running_proc) {
                wait(NULL);
            }
            pid_t pid = fork();
            if (!pid) {
                if (fumask(curr_node->filename) != -1) {
                    int bytes_copied = 0;
                    ssize_t fcopy_result = fcopy(curr_node->filename, dir2_path);
                    if (fcopy_result > 0) {
                        printf("pid: %d; source: %s; bytes copied: %d\n",
                            getpid(), curr_node->filename, fcopy_result);
                        exit(0);
                    }
                    exit(1);
                }
                exit(1);
            } else if (pid == -1) {
                printerr(module, strerror(errno), NULL);
            }
        }
    }

    while (wait(NULL) != -1) {};

    free_flist(dir1_files);
    free_flist(dir2_files);

    return 0;
}

struct flist_node *flist_node_alloc()
{
    struct flist_node *node;
    node = malloc(sizeof(struct flist_node));
    node->next = NULL;

    return node;
}

int fill_flist(const char *dirpath, struct flist_node *files)
{
    DIR *currdir;
    if (!(currdir = opendir(dirpath))) {
        printerr(module, strerror(errno), dirpath);
        return 1;
    }

    struct flist_node *curr_node = files;

    struct dirent *cdirent;
    while (cdirent = readdir(currdir)) {
        char fullpath[PATH_MAX];
        strcpy(fullpath, dirpath);
        strcat(fullpath, "/");
        strcat(fullpath, cdirent->d_name);

        if (isreg(fullpath)) {
            curr_node->next = flist_node_alloc();
            curr_node = curr_node->next;
            strcpy(curr_node->filename, fullpath);
        }
    }

    closedir(currdir);
}

void free_flist(struct flist_node *files)
{
    struct flist_node *curr_node;

    while ((curr_node = files) != NULL) {
        files = files->next;
        free(curr_node);
    }
}

bool is_in_flist(const char *filepath, struct flist_node *files)
{
    struct flist_node *curr_node = files;

    while (curr_node->next) {
        curr_node = curr_node->next;
        if (!strcmp(basename(filepath), basename(curr_node->filename))) {
            return true;
        }
    }
    return false;
}

ssize_t fcopy(const char *filepath_from, const char *dirpath_to)
{
    int source_fd;
    if ((source_fd = open(filepath_from, O_RDONLY)) == -1) {
        printerr(module, strerror(errno), filepath_from);
        return -1;
    }

    char filepath_to[PATH_MAX];
    realpath(dirpath_to, filepath_to);
    strcat(filepath_to, "/");
    strcat(filepath_to, basename(filepath_from));

    int dest_fd;
    if ((dest_fd = open(filepath_to, O_CREAT | O_WRONLY | O_EXCL)) == -1) {
        printerr(module, strerror(errno), filepath_to);
        if (close(source_fd) == -1) {
            printerr(module, strerror(errno), filepath_from);
        }
        return -1;
    }

    ssize_t wrbytes_total = 0;
    ssize_t rdbytes;
    char buf[COPY_BUF_SIZE];
    bool is_rdwrerror = false;

    while (!is_rdwrerror && (rdbytes = read(source_fd, buf, COPY_BUF_SIZE))) {
        ssize_t wrbytes;
        char *buf_pos_pointer = buf;

        if (rdbytes != -1) {
            do {
                wrbytes = write(dest_fd, buf_pos_pointer, rdbytes);
                if (wrbytes) {
                    if (wrbytes != -1) {
                        rdbytes -= wrbytes;
                        buf_pos_pointer += wrbytes;
                        wrbytes_total += wrbytes;
                    } else if (errno != EINTR) {
                        printerr(module, strerror(errno), filepath_to);
                        is_rdwrerror = true;
                    }
                }
            } while (!is_rdwrerror && rdbytes);
        } else if (errno != EINTR) {
            printerr(module, strerror(errno), filepath_from);
            is_rdwrerror = true;
        }
    }

    if (close(source_fd) == -1) {
        printerr(module, strerror(errno), filepath_from);
    }
    if (close(dest_fd) == -1) {
        printerr(module, strerror(errno), filepath_to);
    }

    return wrbytes_total;
}

smode_t fumask(const char *filepath)
{
    struct stat statbuf;

    if (lstat(filepath, &statbuf) == -1) {
        printerr(module, strerror(errno), filepath);
        return -1;
    }

    return umask(statbuf.st_mode);
}
