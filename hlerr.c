#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>

#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

static const char STDERR_BEGIN_MARKER[] = "\x1b[31m";
static const char STDERR_END_MARKER[] = "\x1b[m";

static int child(int argc, char *argv[],
        int *stdout_pipes, int *stderr_pipes);
static int parent(int* stdout_pipes, int* stderr_pipes);

static int stdout_putc(char c);
static int stderr_putc(char c);
static int set_stderr(bool is_stderr);
static int flush_stdout_line();

bool in_stderr = false;
char stdout_line[1024];
size_t stdout_line_pos = 0;

static int write_all_nointr(int fd, const void *buf, size_t nbyte);
static ssize_t read_nointr(int fd, void *buf, size_t nbyte);
static ssize_t write_nointr(int fd, const void *buf, size_t nbyte);

static void usage(const char* argv0);
static const char *get_signal_name(int signal);

int main(int argc, char *argv[])
{
    if (argc <= 1) usage(argv[0]);

    int rc;

    int stdout_pipe[2], stderr_pipe[2];

    rc = pipe(stdout_pipe);
    if (rc == -1) {
        perror("pipe(stdout_pipe)");
        goto err0;
    }

    rc = pipe(stderr_pipe);
    if (rc == -1) {
        perror("pipe(stderr_pipe)");
        goto err0;
    }

    switch (fork()) {
    case -1:
        perror("fork()");
        goto err0;
    case 0:
        return child(argc, argv, stdout_pipe, stderr_pipe);
    default:
        return parent(stdout_pipe, stderr_pipe);
    }

err0:
    /* don't bother closing any file descriptors */
    return EXIT_FAILURE;
}

int child(int argc, char *argv[],
        int *stdout_pipes, int *stderr_pipes)
{
    int rc;

    /* Close the reading ends of the pipes */

    rc = close(stdout_pipes[0]);
    if (rc == -1) {
        perror("close(stdout_pipes[0])");
        goto err0;
    }
    rc = close(stderr_pipes[0]);
    if (rc == -1) {
        perror("close(stderr_pipes[0])");
        goto err0;
    }

    /* Duplicate the writing end as stdout */

    rc = dup2(stdout_pipes[1], STDOUT_FILENO);
    if (rc == -1) {
        perror("dup2() for STDOUT");
        goto err0;
    }
    rc = close(stdout_pipes[1]);
    if (rc == -1) {
        perror("close(stdout_pipes[1])");
        goto err0;
    }

    /* Duplicate the writing end as stderr */

    rc = dup2(stderr_pipes[1], STDERR_FILENO);
    if (rc == -1) {
        perror("dup2() for STDERR");
        goto err0;
    }
    rc = close(stderr_pipes[1]);
    if (rc == -1) {
        perror("close(stderr_pipes[1])");
        goto err0;
    }

    /* Exec */

    execvp(argv[1], &argv[1]);
    /* The most common error case is when the executable is not found */
    perror("Failed to execute");
    return -1;

err0:
    /* don't bother closing any file descriptors */
    return EXIT_FAILURE;
}

