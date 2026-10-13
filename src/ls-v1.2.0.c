/*
* Programming Assignment 02: ls-v1.2.0
* Feature-3: Column Display (down then across)
*
* Usage:
*       $ ls                 (multi-column listing, "down then across")
*       $ ls -l              (long listing)
*       $ ls -l /home /etc
*/
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>

extern int errno;

/* Column widths needed to align the long listing */
struct widths {
    int links;
    int user;
    int group;
    int size;
};

void do_ls(const char *dir, int long_flag);
static int  read_names(const char *dir, char ***out, int *count, size_t *maxlen);
static void free_names(char **names, int count);
static int  term_width(void);
static void print_columns(char **names, int count, size_t maxlen);
static void print_long(const char *dir, char **names, int count);
static void print_long_entry(const char *path, const char *name,
                             const struct stat *st, const struct widths *w);
static void build_path(const char *dir, const char *name, char *buf, size_t size);
static void mode_to_str(mode_t mode, char *str);
static void owner_name(uid_t uid, char *buf, size_t size);
static void group_name(gid_t gid, char *buf, size_t size);
static void size_str(const struct stat *st, char *buf, size_t size);
static int  num_width(unsigned long long n);

int main(int argc, char *argv[])
{
    int long_flag = 0;
    int opt;

    while ((opt = getopt(argc, argv, "l")) != -1)
    {
        switch (opt)
        {
        case 'l':
            long_flag = 1;
            break;
        default:
            fprintf(stderr, "Usage: %s [-l] [directory...]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (optind == argc)
    {
        do_ls(".", long_flag);
    }
    else
    {
        for (int i = optind; i < argc; i++)
        {
            printf("Directory listing of %s : \n", argv[i]);
            do_ls(argv[i], long_flag);
            puts("");
        }
    }
    return 0;
}

void do_ls(const char *dir, int long_flag)
{
    char **names = NULL;
    int count = 0;
    size_t maxlen = 0;

    /* Step 1: read ALL the names into memory first */
    if (read_names(dir, &names, &count, &maxlen) == -1)
        return;

    /* Step 2: display */
    if (long_flag)
        print_long(dir, names, count);
    else
        print_columns(names, count, maxlen);

    /* Step 3: give the memory back */
    free_names(names, count);
}

/*
 * Reads every (non-hidden) name of a directory into a dynamically growing
 * array of strings. Also reports the length of the longest name.
 * Returns 0 on success, -1 if the directory cannot be opened.
 */
static int read_names(const char *dir, char ***out, int *count, size_t *maxlen)
{
    DIR *dp = opendir(dir);
    if (dp == NULL)
    {
        fprintf(stderr, "Cannot open directory : %s\n", dir);
        return -1;
    }

    char **names = NULL;
    int n = 0, capacity = 0;
    size_t longest = 0;
    struct dirent *entry;

    errno = 0;
    while ((entry = readdir(dp)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;

        if (n == capacity) /* array full: double its size */
        {
            capacity = (capacity == 0) ? 16 : capacity * 2;
            char **tmp = realloc(names, capacity * sizeof(char *));
            if (tmp == NULL)
            {
                perror("realloc failed");
                exit(EXIT_FAILURE);
            }
            names = tmp;
        }

        names[n] = strdup(entry->d_name); /* own copy: entry is reused by readdir */
        if (names[n] == NULL)
        {
            perror("strdup failed");
            exit(EXIT_FAILURE);
        }
        size_t len = strlen(names[n]);
        if (len > longest)
            longest = len;
        n++;
    }
    if (errno != 0)
        perror("readdir failed");

    closedir(dp);
    *out = names;
    *count = n;
    *maxlen = longest;
    return 0;
}

static void free_names(char **names, int count)
{
    for (int i = 0; i < count; i++)
        free(names[i]);
    free(names);
}

/* Terminal width in characters; falls back to 80 if it cannot be found */
static int term_width(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
    return 80;
}

/*
 * "Down then across":
 *      a  d  g
 *      b  e  h
 *      c  f
 * item index for (row r, column c) is  r + c * rows
 */
static void print_columns(char **names, int count, size_t maxlen)
{
    if (count == 0)
        return;

    int width = term_width();
    int colw = (int)maxlen + 2;      /* longest name + 2 spaces between columns */
    int cols = width / colw;
    if (cols < 1)     cols = 1;      /* a very long name: one per line */
    if (cols > count) cols = count;
    int rows = (count + cols - 1) / cols; /* ceiling of count / cols */

    for (int r = 0; r < rows; r++)
    {
        for (int c = 0; c < cols; c++)
        {
            int idx = r + c * rows;
            if (idx >= count)
                break;
            printf("%s", names[idx]);
            /* pad only if another item follows on this row */
            if (idx + rows < count && c + 1 < cols)
                printf("%*s", colw - (int)strlen(names[idx]), "");
        }
        putchar('\n');
    }
}

static void print_long(const char *dir, char **names, int count)
{
    struct stat *sts = malloc((count > 0 ? count : 1) * sizeof(struct stat));
    int *ok = calloc(count > 0 ? count : 1, sizeof(int));
    if (sts == NULL || ok == NULL)
    {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }

    struct widths w = {1, 1, 1, 1};
    char path[PATH_MAX], buf[64];
    long long total = 0;

    for (int i = 0; i < count; i++)
    {
        build_path(dir, names[i], path, sizeof(path));
        if (lstat(path, &sts[i]) == -1)
        {
            perror(path);
            continue;
        }
        ok[i] = 1;
        total += sts[i].st_blocks;

        int n = num_width(sts[i].st_nlink);
        if (n > w.links) w.links = n;
        owner_name(sts[i].st_uid, buf, sizeof(buf));
        n = (int)strlen(buf);
        if (n > w.user) w.user = n;
        group_name(sts[i].st_gid, buf, sizeof(buf));
        n = (int)strlen(buf);
        if (n > w.group) w.group = n;
        size_str(&sts[i], buf, sizeof(buf));
        n = (int)strlen(buf);
        if (n > w.size) w.size = n;
    }

    printf("total %lld\n", total / 2);

    for (int i = 0; i < count; i++)
    {
        if (!ok[i])
            continue;
        build_path(dir, names[i], path, sizeof(path));
        print_long_entry(path, names[i], &sts[i], &w);
    }
    free(sts);
    free(ok);
}

static void print_long_entry(const char *path, const char *name,
                             const struct stat *st, const struct widths *w)
{
    char perms[11];
    char user[64], group[64], size[64];
    char target[PATH_MAX];

    mode_to_str(st->st_mode, perms);
    owner_name(st->st_uid, user, sizeof(user));
    group_name(st->st_gid, group, sizeof(group));
    size_str(st, size, sizeof(size));

    time_t mtime = st->st_mtime;
    char *t = ctime(&mtime);
    time_t now = time(NULL);
    char when[32];
    if (now - mtime > 15778476 || mtime > now)
        snprintf(when, sizeof(when), "%.6s  %.4s", t + 4, t + 20);
    else
        snprintf(when, sizeof(when), "%.12s", t + 4);

    printf("%s %*lu %-*s %-*s %*s %s %s",
           perms,
           w->links, (unsigned long)st->st_nlink,
           w->user, user,
           w->group, group,
           w->size, size,
           when, name);

    if (S_ISLNK(st->st_mode))
    {
        ssize_t len = readlink(path, target, sizeof(target) - 1);
        if (len != -1)
        {
            target[len] = '\0';
            printf(" -> %s", target);
        }
    }
    putchar('\n');
}

static void build_path(const char *dir, const char *name, char *buf, size_t size)
{
    size_t len = strlen(dir);
    if (len > 0 && dir[len - 1] == '/')
        snprintf(buf, size, "%s%s", dir, name);
    else
        snprintf(buf, size, "%s/%s", dir, name);
}

static void mode_to_str(mode_t mode, char *str)
{
    if      (S_ISREG(mode))  str[0] = '-';
    else if (S_ISDIR(mode))  str[0] = 'd';
    else if (S_ISLNK(mode))  str[0] = 'l';
    else if (S_ISCHR(mode))  str[0] = 'c';
    else if (S_ISBLK(mode))  str[0] = 'b';
    else if (S_ISFIFO(mode)) str[0] = 'p';
    else if (S_ISSOCK(mode)) str[0] = 's';
    else                     str[0] = '?';

    str[1] = (mode & S_IRUSR) ? 'r' : '-';
    str[2] = (mode & S_IWUSR) ? 'w' : '-';
    str[3] = (mode & S_IXUSR) ? 'x' : '-';
    str[4] = (mode & S_IRGRP) ? 'r' : '-';
    str[5] = (mode & S_IWGRP) ? 'w' : '-';
    str[6] = (mode & S_IXGRP) ? 'x' : '-';
    str[7] = (mode & S_IROTH) ? 'r' : '-';
    str[8] = (mode & S_IWOTH) ? 'w' : '-';
    str[9] = (mode & S_IXOTH) ? 'x' : '-';
    str[10] = '\0';

    if (mode & S_ISUID) str[3] = (mode & S_IXUSR) ? 's' : 'S';
    if (mode & S_ISGID) str[6] = (mode & S_IXGRP) ? 's' : 'S';
    if (mode & S_ISVTX) str[9] = (mode & S_IXOTH) ? 't' : 'T';
}

static void owner_name(uid_t uid, char *buf, size_t size)
{
    struct passwd *pw = getpwuid(uid);
    if (pw != NULL)
        snprintf(buf, size, "%s", pw->pw_name);
    else
        snprintf(buf, size, "%u", (unsigned)uid);
}

static void group_name(gid_t gid, char *buf, size_t size)
{
    struct group *gr = getgrgid(gid);
    if (gr != NULL)
        snprintf(buf, size, "%s", gr->gr_name);
    else
        snprintf(buf, size, "%u", (unsigned)gid);
}

static void size_str(const struct stat *st, char *buf, size_t size)
{
    if (S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode))
        snprintf(buf, size, "%u, %u", major(st->st_rdev), minor(st->st_rdev));
    else
        snprintf(buf, size, "%lld", (long long)st->st_size);
}

static int num_width(unsigned long long n)
{
    int w = 1;
    while (n >= 10)
    {
        n /= 10;
        w++;
    }
    return w;
}
