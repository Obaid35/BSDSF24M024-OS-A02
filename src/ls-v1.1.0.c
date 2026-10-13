/*
* Programming Assignment 02: ls-v1.1.0
* Feature-2: Complete Long Listing Format (-l)
*
* Usage:
*       $ ls                 (simple listing, one name per line)
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
static void do_ls_long(const char *dir);
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

    /* getopt() walks over the command line and returns one option letter per call */
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

    /* After getopt() finishes, optind is the index of the first non-option argument */
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
    if (long_flag)
    {
        do_ls_long(dir);
        return;
    }

    /* Original simple behaviour of v1.0.0 */
    struct dirent *entry;
    DIR *dp = opendir(dir);
    if (dp == NULL)
    {
        fprintf(stderr, "Cannot open directory : %s\n", dir);
        return;
    }
    errno = 0;
    while ((entry = readdir(dp)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;
        printf("%s\n", entry->d_name);
    }
    if (errno != 0)
    {
        perror("readdir failed");
    }
    closedir(dp);
}

/*
 * Long listing. Two passes over the directory:
 *   pass 1: lstat() every entry, find the widest value of each column, add up blocks
 *   pass 2: print every entry using those widths so the columns line up
 */
static void do_ls_long(const char *dir)
{
    struct dirent *entry;
    struct stat st;
    struct widths w = {1, 1, 1, 1};
    char path[PATH_MAX];
    char buf[64];
    long long total = 0;

    DIR *dp = opendir(dir);
    if (dp == NULL)
    {
        fprintf(stderr, "Cannot open directory : %s\n", dir);
        return;
    }

    /* ---- pass 1 ---- */
    errno = 0;
    while ((entry = readdir(dp)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;
        build_path(dir, entry->d_name, path, sizeof(path));
        if (lstat(path, &st) == -1)
        {
            perror(path);
            continue;
        }
        total += st.st_blocks; /* st_blocks counts 512-byte units */

        int n = num_width(st.st_nlink);
        if (n > w.links) w.links = n;

        owner_name(st.st_uid, buf, sizeof(buf));
        n = (int)strlen(buf);
        if (n > w.user) w.user = n;

        group_name(st.st_gid, buf, sizeof(buf));
        n = (int)strlen(buf);
        if (n > w.group) w.group = n;

        size_str(&st, buf, sizeof(buf));
        n = (int)strlen(buf);
        if (n > w.size) w.size = n;
    }
    if (errno != 0)
        perror("readdir failed");

    /* ls prints blocks in 1024-byte units, st_blocks is in 512-byte units */
    printf("total %lld\n", total / 2);

    /* ---- pass 2 ---- */
    rewinddir(dp);
    while ((entry = readdir(dp)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;
        build_path(dir, entry->d_name, path, sizeof(path));
        if (lstat(path, &st) == -1)
            continue;
        print_long_entry(path, entry->d_name, &st, &w);
    }
    closedir(dp);
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

    /* ctime() returns "Wed Jun 30 21:49:08 1993\n"; characters 4..15 are "Jun 30 21:49" */
    time_t mtime = st->st_mtime;
    char *t = ctime(&mtime);
    time_t now = time(NULL);
    char when[32];
    if (now - mtime > 15778476 || mtime > now) /* older than ~6 months (or in the future) */
        snprintf(when, sizeof(when), "%.6s  %.4s", t + 4, t + 20); /* "Jun 30  1993" */
    else
        snprintf(when, sizeof(when), "%.12s", t + 4);              /* "Jun 30 21:49" */

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

/* "dir" + "name" -> "dir/name" (no double slash if dir already ends with '/') */
static void build_path(const char *dir, const char *name, char *buf, size_t size)
{
    size_t len = strlen(dir);
    if (len > 0 && dir[len - 1] == '/')
        snprintf(buf, size, "%s%s", dir, name);
    else
        snprintf(buf, size, "%s/%s", dir, name);
}

/* st_mode -> "drwxr-xr-x" */
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

    /* special bits replace the 'x' position: s/S (setuid, setgid), t/T (sticky) */
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
        snprintf(buf, size, "%u", (unsigned)uid); /* no such user: show the number */
}

static void group_name(gid_t gid, char *buf, size_t size)
{
    struct group *gr = getgrgid(gid);
    if (gr != NULL)
        snprintf(buf, size, "%s", gr->gr_name);
    else
        snprintf(buf, size, "%u", (unsigned)gid);
}

/* Size column: normal files show bytes, device files show "major, minor" */
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
