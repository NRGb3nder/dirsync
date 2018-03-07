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
#define FLIST_SIZE 256
#define CHAR_BUF_SIZE 256
#define COPY_BUF_SIZE 512
#define MIN_RUNNING_PROC 2

typedef int smode_t;

void printerr(const char *module, const char *errmsg, const char *filename);
bool isdir(const char *path);
bool isreg(const char *path);
int sync_dirs(const char *dir1_path, const char *dir2_path, long max_running_proc);
int fill_flist(const char *dirpath, char **files);
void free_flist(char **files);
bool is_in_flist(const char *filepath, char **flist);
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
    char **dir1_files;
    char **dir2_files;
    dir1_files = malloc(FLIST_SIZE * sizeof(char *));
    dir2_files = malloc(FLIST_SIZE * sizeof(char *));

    if (fill_flist(dir1_path, dir1_files) == -1) {
        return 1;
    }
    if (fill_flist(dir2_path, dir2_files) == -1) {
        return 1;
    }

    long process_counter = 1;

    for (int i = 0; dir1_files[i]; i++) {
        if (!is_in_flist(dir1_files[i], dir2_files)) {
            if (process_counter == max_running_proc) {
                wait(NULL);
            }
            pid_t pid = fork();
            if (!pid) {
                if (fumask(dir1_files[i]) != -1) {
                    int bytes_copied = 0;
                    ssize_t fcopy_result = fcopy(dir1_files[i], dir2_path);
                    if (fcopy_result > 0) {
                        printf("pid: %d; source: %s; bytes copied: %d\n",
                            getpid(), dir1_files[i], fcopy_result);
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

int fill_flist(const char *dirpath, char **files)
{
    DIR *currdir;
    if (!(currdir = opendir(dirpath))) {
        printerr(module, strerror(errno), dirpath);
        return 1;
    }

    int fnum = 0;

    struct dirent *cdirent;
    while (cdirent = readdir(currdir)) {
        char fullpath[PATH_MAX];
        strcpy(fullpath, dirpath);
        strcat(fullpath, "/");
        strcat(fullpath, cdirent->d_name);

        if (isreg(fullpath)) {
            files[fnum] = malloc(PATH_MAX * sizeof(char));
            strcpy(files[fnum++], fullpath);
        }
    }

    files[fnum] = NULL;

    closedir(currdir);
}

void free_flist(char **files)
{
    for (int i = 0; files[i]; i++) {
        free(files[i]);
    }
    free(files);
}

bool is_in_flist(const char *filepath, char **flist)
{
    for (int i = 0; flist[i]; i++) {
        if (!strcmp(basename(filepath), basename(flist[i]))) {
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
                    } else {
                        if (errno != EINTR) {
                            printerr(module, strerror(errno), filepath_to);
                            is_rdwrerror = true;
                        }
                    }
                }
            } while (!is_rdwrerror && rdbytes);
        } else {
            if (errno != EINTR) {
                printerr(module, strerror(errno), filepath_from);
                is_rdwrerror = true;
            }
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
