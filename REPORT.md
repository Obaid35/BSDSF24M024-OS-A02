# PA-02 — Report

**Name:** Obaid ur Rehman
**Roll Number:** BSDSF24M024
**Repository:** https://github.com/Obaid35/BSDSF24M024-OS-A02

---

## Feature 2 — ls-v1.1.0: Complete Long Listing Format

### Q1. What is the crucial difference between `stat()` and `lstat()`? In the context of `ls`, when is it more appropriate to use `lstat()`?

Both system calls fill a `struct stat` with the metadata of a file (type, permissions, owner, size, timestamps, link count). They differ only in what they do when the path is a **symbolic link**:

- `stat()` **follows** the link and returns the metadata of the file the link points to (the *target*).
- `lstat()` does **not** follow it and returns the metadata of the **link itself**.

`ls` should use `lstat()`. In `ls -l` a symbolic link must be shown as type `l`, with its own permissions (`lrwxrwxrwx`), its own size (the number of characters in the path it stores) and the arrow `name -> target`. If I used `stat()`, the link would be reported as the type of its target (for example `-` for a regular file or `d` for a directory) and I would not know it is a link at all. A *dangling* link (target deleted) would even make `stat()` fail with `ENOENT`, although the link itself exists in the directory. The same reasoning matters in the recursive feature: `lstat()` keeps `S_ISDIR()` false for a link to a directory, so the program cannot loop forever through a link that points back to a parent directory.

### Q2. The `st_mode` field contains both the file type and the permission bits. How can bitwise operators and macros such as `S_IFDIR` or `S_IRUSR` be used to extract this information?

`st_mode` is an integer whose bits are divided into groups:

```
 type (4 bits) | special (3 bits: setuid, setgid, sticky) | owner rwx | group rwx | others rwx
```

To read one piece of information, I keep only the bits I am interested in using the bitwise AND (`&`) with a **mask**, and then compare.

- **File type:** `S_IFMT` (octal `0170000`) is the mask of the 4 type bits. `(st_mode & S_IFMT) == S_IFDIR` is true for a directory. The macros `S_ISDIR(m)`, `S_ISREG(m)`, `S_ISLNK(m)` and others do exactly this.
- **A permission bit:** each permission has its own single-bit constant, for example `S_IRUSR` (`0400`), `S_IWUSR` (`0200`), `S_IXUSR` (`0100`), then the same for group (`S_IRGRP`...) and others (`S_IROTH`...). `st_mode & S_IRUSR` is non-zero when the owner may read the file, and zero otherwise.
- **Special bits:** `S_ISUID` (`04000`), `S_ISGID` (`02000`) and `S_ISVTX` (`01000`). In my code they replace the `x` character by `s`/`S` (setuid, setgid) or `t`/`T` (sticky). The lower-case letter means the execute bit is also set and the upper-case letter means it is not.

In `mode_to_str()` I test each of the nine permission bits with `&` and write `r`, `w`, `x` or `-` into a 10-character string, and I use the `S_IS*` macros for the first character.

---

## Feature 3 — ls-v1.2.0: Column Display (Down Then Across)

### Q1. Explain the general logic for printing items in a "down then across" columnar format. Why is a simple single loop through the list of filenames insufficient?

"Down then across" means that the names are arranged so that the sequence reads **down the first column, then continues at the top of the second column**, and so on:

```
a  d  g
b  e  h
c  f
```

The logic is:
1. Read all names into an array and find the length of the longest name.
2. Column width = longest name + 2 spaces.
3. Number of columns = terminal width / column width (at least 1, at most the number of files).
4. Number of rows = ceiling(number of files / number of columns).
5. Print **row by row**. For row `r` and column `c`, the item to print is `names[r + c * rows]`.