int parent(int *stdout_pipes, int *stderr_pipes)
{
    int rc;

    /* Close the writing ends of the pipes */

    rc = close(stdout_pipes[1]);
    if (rc == -1) {
        perror("close(stdout_pipes[1])");
        goto reap_child;
    }
    rc = close(stderr_pipes[1]);
    if (rc == -1) {
        perror("close(stderr_pipes[1])");
        goto reap_child;
    }

    struct pollfd pfd[2];
    pfd[0].fd = stdout_pipes[0];
    pfd[0].events = POLLIN | POLLPRI;
    pfd[1].fd = stderr_pipes[0];
    pfd[1].events = POLLIN | POLLPRI;

    for (;;)
    {
        rc = poll(pfd, 2, -1);
        if (rc == -1) {
            perror("poll()");
            goto reap_child;
        }

        if ((pfd[0].revents & (POLLERR | POLLNVAL)) ||
            (pfd[1].revents & (POLLERR | POLLNVAL)))
        {
            fprintf(stderr, "stream error\n");
            goto reap_child;
        }

        bool got_stdout = false, got_stderr = false;

        if (pfd[0].revents & (POLLIN | POLLPRI)) {
            char c;
            rc = read_nointr(stdout_pipes[0], &c, 1);
            if (rc == -1) {
                perror("read() from stdout pipe");
                goto reap_child;
            }
            stdout_putc(c);
            got_stdout = true;
        }

        if (pfd[1].revents & (POLLIN | POLLPRI)) {
            char c;
            rc = read_nointr(stderr_pipes[0], &c, 1);
            if (rc == -1) {
                perror("read() from stderr pipe");
                goto reap_child;
            }
            stderr_putc(c);
            got_stderr = true;
        }

        if (!got_stdout && !got_stderr) break;
    }

reap_child:

    flush_stdout_line();

    int status;
    pid_t wpid = wait(&status);
    if (wpid == -1) {
        perror("wait()");
        goto err0;
    }

    if (WIFEXITED(status)) {
        printf("\x1b[34mExited with status %d\x1b[m\n", WEXITSTATUS(status));
        return WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status)) {
        const char *signal_name = get_signal_name(WTERMSIG(status));
        if (signal_name)
            printf("\x1b[34mKilled by signal %d (%s)\x1b[m\n",
                    WTERMSIG(status), signal_name);
        else
            printf("\x1b[34mKilled by signal %d\x1b[m\n", WTERMSIG(status));
        return EXIT_FAILURE;
    }
    else {
        fprintf(stderr, "\x1b[34mUnknown status %d returned from wait() "
                "for pid %d\x1b[m\n", status, (int) wpid);
        return EXIT_FAILURE;
    }

    close(stdout_pipes[0]);
    close(stderr_pipes[0]);

    return EXIT_SUCCESS;

err0:
    /* don't bother closing any file descriptors */
    return EXIT_FAILURE;
}

int stdout_putc(char c)
{
    if (stdout_line_pos >= sizeof(stdout_line)) {
        if (flush_stdout_line()) return 1;
    }

    stdout_line[stdout_line_pos++] = c;
    if (c == '\n') {
        if (flush_stdout_line()) return 1;
    }

    return 0;
}

int stderr_putc(char c)
{
    if (set_stderr(true)) return 1;
    if (write_all_nointr(STDERR_FILENO, &c, 1)) return 1;
    return 0;
}

int flush_stdout_line()
{
    if (set_stderr(false)) return 1;
    if (write_all_nointr(STDOUT_FILENO, stdout_line, stdout_line_pos))
        return 1;
    stdout_line_pos = 0;
    return 0;
}

int set_stderr(bool is_stderr)
{
    if (is_stderr == in_stderr) return 0;
    in_stderr = is_stderr;
    int rc;
    if (in_stderr)
        rc = write_all_nointr(STDERR_FILENO, STDERR_BEGIN_MARKER,
                sizeof(STDERR_BEGIN_MARKER));
    else
        rc = write_all_nointr(STDERR_FILENO, STDERR_END_MARKER,
                sizeof(STDERR_END_MARKER));
    return rc;
}

int write_all_nointr(int fd, const void *buf, size_t nbyte)
{
    size_t numwritten = 0;
    while (numwritten < nbyte) {
        int rc = write_nointr(fd, buf + numwritten, nbyte - numwritten);
        if (rc == -1) {
            perror("write()");
            return rc;
        }
        numwritten += rc;
    }
    return 0;
}

ssize_t read_nointr(int fd, void *buf, size_t nbyte)
{
    int numread;
    do {
        numread = read(fd, buf, nbyte);
    } while (numread == -1 && errno == EINTR);
    return numread;
}

ssize_t write_nointr(int fd, const void *buf, size_t nbyte)
{
    int numwritten;
    do {
        numwritten = write(fd, buf, nbyte);
    } while (numwritten == -1 && errno == EINTR);
    return numwritten;
}

void usage(const char *argv0)
{
    fprintf(stderr, "usage: %s <command>...\n", argv0);
    exit(2);
}

const char *get_signal_name(int signal)
{
    switch (signal) {
#define S(sig) case SIG ## sig: return "SIG" #sig
        S(ABRT);
        S(ALRM);
        S(BUS);
        S(CHLD);
        S(CONT);
        S(FPE);
        S(HUP);
        S(ILL);
        S(INT);
        S(KILL);
        S(PIPE);
        S(QUIT);
        S(SEGV);
        S(STOP);
        S(TERM);
        S(TSTP);
        S(TTIN);
        S(TTOU);
        S(USR1);
        S(USR2);
        S(POLL);
        S(PROF);
        S(SYS);
        S(TRAP);
        S(URG);
        S(VTALRM);
        S(XCPU);
        S(XFSZ);
#undef S
        default: return NULL;
    }
}
