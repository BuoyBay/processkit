/* Scalar ABI for shell-free process pipes. Each handle is confined to one thread. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#define API __declspec(dllexport)
#else
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <pthread.h>
#include <poll.h>
#include <sys/wait.h>
#include <sys/resource.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#define API __attribute__((visibility("default")))
extern char **environ;
#endif

typedef struct {
    char *executable, *cwd, **argv;
    size_t argc;
    char **env_names, **env_values;
    size_t env_count;
    int started, exited, eof[2], owned_tree, signal_number, tree_terminated;
    int64_t exit_code;
    char error[512];
#ifdef _WIN32
    HANDLE child, input, output[2], job;
#else
    pid_t child;
    int input, output[2], status_fd, owner_fd;
#endif
} BPKProcess;

static char *copy_text(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}
static int failure(BPKProcess *p, const char *operation) {
#ifdef _WIN32
    snprintf(p->error, sizeof(p->error), "%s (Windows error %lu)", operation, (unsigned long)GetLastError());
#else
    snprintf(p->error, sizeof(p->error), "%s: %s", operation, strerror(errno));
#endif
    return 0;
}
API const char *bpk_error(const BPKProcess *p) { return p ? p->error : "Cannot allocate process"; }
API void *bpk_new(const char *executable, const char *cwd) {
    if (!executable || !*executable || !cwd) return NULL;
    BPKProcess *p = calloc(1, sizeof(*p));
    if (!p) return NULL;
#ifndef _WIN32
    p->input = p->output[0] = p->output[1] = p->status_fd = p->owner_fd = -1;
#endif
    p->executable = copy_text(executable);
    p->cwd = copy_text(cwd);
    p->argv = calloc(2, sizeof(char *));
    if (p->argv) p->argv[0] = copy_text(executable);
    if (!p->executable || !p->cwd || !p->argv || !p->argv[0]) {
        if (p->argv) free(p->argv[0]);
        free(p->argv); free(p->executable); free(p->cwd); free(p); return NULL;
    }
    p->argc = 1;
    return p;
}
API int64_t bpk_arg(BPKProcess *p, const char *argument) {
    if (!p || p->started || !argument || p->argc >= 65536) return 0;
    char *text = copy_text(argument);
    char **args = text ? realloc(p->argv, (p->argc + 2) * sizeof(char *)) : NULL;
    if (!args) { free(text); snprintf(p->error, sizeof(p->error), "Cannot allocate arguments"); return 0; }
    p->argv = args;
    p->argv[p->argc++] = text;
    p->argv[p->argc] = NULL;
    return 1;
}

/* Opt-in ownership. Legacy callers retain direct-child semantics. */
API int64_t bpk_own_tree(BPKProcess *p) {
    if (!p || p->started) return 0;
    p->owned_tree = 1;
    return 1;
}


/* Names/values are UTF-8 C strings; managed callers reject embedded NUL. */
API int64_t bpk_env(BPKProcess *p, const char *name, const char *value) {
    if (!p || p->started || !name || !*name || strchr(name, '=') || !value) {
        if (p) snprintf(p->error, sizeof(p->error), "Invalid environment override");
        return 0;
    }
    if (p->env_count >= 4096) {
        snprintf(p->error, sizeof(p->error), "Too many environment overrides"); return 0;
    }
    char *copied_name = copy_text(name), *copied_value = copy_text(value);
    char **names = copied_name && copied_value ? realloc(p->env_names, (p->env_count + 1) * sizeof(char *)) : NULL;
    if (!names) {
        free(copied_name); free(copied_value);
        snprintf(p->error, sizeof(p->error), "Cannot allocate environment override"); return 0;
    }
    p->env_names = names;
    char **values = realloc(p->env_values, (p->env_count + 1) * sizeof(char *));
    if (!values) {
        free(copied_name); free(copied_value);
        snprintf(p->error, sizeof(p->error), "Cannot allocate environment override"); return 0;
    }
    p->env_values = values;
    p->env_names[p->env_count] = copied_name;
    p->env_values[p->env_count++] = copied_value;
    return 1;
}
static void clear_text(void *memory, size_t size) {
    volatile unsigned char *bytes = memory;
    while (size--) *bytes++ = 0;
}
static void free_environment_text(char *text) {
    if (text) { clear_text(text, strlen(text)); free(text); }
}

#ifdef _WIN32
static wchar_t *wide_text(const char *text) {
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (!size) return NULL;
    wchar_t *wide = malloc((size_t)size * sizeof(wchar_t));
    if (!wide) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, size)) { free(wide); return NULL; }
    return wide;
}