A single loop `for (i = 0; i < n; i++) printf(names[i])` prints the array in memory order, which is **across** the screen (a b c d e f ...). The terminal can only print left to right, one line at a time, so to get the *down* order the program has to print the first element of every column on the first line, then the second element of every column on the second line. Neighbouring items on one line are therefore `rows` positions apart in the array, not one position apart. That needs two nested loops (rows and columns) and the index formula above, and it also needs all names in memory first, because the program must know `n` and the longest name before it can print even the first line.

### Q2. What is the purpose of `ioctl` here? What would be the limitations of a fixed 80-column width?

`ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)` asks the terminal driver for the current window size and fills a `struct winsize`; `ws.ws_col` is the number of character columns. The program needs this to decide how many columns fit on a line.

If the program only used a fixed 80 columns:
- On a wide terminal (for example 200 columns) it would waste more than half of the screen and print many more lines than necessary.
- On a narrow terminal (for example 40 columns) the lines would be longer than the screen, so the terminal would wrap them in the middle of a name and the columns would no longer line up.
- When the user resizes the window, the output would not adapt.

So `ioctl()` makes the output match the real terminal, and 80 is used only as a fallback when the call fails (for example when the output is redirected to a file or a pipe, where there is no terminal).

---

## Feature 4 — ls-v1.3.0: Horizontal Column Display (-x)

### Q1. Compare the implementation complexity of "down then across" printing versus "across" printing. Which one requires more pre-calculation and why?

"Down then across" is more complex. Before the first character can be printed it needs the **number of columns**, from that the **number of rows**, and then every position is computed with the index formula `r + c * rows`. The array must be fully known (`n`) and the layout depends on the rows, so every item's position is calculated twice (rows and columns).

"Across" (`-x`) needs only the column width and the terminal width. The names are printed in the array order, and the only state is the current horizontal position: if `position + column width` would exceed the terminal width, print a newline and reset the position to 0. There is no row count and no index arithmetic. The items simply flow from left to right and wrap, like text in a paragraph.

### Q2. Describe the strategy used to manage the different display modes (-l, -x, default). How did the program decide which function to call?

I used an `enum display_mode { MODE_DEFAULT, MODE_LONG, MODE_HORIZONTAL }` and one variable `mode` in `main()`. The `getopt()` loop sets it: `-l` sets `MODE_LONG` and `-x` sets `MODE_HORIZONTAL`; if neither is given it stays `MODE_DEFAULT`. When both are given the last one wins, which is also how GNU `ls` behaves. The mode is passed to `do_ls()`. After `do_ls()` has read all the names into the array, a `switch (mode)` calls exactly one display function: `print_long()`, `print_horizontal()` or, by default, `print_columns()`. Reading the directory is common to all modes and only the display step differs, so adding a new mode needs a new enum value, one `case` label and one new function.

---

## Feature 5 — ls-v1.4.0: Alphabetical Sort

### Q1. Why is it necessary to read all directory entries into memory before sorting? What are the drawbacks for directories with millions of files?

`readdir()` returns the entries one at a time in the order in which the file system stores them (not alphabetical). A sort has to compare entries with each other, and the very first name to print can be the one that `readdir()` returns last. So no name can be printed until every name has been seen; therefore all of them must be kept in memory.

For a directory with millions of files this has drawbacks:
- **Memory:** every name needs a heap-allocated copy plus a pointer, so millions of entries can use hundreds of megabytes, and the whole array must exist at the same time.
- **Time before first output:** the user sees nothing until all entries are read and sorted. Sorting costs O(n log n) comparisons.
- With `-l`, the program also calls `lstat()` for each entry, which is slow for millions of files.

(GNU `ls` has the `-f` / `-U` option, which skips sorting and prints entries as they are read, exactly to avoid this cost.)

### Q2. Explain the purpose and signature of the comparison function required by `qsort()`. Why must it take `const void *` arguments?

`qsort(base, nmemb, size, compar)` is a generic function: it can sort an array of any element type, but it has no idea how to compare two elements. The caller supplies that knowledge through the function pointer `compar`, whose signature is

```c
int compar(const void *a, const void *b);
```

