# hlerr

When doing shell scripting, one often needs to know which part of a program's output is going to stdout and which to stderr. Most shells (including bash) offer no way to distinguish these visually; so the usual way of finding out is to redirect stdout to /dev/null and see what remains.

Enter `hlerr`. It runs the given command and highlights all stderr output in red. As a bonus, when the program is finished, it prints its exit code, or the signal that terminated it.

## Caveats

Running a program that produces a large amount of output via `hlerr` may considerably increase the time taken.