static size_t environment_name_length(const wchar_t *entry) {
    /* Windows keeps special inherited entries such as =C:=C:\\work. */
    const wchar_t *separator = wcschr(entry + (*entry == L'='), L'=');
    return separator ? (size_t)(separator - entry) : wcslen(entry);
}
static int environment_name_equal(const wchar_t *left, const wchar_t *right) {
    size_t a = environment_name_length(left), b = environment_name_length(right);
    return a <= INT_MAX && b <= INT_MAX && CompareStringOrdinal(left, (int)a, right, (int)b, TRUE) == CSTR_EQUAL;
}
static int environment_order(const void *left, const void *right) {
    const wchar_t *a = *(const wchar_t *const *)left, *b = *(const wchar_t *const *)right;
    int order = CompareStringOrdinal(a, (int)environment_name_length(a), b, (int)environment_name_length(b), TRUE);
    return order == CSTR_LESS_THAN ? -1 : order == CSTR_GREATER_THAN ? 1 : 0;
}
static wchar_t *environment_block(BPKProcess *p) {
    wchar_t *inherited = GetEnvironmentStringsW();
    if (!inherited) return NULL;
    size_t inherited_count = 0;
    for (const wchar_t *entry = inherited; *entry; entry += wcslen(entry) + 1) inherited_count++;
    wchar_t **entries = calloc(inherited_count + p->env_count + 1, sizeof(wchar_t *));
    if (!entries) { FreeEnvironmentStringsW(inherited); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    size_t count = 0;
    for (const wchar_t *entry = inherited; *entry; entry += wcslen(entry) + 1) {
        size_t length = wcslen(entry) + 1;
        entries[count] = malloc(length * sizeof(wchar_t));
        if (!entries[count]) goto fail;
        memcpy(entries[count++], entry, length * sizeof(wchar_t));
    }
    FreeEnvironmentStringsW(inherited); inherited = NULL;
    for (size_t i = 0; i < p->env_count; i++) {
        wchar_t *name = wide_text(p->env_names[i]), *value = wide_text(p->env_values[i]);
        if (!name || !value) { free(name); free(value); goto fail; }
        size_t name_size = wcslen(name), value_size = wcslen(value);
        if (name_size > 1048576 || value_size > 1048576) { free(name); free(value); goto fail; }
        wchar_t *entry = malloc((name_size + value_size + 2) * sizeof(wchar_t));
        if (!entry) { free(name); free(value); goto fail; }
        memcpy(entry, name, name_size * sizeof(wchar_t)); entry[name_size] = L'=';
        memcpy(entry + name_size + 1, value, (value_size + 1) * sizeof(wchar_t));
        clear_text(value, value_size * sizeof(wchar_t)); free(name); free(value);
        /* Remove every inherited/earlier spelling of this case-insensitive key. */
        for (size_t at = 0; at < count; ) {
            if (environment_name_equal(entries[at], entry)) {
                clear_text(entries[at], wcslen(entries[at]) * sizeof(wchar_t)); free(entries[at]);
                memmove(entries + at, entries + at + 1, (count - at - 1) * sizeof(wchar_t *)); count--;
            } else at++;
        }
        entries[count++] = entry;
    }
    qsort(entries, count, sizeof(wchar_t *), environment_order);
    size_t units = 1;
    for (size_t i = 0; i < count; i++) {
        size_t n = wcslen(entries[i]) + 1;
        if (n > 16777216 || units > 16777216 - n) goto fail;
        units += n;
    }
    if (units < 2) units = 2;
    wchar_t *block = calloc(units, sizeof(wchar_t));
    if (!block) goto fail;
    size_t at = 0;
    for (size_t i = 0; i < count; i++) {
        size_t n = wcslen(entries[i]) + 1;
        memcpy(block + at, entries[i], n * sizeof(wchar_t)); at += n;
        clear_text(entries[i], n * sizeof(wchar_t)); free(entries[i]);
    }
    free(entries); return block;
fail:
    if (inherited) FreeEnvironmentStringsW(inherited);
    for (size_t i = 0; i < count; i++) { clear_text(entries[i], wcslen(entries[i]) * sizeof(wchar_t)); free(entries[i]); }
    free(entries); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL;
}
static void free_environment_block(wchar_t *block) {
    if (!block) return;
    size_t units = 0;
    for (wchar_t *entry = block; *entry; entry += wcslen(entry) + 1) units += wcslen(entry) + 1;
    clear_text(block, (units + 1) * sizeof(wchar_t)); free(block);
}

static wchar_t *command_line(BPKProcess *p) {
    /* Always quote each argument using the CommandLineToArgv/CRT backslash rules. */
    size_t capacity = 1;
    wchar_t **args = calloc(p->argc, sizeof(wchar_t *));
    if (!args) return NULL;
    for (size_t i = 0; i < p->argc; i++) {
        args[i] = wide_text(p->argv[i]);
        if (!args[i]) goto fail;
        size_t n = wcslen(args[i]);
        if (n > 16382 || capacity + n * 2 + 3 > 32767) goto fail;
        capacity += n * 2 + 3;
    }
    wchar_t *line = calloc(capacity, sizeof(wchar_t));
    if (!line) goto fail;
    size_t at = 0;
    for (size_t i = 0; i < p->argc; i++) {
        if (i) line[at++] = L' ';
        line[at++] = L'"';
        size_t slashes = 0;
        for (const wchar_t *c = args[i]; ; c++) {
            if (*c == L'\\') { slashes++; continue; }
            size_t emit = (*c == L'"' || !*c) ? slashes * 2 : slashes;
            for (size_t j = 0; j < emit; j++) line[at++] = L'\\';
            slashes = 0;
            if (*c == L'"') line[at++] = L'\\';
            if (!*c) break;
            line[at++] = *c;
        }
        line[at++] = L'"';
    }
    for (size_t i = 0; i < p->argc; i++) free(args[i]);
    free(args);
    return line;
fail:
    for (size_t i = 0; i < p->argc; i++) free(args[i]);
    free(args); SetLastError(ERROR_INVALID_PARAMETER); return NULL;
}
static void close_handle(HANDLE *h) { if (*h && *h != INVALID_HANDLE_VALUE) CloseHandle(*h); *h = NULL; }
API int64_t bpk_start(BPKProcess *p) {
    if (!p || p->started) return 0;
    HANDLE child_in = NULL, child_out = NULL, child_err = NULL;
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    static LONG counter = 0;
    wchar_t pipe_name[128];
    swprintf(pipe_name, 128, L"\\\\.\\pipe\\buoy-processkit-%lu-%ld", (unsigned long)GetCurrentProcessId(), InterlockedIncrement(&counter));
    p->input = CreateNamedPipeW(pipe_name, PIPE_ACCESS_OUTBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT, 1, 65536, 65536, 0, NULL);
    if (p->input == INVALID_HANDLE_VALUE) { p->input = NULL; goto fail; }
    child_in = CreateFileW(pipe_name, GENERIC_READ, 0, &security, OPEN_EXISTING, 0, NULL);
    if (child_in == INVALID_HANDLE_VALUE) { child_in = NULL; goto fail; }
    if (!ConnectNamedPipe(p->input, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) goto fail;
    if (!CreatePipe(&p->output[0], &child_out, &security, 0) ||
        !SetHandleInformation(p->output[0], HANDLE_FLAG_INHERIT, 0)) goto fail;
    if (!CreatePipe(&p->output[1], &child_err, &security, 0) ||
        !SetHandleInformation(p->output[1], HANDLE_FLAG_INHERIT, 0)) goto fail;
    wchar_t *exe = wide_text(p->executable), *cwd = *p->cwd ? wide_text(p->cwd) : NULL;
    wchar_t *line = command_line(p);
    wchar_t *environment = environment_block(p);
    if (!exe || (*p->cwd && !cwd) || !line || !environment) { free(exe); free(cwd); free(line); free_environment_block(environment); goto fail; }
    SIZE_T attribute_size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_size);
    LPPROC_THREAD_ATTRIBUTE_LIST attributes = malloc(attribute_size);
    if (!attributes) { free(exe); free(cwd); free(line); free_environment_block(environment); SetLastError(ERROR_NOT_ENOUGH_MEMORY); goto fail; }
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_size)) {
        free(attributes); free(exe); free(cwd); free(line); free_environment_block(environment); goto fail;
    }
    HANDLE handles[] = {child_in, child_out, child_err};
    BOOL prepared = UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        handles, sizeof(handles), NULL, NULL);
    STARTUPINFOEXW startup;
    memset(&startup, 0, sizeof(startup));
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = child_in;
    startup.StartupInfo.hStdOutput = child_out;
    startup.StartupInfo.hStdError = child_err;
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION info;
    memset(&info, 0, sizeof(info));
    if (p->owned_tree && prepared) {
        p->job = CreateJobObjectW(NULL, NULL);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
        memset(&limits, 0, sizeof(limits));
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        prepared = p->job && SetInformationJobObject(p->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
    BOOL created = prepared && CreateProcessW(exe, line, NULL, NULL, TRUE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT |
        (p->owned_tree ? CREATE_SUSPENDED : 0), environment, cwd, &startup.StartupInfo, &info);
    DWORD error = GetLastError();
    DeleteProcThreadAttributeList(attributes); free(attributes); free(exe); free(cwd); free(line); free_environment_block(environment);
    if (!created) { SetLastError(error); goto fail; }
    if (p->owned_tree && (!AssignProcessToJobObject(p->job, info.hProcess) || ResumeThread(info.hThread) == (DWORD)-1)) {
        error = GetLastError();
        TerminateProcess(info.hProcess, 137);
        WaitForSingleObject(info.hProcess, INFINITE);
        CloseHandle(info.hThread); CloseHandle(info.hProcess);
        SetLastError(error); goto fail;
    }
    close_handle(&child_in); close_handle(&child_out); close_handle(&child_err);
    CloseHandle(info.hThread); p->child = info.hProcess; p->started = 1;
    return 1;
fail:
    failure(p, "Cannot start process");
    close_handle(&child_in); close_handle(&child_out); close_handle(&child_err);
    close_handle(&p->input); close_handle(&p->output[0]); close_handle(&p->output[1]);
    close_handle(&p->job);
    return 0;
}
#else

static void free_environment(char **entries) {
    if (!entries) return;
    for (size_t i = 0; entries[i]; i++) free_environment_text(entries[i]);
    free(entries);
}
static char **process_environment(BPKProcess *p) {
    size_t inherited_count = 0;
    while (environ[inherited_count]) inherited_count++;
    char **entries = calloc(inherited_count + p->env_count + 1, sizeof(char *));
    if (!entries) return NULL;
    size_t count = 0;
    for (size_t i = 0; i < inherited_count; i++) {
        entries[count] = copy_text(environ[i]);
        if (!entries[count]) { free_environment(entries); return NULL; }
        count++;
    }
    for (size_t i = 0; i < p->env_count; i++) {
        size_t name_size = strlen(p->env_names[i]), value_size = strlen(p->env_values[i]);
        char *entry = malloc(name_size + value_size + 2);
        if (!entry) { free_environment(entries); return NULL; }
        memcpy(entry, p->env_names[i], name_size); entry[name_size] = '=';
        memcpy(entry + name_size + 1, p->env_values[i], value_size + 1);
        for (size_t at = 0; at < count; ) {
            if (strncmp(entries[at], entry, name_size + 1) == 0) {
                free_environment_text(entries[at]);
                memmove(entries + at, entries + at + 1, (count - at) * sizeof(char *)); count--;
            } else at++;
        }
        entries[count++] = entry; entries[count] = NULL;
    }
    return entries;
}

static void close_fd(int *fd) { if (*fd >= 0) close(*fd); *fd = -1; }
static int make_pipe(int fds[2]) {
    if (pipe(fds)) return -1;
    for (int i = 0; i < 2; i++) if (fcntl(fds[i], F_SETFD, FD_CLOEXEC) < 0) {
        int e = errno; close(fds[0]); close(fds[1]); fds[0] = fds[1] = -1; errno = e; return -1;
    }
    /* Standard descriptors might be closed in an embedding application. Move
       every pipe above fd3 to keep child dup/exec-handshake wiring unambiguous. */
    for (int i = 0; i < 2; i++) if (fds[i] < 4) {
        int moved = fcntl(fds[i], F_DUPFD_CLOEXEC, 4);
        if (moved < 0) { int e = errno; close(fds[0]); close(fds[1]); fds[0] = fds[1] = -1; errno = e; return -1; }
        close(fds[i]); fds[i] = moved;
    }
    return 0;
}
/* Compute the fallback bound in the parent, before fork. An already-open fd
   may exceed BOTH limits after setrlimit lowers them. Enumerating the actual
   descriptor table catches that case; no allocator/directory API runs in child. */
static int descriptor_bound(long *bound) {
    struct rlimit limits;
    if (getrlimit(RLIMIT_NOFILE, &limits)) return -1;
    rlim_t limit = limits.rlim_max != RLIM_INFINITY ? limits.rlim_max : limits.rlim_cur;
    if (limit == RLIM_INFINITY) { errno = EINVAL; return -1; }
    *bound = limit < (rlim_t)INT_MAX ? (long)limit : INT_MAX;
    DIR *directory = opendir("/proc/self/fd");
    if (!directory) directory = opendir("/dev/fd");
    if (!directory) return -1;
    struct dirent *entry;
    errno = 0;
    while ((entry = readdir(directory)) != NULL) {
        const unsigned char *digit = (const unsigned char *)entry->d_name;
        long number = 0;
        if (!*digit) continue;
        for (; *digit >= '0' && *digit <= '9'; digit++) {
            if (number > (INT_MAX - (*digit - '0')) / 10) break;
            number = number * 10 + (*digit - '0');
        }
        if (!*digit && number < INT_MAX && number + 1 > *bound) *bound = number + 1;
    }
    int saved = errno;
    if (closedir(directory) && !saved) saved = errno;
    if (saved) { errno = saved; return -1; }
    return 0;
}

/* Post-fork failure path: preserve the original errno and use only
   async-signal-safe write/_exit. Retry interruptions and advance short writes;
   a closed/broken reporting pipe must never keep the child alive. */
static void child_start_failure(int fd, int error_number) {
    const unsigned char *bytes = (const unsigned char *)&error_number;
    size_t remaining = sizeof(error_number);
    while (remaining) {
        ssize_t written = write(fd, bytes, remaining);
        if (written > 0) {
            bytes += written;
            remaining -= (size_t)written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    _exit(127);
}

static int nonblock(int fd) { int flags = fcntl(fd, F_GETFL); return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK); }

/* The session leader stays alive until its owner closes the handle. This keeps
   the group identity reserved even after the command exits. A private pipe
   also makes owner death observable independently of a stopped/crashed owner.
   Only async-signal-safe operations run after fork. */
static void supervise_owned(pid_t command, int owner, int status_pipe, long maxfd) {
    int closed = 0;
#if defined(__linux__) && defined(SYS_close_range) && !defined(BPK_FORCE_FD_FALLBACK)
    int low = owner < status_pipe ? owner : status_pipe;
    int high = owner > status_pipe ? owner : status_pipe;
    if (syscall(SYS_close_range, 0U, (unsigned int)low - 1U, 0) == 0 &&
        (high == low + 1 || syscall(SYS_close_range, (unsigned int)low + 1U, (unsigned int)high - 1U, 0) == 0) &&
        syscall(SYS_close_range, (unsigned int)high + 1U, ~0U, 0) == 0) closed = 1;
#endif
    if (!closed) for (int fd = 0; fd < maxfd; fd++) if (fd != owner && fd != status_pipe) close(fd);
    int done = 0;
    for (;;) {
        if (!done) {
            int status;
            pid_t waited = waitpid(command, &status, WNOHANG);
            if (waited == command || (waited < 0 && errno != EINTR)) {
                if (waited < 0) status = 127 << 8;
                ssize_t count;
                do { count = write(status_pipe, &status, sizeof(status)); } while (count < 0 && errno == EINTR);
                if (count != (ssize_t)sizeof(status)) { kill(0, SIGKILL); _exit(127); }
                close(status_pipe);
                done = 1;
            }
        }
        struct pollfd watched = {owner, POLLIN | POLLHUP | POLLERR, 0};
        int ready = poll(&watched, 1, done ? -1 : 10);
        if (ready > 0) {
            char request;
            ssize_t count = read(owner, &request, 1);
            if (count == 1 && request == 'T') {
                if (!done) kill(command, SIGKILL);
                continue;
            }
            if (count < 0 && errno == EINTR) continue;
            kill(0, SIGKILL);
            _exit(127);
        }
        if (ready < 0 && errno != EINTR) {
            kill(0, SIGKILL);
            _exit(127);
        }
    }
}

static int stop_owned_command(BPKProcess *p) {
    sigset_t blocked, previous, pending;
    sigemptyset(&blocked); sigaddset(&blocked, SIGPIPE);
    int error = pthread_sigmask(SIG_BLOCK, &blocked, &previous);
    if (error) { errno = error; return 0; }
    sigpending(&pending);
    int was_pending = sigismember(&pending, SIGPIPE);
    char request = 'T';
    ssize_t written;
    do { written = write(p->owner_fd, &request, 1); } while (written < 0 && errno == EINTR);
    int saved = errno;
    if (written < 0 && saved == EPIPE && !was_pending) {
        sigpending(&pending);
        if (sigismember(&pending, SIGPIPE)) { int number; (void)sigwait(&blocked, &number); }
    }
    pthread_sigmask(SIG_SETMASK, &previous, NULL);
    errno = saved;
    return written == 1 || (written < 0 && (saved == EAGAIN || saved == EWOULDBLOCK));
}

API int64_t bpk_start(BPKProcess *p) {
    if (!p || p->started) return 0;
    int input[2] = {-1,-1}, output[2] = {-1,-1}, error[2] = {-1,-1}, handshake[2] = {-1,-1};
    int status_pipe[2] = {-1,-1}, owner[2] = {-1,-1};
    struct sigaction child_action;
    memset(&child_action, 0, sizeof(child_action));
    if (p->owned_tree) {
        if (sigaction(SIGCHLD, NULL, &child_action)) return failure(p, "Cannot inspect SIGCHLD disposition");
        if (child_action.sa_handler == SIG_IGN || (child_action.sa_flags & SA_NOCLDWAIT)) {
            snprintf(p->error, sizeof(p->error), "Owned process supervision requires waitable SIGCHLD (no SIG_IGN or SA_NOCLDWAIT)");
            return 0;
        }
    }
    char **environment = process_environment(p);
    if (!environment) { errno = ENOMEM; goto fail; }
    if (make_pipe(input) || make_pipe(output) || make_pipe(error) || make_pipe(handshake)) goto fail;
    if (p->owned_tree && (make_pipe(status_pipe) || make_pipe(owner) || nonblock(status_pipe[0]) || nonblock(owner[1]))) goto fail;
    if (nonblock(input[1]) || nonblock(output[0]) || nonblock(error[0])) goto fail;
    long maxfd;
    if (descriptor_bound(&maxfd)) goto fail;
    pid_t child = fork();
    if (child < 0) goto fail;
    if (!child) {
        if (p->owned_tree) {
            if (setsid() < 0) { child_start_failure(handshake[1], errno); }
            sigset_t blocked, previous;
            sigemptyset(&blocked); sigaddset(&blocked, SIGCHLD); sigaddset(&blocked, SIGPIPE);
            if (sigprocmask(SIG_BLOCK, &blocked, &previous) < 0) { child_start_failure(handshake[1], errno); }
            struct sigaction waitable;
            memset(&waitable, 0, sizeof(waitable)); waitable.sa_handler = SIG_DFL; sigemptyset(&waitable.sa_mask);
            if (sigaction(SIGCHLD, &waitable, NULL)) { child_start_failure(handshake[1], errno); }
            pid_t command = fork();
            if (command < 0) { child_start_failure(handshake[1], errno); }
            if (command > 0) supervise_owned(command, owner[0], status_pipe[1], maxfd);
            if (sigaction(SIGCHLD, &child_action, NULL)) { child_start_failure(handshake[1], errno); }
            if (sigprocmask(SIG_SETMASK, &previous, NULL) < 0) { child_start_failure(handshake[1], errno); }
        }
        if (dup2(input[0], 0) < 0 || dup2(output[1], 1) < 0 || dup2(error[1], 2) < 0 || dup2(handshake[1], 3) < 0) {
            child_start_failure(handshake[1], errno);
        }
        if (fcntl(3, F_SETFD, FD_CLOEXEC) < 0) { child_start_failure(3, errno); }
#ifdef __linux__
#if defined(SYS_close_range) && !defined(BPK_FORCE_FD_FALLBACK)
        if (syscall(SYS_close_range, 4U, ~0U, 0) != 0)
#endif
#endif
            for (int fd = 4; fd < maxfd; fd++) close(fd);
        if (*p->cwd && chdir(p->cwd)) { child_start_failure(3, errno); }
        /* execve, unlike execvp, never falls back to /bin/sh for ENOEXEC. */
        execve(p->executable, p->argv, environment);
        child_start_failure(3, errno);
    }
    close_fd(&input[0]); close_fd(&output[1]); close_fd(&error[1]); close_fd(&handshake[1]);
    close_fd(&status_pipe[1]); close_fd(&owner[0]);
    int child_error = 0;
    ssize_t count;
    do { count = read(handshake[0], &child_error, sizeof(child_error)); } while (count < 0 && errno == EINTR);
    close_fd(&handshake[0]);
    if (count != 0) {
        if (count < 0) child_error = errno;
        if (p->owned_tree) {
            close_fd(&owner[1]);
            siginfo_t observed;
            memset(&observed, 0, sizeof(observed));
            int checked;
            do { checked = waitid(P_PID, (id_t)child, &observed, WEXITED | WNOHANG | WNOWAIT); } while (checked < 0 && errno == EINTR);
            if (!checked) kill(-child, SIGKILL);
        } else kill(child, SIGKILL);
        while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {}
        errno = child_error ? child_error : EIO;
        goto fail;
    }
    free_environment(environment);
    p->child = child; p->started = 1;
    p->input = input[1]; p->output[0] = output[0]; p->output[1] = error[0];
    p->status_fd = status_pipe[0]; p->owner_fd = owner[1];
    return 1;
fail:
    failure(p, "Cannot start process");
    free_environment(environment);
    close_fd(&input[0]); close_fd(&input[1]); close_fd(&output[0]); close_fd(&output[1]);
    close_fd(&error[0]); close_fd(&error[1]); close_fd(&handshake[0]); close_fd(&handshake[1]);
    close_fd(&status_pipe[0]); close_fd(&status_pipe[1]); close_fd(&owner[0]); close_fd(&owner[1]);
    return 0;
}
#endif

API int64_t bpk_poll(BPKProcess *p) {
    if (!p || !p->started) return -1;
    if (p->exited) return 0;
#ifdef _WIN32
    DWORD code;
    DWORD wait = WaitForSingleObject(p->child, 0);
    if (wait == WAIT_TIMEOUT) return 1;
    if (wait != WAIT_OBJECT_0 || !GetExitCodeProcess(p->child, &code)) { failure(p, "Cannot poll process"); return -1; }
    p->exit_code = code;
#else
    int status;
    pid_t waited;
    if (p->owned_tree) {
        ssize_t count;
        do { count = read(p->status_fd, &status, sizeof(status)); } while (count < 0 && errno == EINTR);
        if (count == (ssize_t)sizeof(status)) waited = p->child;
        else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) { failure(p, "Cannot read process status"); return -1; }
        else {
            siginfo_t observed;
            memset(&observed, 0, sizeof(observed));
            int checked;
            do { checked = waitid(P_PID, (id_t)p->child, &observed, WEXITED | WNOHANG | WNOWAIT); } while (checked < 0 && errno == EINTR);
            if (checked < 0) { failure(p, "Cannot observe owned supervisor"); return -1; }
            if (!observed.si_pid) return 1;
            p->signal_number = observed.si_code == CLD_EXITED ? 0 : observed.si_status;
            p->exit_code = observed.si_code == CLD_EXITED ? observed.si_status : 128 + observed.si_status;
            p->exited = 1;
            return 0;
        }
    } else do { waited = waitpid(p->child, &status, WNOHANG); } while (waited < 0 && errno == EINTR);
    if (!waited) return 1;
    if (waited < 0) { failure(p, "Cannot reap process"); return -1; }
    p->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    p->signal_number = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
#endif
    p->exited = 1;
    return 0;
}
API int64_t bpk_exit(const BPKProcess *p) { return p && p->exited ? p->exit_code : -1; }
API int64_t bpk_signal(const BPKProcess *p) { return p ? p->signal_number : 0; }
API int64_t bpk_eof(const BPKProcess *p, int64_t stream) { return p && stream >= 0 && stream < 2 ? p->eof[stream] : 1; }
API int64_t bpk_read(BPKProcess *p, int64_t stream, void *buffer, int64_t capacity) {
    if (!p || !p->started || stream < 0 || stream > 1 || !buffer || capacity < 1 || capacity > 1048576) return -1;
    if (p->eof[stream]) return 0;
#ifdef _WIN32
    DWORD available = 0, read_count = 0;
    if (!PeekNamedPipe(p->output[stream], NULL, 0, NULL, &available, NULL)) {
        if (GetLastError() == ERROR_BROKEN_PIPE) { p->eof[stream] = 1; return 0; }
        failure(p, "Cannot read process output"); return -1;
    }
    if (!available) return 0;
    if (available > (DWORD)capacity) available = (DWORD)capacity;
    if (!ReadFile(p->output[stream], buffer, available, &read_count, NULL)) { failure(p, "Cannot read process output"); return -1; }
    return read_count;
#else
    ssize_t count;
    do { count = read(p->output[stream], buffer, (size_t)capacity); } while (count < 0 && errno == EINTR);
    if (!count) p->eof[stream] = 1;
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
    if (count < 0) failure(p, "Cannot read process output");
    return count;
#endif
}
API int64_t bpk_write(BPKProcess *p, const void *buffer, int64_t length, int64_t offset) {
    if (!p || !p->started || !buffer || length < 0 || offset < 0 || offset > length) return -1;
    if (offset == length) return 0;
    size_t count = (size_t)(length - offset);
    if (count > 1048576) count = 1048576;
#ifdef _WIN32
    if (!p->input) { snprintf(p->error, sizeof(p->error), "Process stdin is closed"); return -1; }
    DWORD written = 0;
    if (!WriteFile(p->input, (const char *)buffer + offset, (DWORD)count, &written, NULL)) {
        if (GetLastError() == ERROR_NO_DATA) return 0;
        failure(p, "Cannot write process stdin"); return -1;
    }
    return written;
#else
    if (p->input < 0) { snprintf(p->error, sizeof(p->error), "Process stdin is closed"); return -1; }
    sigset_t blocked, previous, pending;
    sigemptyset(&blocked); sigaddset(&blocked, SIGPIPE);
    int mask_error = pthread_sigmask(SIG_BLOCK, &blocked, &previous);
    if (mask_error) { errno = mask_error; failure(p, "Cannot mask SIGPIPE"); return -1; }
    sigpending(&pending);
    int was_pending = sigismember(&pending, SIGPIPE);
    ssize_t written;
    do { written = write(p->input, (const char *)buffer + offset, count); } while (written < 0 && errno == EINTR);
    int saved = errno;
    if (written < 0 && saved == EPIPE && !was_pending) {
        sigpending(&pending);
        if (sigismember(&pending, SIGPIPE)) { int signal_number; (void)sigwait(&blocked, &signal_number); }
    }
    pthread_sigmask(SIG_SETMASK, &previous, NULL);
    errno = saved;
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
    if (written < 0) failure(p, "Cannot write process stdin");
    return written;
#endif
}
API void bpk_close_input(BPKProcess *p) {
    if (!p) return;
#ifdef _WIN32
    close_handle(&p->input);
#else
    close_fd(&p->input);
#endif
}
API int64_t bpk_terminate(BPKProcess *p) {
    if (!p || !p->started) return 0;
    int64_t state = bpk_poll(p);
    if (state <= 0) return state == 0;
#ifdef _WIN32
    if (!TerminateProcess(p->child, 137)) {
        if (bpk_poll(p) == 0) return 1;
        return failure(p, "Cannot terminate process");
    }
#else
    if (p->owned_tree) {
        if (!stop_owned_command(p)) return failure(p, "Cannot terminate owned command");
    } else if (kill(p->child, SIGKILL) && errno != ESRCH) return failure(p, "Cannot terminate process");
#endif
    return 1;
}
API int64_t bpk_terminate_tree(BPKProcess *p) {
    if (!p || !p->started || !p->owned_tree) {
        if (p) snprintf(p->error, sizeof(p->error), "Process tree ownership is required");
        return 0;
    }
    if (p->tree_terminated < 0) {
        snprintf(p->error, sizeof(p->error), "Owned supervisor is no longer waitable");
        return 0;
    }
    if (p->tree_terminated) return 1;
#ifdef _WIN32
    if (!TerminateJobObject(p->job, 137)) return failure(p, "Cannot terminate process job");
#else
    siginfo_t observed;
    memset(&observed, 0, sizeof(observed));
    int checked;
    do { checked = waitid(P_PID, (id_t)p->child, &observed, WEXITED | WNOHANG | WNOWAIT); } while (checked < 0 && errno == EINTR);
    if (checked < 0) {
        p->tree_terminated = -1; /* Preserve failure and never signal a reused ID after identity loss. */
        return failure(p, "Owned supervisor is no longer waitable");
    }
    p->tree_terminated = 1;
    if (kill(-p->child, SIGKILL) && errno != ESRCH) { p->tree_terminated = 0; return failure(p, "Cannot terminate process group"); }
#endif
    return 1;
}
API void bpk_delete(BPKProcess *p) {
    if (!p) return;
    if (p->started && p->owned_tree) bpk_terminate_tree(p);
#ifndef _WIN32
    if (p->owned_tree) close_fd(&p->owner_fd);
#endif
    if (p->started && !p->exited) {
#ifdef _WIN32
        bpk_terminate(p);
        WaitForSingleObject(p->child, INFINITE);
#else
        if (!p->owned_tree) bpk_terminate(p);
        while (waitpid(p->child, NULL, 0) < 0 && errno == EINTR) {}
#endif
    }
    bpk_close_input(p);
#ifdef _WIN32
    close_handle(&p->output[0]); close_handle(&p->output[1]); close_handle(&p->child);
    close_handle(&p->job);
#else
    close_fd(&p->output[0]); close_fd(&p->output[1]);
    close_fd(&p->owner_fd); close_fd(&p->status_fd);
    if (p->started && p->owned_tree) while (waitpid(p->child, NULL, 0) < 0 && errno == EINTR) {}
#endif
    for (size_t i = 0; i < p->argc; i++) free(p->argv[i]);
    for (size_t i = 0; i < p->env_count; i++) { free(p->env_names[i]); free_environment_text(p->env_values[i]); }
    free(p->env_names); free(p->env_values);
    free(p->argv); free(p->cwd); free(p->executable); free(p);
}

API void *bpk_alloc(int64_t size) { return size > 0 && size <= 1048576 ? malloc((size_t)size) : NULL; }
API void bpk_free(void *memory) { free(memory); }

/* Flush a completed text output before its same-volume atomic replacement. */
API int64_t bpk_sync_file(const char *filename) {
#ifdef _WIN32
    wchar_t *wide = wide_text(filename);
    if (!wide) return 0;
    /* _commit is FlushFileBuffers, which needs a handle with write access. */
    FILE *file = _wfopen(wide, L"r+b");
    free(wide);
    if (!file) return 0;
    int synced = _commit(_fileno(file));
#else
    FILE *file = fopen(filename, "rb");
    if (!file) return 0;
    int synced = fsync(fileno(file));
#endif
    int closed = fclose(file);
    return synced == 0 && closed == 0;
}