It must return a negative number if the first element comes before the second, zero if they are equal and a positive number if the first comes after. It receives **pointers to the array elements**. My array is an array of `char *`, so each argument is really a pointer to a `char *`, i.e. a `char **`. My function casts it back and calls `strcmp()`:

```c
static int compare_names(const void *a, const void *b)
{
    const char *s1 = *(const char * const *)a;
    const char *s2 = *(const char * const *)b;
    return strcmp(s1, s2);
}
```

The parameters are `const void *` because `qsort()` is written once for all types, and `void *` is the only pointer type in C that can hold the address of any type. The `const` is a promise that the comparison will not modify the elements. Because `void *` has no type, the function must cast it back to the real type before dereferencing it.

---

## Feature 6 — ls-v1.5.0: Colorized Output

### Q1. How do ANSI escape codes produce color? Show the sequence for green text.

An ANSI escape sequence is a string of ordinary characters that starts with the ESC character (ASCII 27, written `\033` in octal or `\x1b` in hex) followed by `[`, one or more attribute numbers, and a final `m`. When the terminal emulator receives the bytes it does not print them; it interprets them as a command to change its drawing state for all the following characters. For example `0` resets, `7` turns on reverse video, `30`–`37` choose the foreground color (31 red, 32 green, 34 blue, 35 magenta) and the color is kept until a reset is sent.

Green text:

```c
printf("\033[0;32m%s\033[0m", "hello");
```

`\033[0;32m` switches the foreground color to green (`0` = reset attributes, `32` = green), then the text is printed, then `\033[0m` resets everything to the default so that later output is not colored. In my program these sequences are `#define` macros, and colors are enabled only when the output is a terminal (`isatty()`), so that a redirected file or pipe does not receive the escape bytes.

### Q2. Which bits of `st_mode` must be checked to decide if a file is executable by owner, group or others?

The three execute bits: `S_IXUSR` (`0100`) for the owner, `S_IXGRP` (`0010`) for the group and `S_IXOTH` (`0001`) for others. A file is executable by at least one class when

```c
st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)
```

is non-zero. (To test one class, `st_mode & S_IXUSR` alone.) I first check the file type, because the execute bit on a **directory** means "may enter it", not "is a program", and directories are colored blue before this test is reached.

---

## Feature 7 — ls-v1.6.0: Recursive Listing (-R)

### Q1. What is a "base case" in a recursive function? What is the base case in the recursive `ls`?

The base case is the condition under which a recursive function returns without calling itself again. Without it the recursion would never stop and the stack would overflow. In my `do_ls()` there is no explicit `if` for it: the base case is a directory that has **no subdirectories**. The recursion happens inside a loop over the entries of the current directory, and the loop only calls `do_ls()` for entries for which `S_ISDIR()` is true. A directory that contains only ordinary files (or nothing, or one that cannot be opened) makes the loop run without any recursive call, and the function simply returns to its caller. Because every recursive call goes one level deeper into a finite directory tree, the recursion is guaranteed to end. Skipping `.` and `..` and using `lstat()` (so symbolic links are not followed) prevent infinite loops.

### Q2. Why is it essential to construct a full path before the recursive call? What would happen if I called `do_ls("subdir")` from inside `do_ls("parent_dir")`?

`readdir()` returns only the **name** of an entry (`subdir`), not its path. `opendir()`, `lstat()` and `readdir()` interpret a relative path starting from the **current working directory** of the process, which never changes while the program runs. Therefore `do_ls("subdir")` would look for `./subdir` in the directory the program was started from, not for `parent_dir/subdir`. The result would be "Cannot open directory" (if no such directory exists there) or, worse, listing a different directory that happens to have the same name. The same applies to `lstat()` used to check whether the entry is a directory. So the program builds `parent_dir/subdir` with `snprintf()` (without doubling the slash when `parent_dir` already ends with `/`) and passes this full path to both `lstat()` and the recursive `do_ls()`. It is also the path that is printed in the `path:` header, exactly as the real `ls -R` does.
