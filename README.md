# mysh — A Unix Shell in C

A working command-line shell written from scratch in C — a stripped-down `bash`.
It runs the classic read-parse-execute loop on top of raw Unix system calls:
forking processes, wiring them together with pipes, redirecting file descriptors,
managing background jobs, and handling terminal signals for full job control.

I built this to go deep on the operating-systems fundamentals that systems roles
care about: process creation and management, file descriptors, pipes and
redirection, signals, process groups and terminal control, and memory-safe C.

## Demo

A session showing pipes, redirection, background jobs, and Ctrl+C / Ctrl+Z job
control:

<!-- Recorded with asciinema; see demo.cast. To regenerate, follow DEMO.md. -->
[![asciicast](demo.gif)](demo.cast)

```
mysh> ls src | grep .c | wc -l
       9
mysh> echo "build log" > out.txt && cat out.txt
build log
mysh> sleep 30 &
[1] 48213
mysh> jobs
[1]  Running  sleep 30
mysh> sleep 100
^Z
[1]  Stopped  sleep 100
mysh> fg %1
```

---

## Features

- **External commands** via `fork` + `execvp` + `waitpid`, with `$PATH` lookup.
- **Pipes** of arbitrary length: `ls -la | grep .c | wc -l`.
- **I/O redirection**: `>` (overwrite), `>>` (append), `<` (input).
- **Built-in commands**: `cd`, `exit`, `export`, `history`, `jobs`, `fg`, `bg`,
  `kill`, `help` — run in the shell process so their effects persist.
- **Background jobs** (`&`) with a jobs table and `SIGCHLD` reaping (no zombies).
- **Job control**: Ctrl+C and Ctrl+Z act on the foreground job, not the shell,
  via process groups and terminal ownership (`setpgid` / `tcsetpgrp`); `fg`/`bg`
  resume stopped jobs.
- **Variable expansion**: `$VAR`, `${VAR}`, and `$?` (last exit status).
- **Wildcard globbing**: `*`, `?`, `[..]` expand to matching filenames.
- **Here-documents**: `cat << EOF ... EOF`.
- **Command history** in a ring buffer, listed with `history`.
- **Memory-safe**: warning-free `-Wall -Wextra` build; **zero leaks** (verified
  with `leaks`); clean under UndefinedBehaviorSanitizer.

---

## Build

Requires only a C11 compiler and POSIX — no external libraries.

```bash
make            # optimized build -> ./mysh
make debug      # build ./mysh-debug with -g + UndefinedBehaviorSanitizer
make leaks      # run the debug binary under macOS's `leaks` tool
make clean
```

Then run it:

```bash
./mysh
```

---

## Usage

```bash
mysh> pwd                                  # external command
mysh> ls -la | grep .c | wc -l            # a 3-stage pipeline
mysh> sort < input.txt > sorted.txt        # input and output redirection
mysh> echo "log entry" >> app.log          # append
mysh> export EDITOR=vim                     # set an env var for child processes
mysh> echo $EDITOR and exit code $?         # variable expansion
mysh> ls src/*.c                            # wildcard globbing
mysh> cat << EOF                            # here-document
> line one
> line two
> EOF
mysh> sleep 30 &                            # background job
mysh> jobs                                  # list jobs
mysh> fg %1                                 # bring job 1 to the foreground
mysh> history                               # recent commands
mysh> help                                  # list built-ins
mysh> exit
```

Job control (interactive terminal only):

```
mysh> sleep 100
^C                  # kills the command, not the shell
mysh> sleep 100
^Z                  # stops it -> "[1] Stopped sleep 100"
mysh> bg %1         # resume it in the background
mysh> kill %1       # send SIGTERM to the job
```

---

## Architecture

The REPL reads a line, the parser turns it into a pipeline of commands, an
expansion pass rewrites `$VAR`/globs, and the executor forks and wires the
processes together.

```
   stdin
     |  getline
     v
  +----------+   line    +-----------+  pipeline_t   +-----------+
  |  main.c  | --------> | parser.c  | ------------> | expand.c  |  $VAR, globs
  |  (REPL)  |           +-----------+               +-----+-----+
  +----+-----+                                             |
       | built-in? --> builtins.c (cd, exit, jobs, fg...)  |
       |                                                   v
       |                                            +-------------+
       +------------------------------------------> | executor.c  |
                                                    +------+------+
                                  fork / execvp / pipe / dup2 / waitpid
                                                    |
                              +---------------------+---------------------+
                              v                     v                     v
                         signals.c             jobs.c               heredoc.c
                      SIGCHLD reaper,       background &           "<< EOF" body
                      Ctrl+C/Ctrl+Z,        stopped jobs          collection
                      process groups
```

| File | Responsibility |
|------|----------------|
| `main.c` | The REPL loop; wires parse → here-docs → expand → execute → cleanup |
| `parser.c` | Tokenize into a `pipeline_t`; pipes, redirection, `&`, `<<` |
| `expand.c` | `$VAR`/`${VAR}`/`$?` expansion and `*`/`?`/`[..]` globbing |
| `heredoc.c` | Collect `<< WORD` bodies from stdin into a temp file |
| `executor.c` | fork/exec/wait, pipes, redirection, process groups, terminal control |
| `builtins.c` | In-process commands (`cd`, `exit`, `export`, `history`, `jobs`, `fg`, `bg`, `kill`, `help`) |
| `jobs.c` | The background/stopped job table |
| `signals.c` | `SIGCHLD` reaping, interactive-signal handling, job-control setup |
| `history.c` | Command history ring buffer |

A deeper, stage-by-stage explanation of every concept (the fork/exec split, pipe
fd hygiene, why built-ins can't be forked, zombie reaping, async-signal-safety,
process groups, and more) lives in
[`concept_walkthrough.md`](concept_walkthrough.md).

---

## System calls implemented (and why each matters)

| System call | Used for | Why it matters |
|-------------|----------|----------------|
| `fork` | Create a child process | The basis of running any external command; splits "new process" from "new program" |
| `execvp` | Replace the child with a program | Loads the requested binary; searches `$PATH` |
| `waitpid` | Collect a child's status | Synchronizes the shell with commands; reaps children (`WNOHANG`, `WUNTRACED`) |
| `pipe` | One-way kernel data channel | The mechanism behind `\|` — connects one command's output to another's input |
| `dup2` | Redirect a file descriptor | Powers both pipes and `<`/`>`/`>>` — points stdin/stdout at a pipe or file |
| `open` / `close` | Open/close files | File redirection targets; here-doc temp files; fd hygiene |
| `setpgid` | Put a process in a group | Lets a whole pipeline be signaled/controlled as one unit |
| `tcsetpgrp` | Set the terminal's foreground group | Hands Ctrl+C/Ctrl+Z to the running job instead of the shell |
| `sigaction` | Install signal handlers | The `SIGCHLD` reaper; ignoring interactive signals in the shell |
| `sigprocmask` | Block/unblock signals | Guards the jobs table against the reaper (race-free job control) |
| `kill` | Send a signal | `kill %N` / SIGCONT for `fg`/`bg`; signals a whole process group |
| `chdir` / `setenv` | Change cwd / environment | Why `cd` and `export` must be built-ins (they mutate the shell itself) |
| `glob` | Pathname expansion | Implements `*.c`-style wildcards the way a real shell does |

---

## What I learned

- **Unix splits "new process" from "new program" on purpose.** `fork` then
  `execvp` is the whole game: the gap between them is where the child redirects
  its own file descriptors before becoming the target program.
- **Pipes live or die by closing file descriptors.** A pipe's read end only sees
  EOF once *every* copy of the write end is closed — and `fork` duplicates them
  all. One leaked fd hangs the pipeline forever.
- **Built-ins can't be forked.** `cd` in a child changes the child's directory,
  then the child exits — the shell never moves. State-changing commands must run
  in the shell process.
- **Background jobs force you to think about concurrency.** A `SIGCHLD` handler
  reaps children asynchronously, which races the foreground `waitpid` and the
  jobs table — solved by blocking `SIGCHLD` with `sigprocmask` around the
  critical sections, and keeping the handler async-signal-safe.
- **Job control is all about process groups and the terminal.** Ctrl+C reaches
  whichever process group owns the terminal; the shell puts each job in its own
  group and hands over the terminal so the signal hits the job, not the shell.
  The sharp edge: an ignored signal *survives* `exec`, so children must reset
  them to default or the program you launch is unkillable.

---

## Notes on portability

Developed and tested on macOS (Apple Silicon). The code is POSIX and also builds
on Linux with the same `Makefile`. Two macOS-specific choices:

- **No AddressSanitizer.** On this macOS version ASan hangs at startup (a runtime
  incompatibility, not a code bug), so the debug build uses
  UndefinedBehaviorSanitizer and memory-leak checking is done with Apple's
  `leaks` tool (`make leaks`) instead of Valgrind, which isn't viable on arm64.
- **Job control is interactive-only.** All terminal-control calls are gated on
  `isatty`, so the shell also runs correctly with piped/scripted input
  (`printf '...' | ./mysh`), which is how its non-interactive behavior is tested.

### Known limitations

These are deliberate scope cuts, documented rather than hidden:

- Operators must be space-separated (`ls > out`, not `ls >out`; `sleep 5 &`).
- No quoting yet, so expansions aren't word-split and quotes aren't special.
- Here-doc bodies are taken literally (no `$VAR` expansion inside them).
- A killed background job is reported as `Done` rather than `Terminated`.
