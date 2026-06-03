# Concept Walkthrough — Unix Shell (mysh)

> A running log of the **concepts** behind each stage of the build — written so I
> can *explain every line* in an interview, not just ship code. Each stage lists
> what was built, the key ideas, any bugs/mistakes made along the way (and their
> fixes), and the questions a Cisco-style interviewer is likely to ask (with
> answers).
>
> This document grows one section per stage. Code lives in `src/`; the original
> spec is `../unix_shell_project.md`.

---

## Table of Contents

- [Stage 1 — The Read-Eval Loop](#stage-1--the-read-eval-loop)
- [Stage 2 — Pipes](#stage-2--pipes)
- [Stage 3 — I/O Redirection](#stage-3--io-redirection)
- [Stage 4 — Built-in Commands](#stage-4--built-in-commands) *(upcoming)*
- [Stage 5 — Background Processes](#stage-5--background-processes) *(upcoming)*
- [Stage 6 — Signal Handling & Job Control](#stage-6--signal-handling--job-control) *(upcoming)*
- [Stage 7 — Polish & Presentation](#stage-7--polish--presentation) *(upcoming)*

---

## Stage 1 — The Read-Eval Loop

**Files:** `main.c`, `parser.c`, `parser.h`, `executor.c`, `executor.h`, `Makefile`
**Goal:** a shell that runs single commands — type `ls -la`, it runs `ls -la`;
type `pwd`, it runs `pwd`. No pipes, no redirection yet — just the core engine
and the loop everything else hangs off.

### Key concepts

**1. The REPL — five steps, forever.**
Every shell, from this one to bash, is the same loop: print a prompt → read a
line → parse it → execute it → repeat. `main.c` is literally that loop. Stages
2–6 don't change its shape; they each make one of those steps smarter (parse
learns pipes, execute learns redirection, etc.). Seeing the whole project as
"five steps and I'm extending one of them" is the mental model.

**2. Why a shell needs `fork()` *and* `execvp()` — the two-call split.**
Unix deliberately separates "create a new process" from "run a new program":
- **`fork()`** clones the calling process. After it returns there are **two**
  processes executing the same code, distinguished only by the return value:
  `0` in the child, the child's PID in the parent, `-1` on failure.
- **`execvp()`** discards the current process's program image and loads a new one
  in its place. On success it **does not return** — there's nothing to return to,
  the old code is gone.
If the shell called `execvp()` directly (no fork), the shell *itself* would be
replaced by `ls` and never come back. Forking first means the **child** gets
replaced while the **parent** stays alive to keep being the shell. The window
between `fork` and `exec` is also where Stages 2–3 will rewire the child's file
descriptors for pipes and redirection — so this split is what makes everything
later possible.

**3. `execvp` vs `execv` — the `p` is `$PATH`.**
`execvp("ls", ...)` searches each directory in `$PATH` for `ls`, which is why we
can type `ls` and not `/bin/ls`. The plain `execv` needs a full path. The `v`
means "vector" — arguments come as an array (`char *argv[]`), which must be
**NULL-terminated** so exec knows where the list ends. That NULL is why the
parser reserves `args[argc] = NULL`.

**4. `waitpid()` — why the parent blocks.**
After forking, the parent calls `waitpid(pid, &status, 0)` to block until that
specific child exits. Without it, the shell would print the next prompt and read
the next line *while the command was still running* — output would interleave
with the prompt. Passing the specific `pid` (not `-1`) means "wait for the
command I just launched," which matters once Stage 5 adds background jobs we
deliberately *don't* wait on.

**5. Decoding the status word — `WIFEXITED` / `WEXITSTATUS` / `WTERMSIG`.**
`waitpid` fills an `int status` that is **not** a plain exit code — it's a packed
word. You interrogate it with macros: `WIFEXITED(status)` is true for a normal
exit, and then `WEXITSTATUS(status)` is the 0–255 code the program returned;
`WIFSIGNALED(status)` is true if a signal killed it, and `WTERMSIG(status)` is
which one. The shell convention (which we follow) reports a signal death as
**128 + signal number** — that's why `$?` is 130 after Ctrl+C (128 + SIGINT=2).
This status is returned now so a future `$?` built-in can use it.

**6. Parsing in place with `strtok` — and who owns the strings.**
`parse_command` tokenizes the input line with `strtok`, which splits on
whitespace by **overwriting each delimiter with `\0`** and returning pointers
*into the original buffer*. That's efficient (no per-token allocation) but
creates an ownership rule: the `args[]` pointers **borrow** from the caller's
line buffer, so that buffer must stay alive as long as the `command_t` is used,
and must be writable. `main.c` respects this — it doesn't touch `line` again
until the next loop iteration, after execution is done.

**7. The fixed-size `command_t` with fields for later stages.**
The struct already has `input_file`, `output_file`, `append_mode`, and
`background` even though Stage 1 never sets them. Declaring them now means the
executor's data shape never changes as redirection (Stage 3) and background jobs
(Stage 5) land — only the code that *fills* them grows. It's also a fixed-size,
stack-allocated struct (`MAX_ARGS` cap), so the hot path does zero `malloc`.

**8. `getline()` over `gets`/fixed buffers — and EOF.**
`getline(&line, &cap, stdin)` grows its buffer as needed (no overflow risk,
unlike `gets`) and reuses the same allocation across iterations. It returns `-1`
at end-of-file — which is what **Ctrl+D on an empty line** produces — so that's
how an interactive shell cleanly exits. We print a newline and break. The single
`free(line)` at the end is why `make leaks` reports zero leaks.

### Bugs & mistakes (and fixes)

**1. AddressSanitizer hung the machine at startup (the big one).**
The natural debug build is `-fsanitize=address`. On this Mac it **hung at ~100%
CPU producing zero output** — not even the first `mysh> ` prompt — and left
*runaway processes* spinning (found via `ps`, three copies pegging the CPU).
- **Diagnosis:** isolated it by building three ways — plain `-g`, UBSan only, and
  ASan only. Plain and UBSan worked; **only ASan hung**. Running with
  `ASAN_OPTIONS=verbosity=2` printed two lines and stopped at
  `FindDynamicShadowStart` — the routine where ASan mmaps its shadow-memory
  region. On macOS 26 (Darwin 25) ASan's shadow-finding probe loops forever.
  This is an **ASan-runtime-vs-OS incompatibility, not a code bug** (proven: the
  same source is clean under UBSan and `leaks`).
- **Fix:** `make debug` now uses **UndefinedBehaviorSanitizer** (which works
  here), and leak checking uses Apple's native **`leaks`** tool via `make leaks`
  (reports "0 leaks for 0 total leaked bytes").
- **Lesson:** when testing a binary that might spin, run it under a **watchdog**
  that kills it after a few seconds — otherwise a startup hang silently piles up
  runaway processes.

**2. A failed `execvp` must terminate the child, not fall through.**
After `execvp` fails (e.g. command not found), the child keeps running the
shell's code. If it returned to the REPL loop, you'd have **two shells reading
one keyboard**. The fix is the `exit(EXIT_FAILURE)` immediately after `execvp` —
it only ever runs in the child, and only when exec failed.

**3. Command-not-found message didn't name the command.**
The first version printed a bare `mysh: No such file or directory`. Real shells
name the offender. Fixed by printing `mysh: <cmd>: <strerror(errno)>` ourselves
(plain `perror` can't insert the command name), so it now reads
`mysh: nosuchcmd: No such file or directory`.

**4. `make` rebuilds by timestamp, not by flags.**
Carried over as a known trap from the packet-sniffer build: `make` decides what
to rebuild by comparing file mtimes, *not* compiler flags. So the release and
debug recipes write to **separate** output names (`mysh` vs `mysh-debug`) — a
shared name would let a stale debug binary masquerade as the release build.

### Reading the output

```
mysh> pwd
/Users/.../unix-shell
mysh> echo hello world
hello world
mysh> ls
Makefile  mysh  src
mysh> nosuchcmd
mysh: nosuchcmd: No such file or directory     ← error handled, shell survives
mysh> exit
```

### Likely interview questions

- *Why does a shell need both `fork` and `exec` instead of one call?* → `exec`
  replaces the current process image and never returns; calling it directly would
  replace the shell itself. Forking first means the child is replaced while the
  parent lives on to keep being the shell — and the fork/exec gap is where you
  customize the child (redirect fds) before the new program starts.
- *What does `fork` return, and how do you tell parent from child?* → `0` in the
  child, the child's PID in the parent, `-1` on failure.
- *What's the difference between `execvp` and `execv`?* → `execvp` searches
  `$PATH` for the program; `execv` needs a full path. The `v` = argument vector
  (NULL-terminated array).
- *Why call `waitpid`? What happens without it?* → it blocks the parent until the
  child exits; without it the prompt prints and the next line is read before the
  command finishes, interleaving output. (And it reaps the child — skipping it
  leaves zombies, which Stage 5's SIGCHLD handling addresses for background jobs.)
- *`waitpid` gives you a `status` int — is that the exit code?* → No, it's a
  packed status word. Use `WIFEXITED`/`WEXITSTATUS` for a normal exit and
  `WIFSIGNALED`/`WTERMSIG` for a signal death; the shell convention reports
  signal deaths as 128 + signal number.
- *Why must the child `exit()` if `execvp` fails?* → otherwise it falls back into
  the REPL and you get a second shell reading the same input.
- *How does your parser manage memory for the tokens?* → `strtok` tokenizes the
  line in place and the arg pointers borrow from that buffer; no per-token
  allocation, but the line must stay alive and writable while the command is used.
- *How does the shell know to stop (EOF)?* → `getline` returns `-1` at
  end-of-file (Ctrl+D on an empty line); we break the loop.
- *How did you check for memory safety without AddressSanitizer?* → ASan hangs at
  startup on this macOS version (a known runtime/OS incompatibility), so I used
  UBSan plus Apple's `leaks` tool — zero leaks, warning-free build, and an
  allocation-light path.

---

## Stage 2 — Pipes

**Files:** `parser.c`, `parser.h`, `executor.c`, `executor.h` (+ `main.c` wiring)
**Goal:** chain commands with `|` so one command's stdout becomes the next one's
stdin. `ls -la | grep .c`, `cat /etc/passwd | grep root | wc -l`, and
`echo hi | tr a-z A-Z` all work.

### Key concepts

**1. What a pipe actually is.**
`pipe(int fds[2])` asks the kernel for a one-way, in-memory FIFO buffer and
returns two file descriptors onto it: `fds[0]` is the **read** end, `fds[1]` is
the **write** end. Bytes written to `fds[1]` come out of `fds[0]`, in order. It's
the same primitive the kernel uses everywhere; a shell pipeline is just two
processes sharing one.

**2. `dup2(oldfd, newfd)` — redirecting a standard stream.**
A child still runs a normal program like `grep` that reads from fd 0 (stdin) and
writes to fd 1 (stdout) — it knows nothing about pipes. `dup2(pipefd[1],
STDOUT_FILENO)` makes fd 1 a *copy* of the pipe's write end, so when `grep`
writes to stdout it's really writing into the pipe. `dup2` atomically closes the
old `newfd` first, then duplicates. This is the exact mechanism Stage 3 reuses
for `>` and `<` — file redirection and pipes are the same idea pointed at
different fds.

**3. The closing discipline — the #1 pipe bug.**
A pipe's read end reports EOF **only when every copy of the write end is
closed**. `fork` duplicates all open fds, so after forking the children of a
pipeline, the *parent* and several children may each hold a copy of the same
write end. If even one stays open, the reader downstream blocks forever waiting
for an EOF that never comes — the pipeline hangs. The rule the code follows:
after `dup2`, close the original; and in the parent, close every pipe end the
moment it's been handed off to a child. We close in both children and the parent,
aggressively.

**4. Why all stages run concurrently (not one-then-the-next).**
A pipe's kernel buffer is small (often 64 KB). If the shell ran `producer` fully,
then `consumer`, a producer emitting more than the buffer holds would block on a
full pipe with nobody reading — deadlock. So every command in the pipeline is
forked up front and runs at the same time; the kernel schedules them and applies
back-pressure (a full pipe blocks the writer, an empty one blocks the reader)
automatically.

**5. SIGPIPE — graceful early exit (`yes | head`).**
When a downstream command finishes early (`head -3` stops after 3 lines and
closes its read end), the upstream `yes` keeps writing — into a pipe with no
reader. The kernel sends it **SIGPIPE**, whose default action is to terminate the
process. That's *correct*: `yes` dies, the pipeline ends, and the shell doesn't
hang. We left the children's default SIGPIPE in place, so this just works. (The
shell itself never writes to a pipe, so it's unaffected.)

**6. Pipeline exit status = the last command.**
`execute_pipeline` reaps all children but reports only the **last** command's
status as `$?` — bash's convention. So `false | true` is success and `true |
false` is failure. The other children are still `waitpid`'d so they don't become
zombies.

**7. `strtok_r` for two-level parsing.**
Parsing is now two passes: split on `|`, then split each segment on whitespace.
Plain `strtok` keeps its progress in one hidden global, so a tokenization nested
inside another would corrupt it. `strtok_r` stores that state in a caller-owned
`saveptr`, giving the outer (pipe) and inner (argument) passes independent
bookmarks. A pipeline of length 1 falls out for free — no `|`, one segment, one
command — so the executor needs only a single entry point.

### Bugs & mistakes (and fixes)

This stage compiled clean on the first try and ran leak-free — *because* the two
classic traps were designed around up front. Worth recording the traps and the
specific decisions that avoid them, since both are prime interview territory:

**1. The pipe-hang trap (avoided by closing discipline).**
The failure mode — a pipeline that hangs forever — comes from a single leaked
write-end fd keeping EOF from ever firing. The defense is mechanical: every
`dup2` is immediately followed by closing the original, and the parent closes
each pipe end the instant it's been handed to a child. The `yes | head -3` test
exists specifically to catch a regression here: if any fd leaked, that command
would hang instead of printing 3 lines and stopping.

**2. The `strtok` re-entrancy trap (avoided by `strtok_r`).**
The intuitive way to write the two-level parse — outer `strtok` on `|`, calling a
helper that also uses `strtok` on whitespace — silently breaks, because the inner
loop overwrites the outer loop's hidden global cursor. Symptom would be commands
after the first `|` getting mangled or dropped. Switching both passes to
`strtok_r` (each with its own `saveptr`) removes the shared state entirely.

**3. Known limitation, accepted for now: malformed pipes.**
Because the `|` split uses `strtok_r` (which collapses consecutive delimiters and
ignores leading/trailing ones), inputs like `ls | | grep` or `ls |` are handled
*leniently* — the empty segment is skipped rather than reported as
`syntax error near unexpected token '|'` the way bash does. Proper detection
needs a hand-rolled scan that preserves empty fields. Documented as a deliberate
Stage 2 simplification; a candidate for the Stage 7 polish pass.

### Reading the output

```
mysh> ls src | grep .c
executor.c
main.c
parser.c
mysh> cat /etc/passwd | grep root | wc -l    ← three-stage pipeline
       3
mysh> echo hello world | tr a-z A-Z
HELLO WORLD
mysh> yes | head -3                          ← must NOT hang
y
y
y
mysh>
```

### Likely interview questions

- *What does `pipe()` give you, and which end is which?* → two fds onto one
  kernel buffer: `fds[0]` read, `fds[1]` write; bytes written to the write end
  come out the read end.
- *How do you connect one command's output to another's input?* → `dup2` the
  pipe's write end onto the producer's stdout and the read end onto the
  consumer's stdin, so the unchanged programs read/write the pipe via fd 0/1.
- *What's the most common bug with pipes, and how do you prevent it?* → leaking a
  pipe fd: the read end never sees EOF until all write-end copies are closed, so
  the pipeline hangs. Close every end you don't use, in both children and parent,
  right after `dup2`.
- *Why must the parent close its copies of the pipe fds too?* → `fork` duplicated
  them; if the parent keeps a write end open, the reader's EOF never fires even
  though the producing child finished.
- *Why run all commands at once instead of sequentially?* → the pipe buffer is
  finite; a producer that outruns it blocks until a concurrent consumer drains
  it. Running them serially would deadlock on the full buffer.
- *What happens in `yes | head`? Why doesn't it hang?* → `head` exits and closes
  the read end; `yes` then writes to a reader-less pipe, gets SIGPIPE, and is
  terminated by the default handler. The pipeline ends cleanly.
- *What exit status does a pipeline return?* → the last command's (bash
  convention); the rest are still reaped to avoid zombies.
- *Why `strtok_r` instead of `strtok` here?* → the parse is nested (split on `|`,
  then on whitespace); `strtok`'s single hidden cursor can't support two
  simultaneous tokenizations, but `strtok_r`'s per-call `saveptr` can.

---

## Stage 3 — I/O Redirection

**Files:** `parser.c`, `parser.h`, `executor.c` (+ `main.c` wiring)
**Goal:** send a command's output to a file (`>`), append to it (`>>`), or feed
its input from a file (`<`) — and have these compose with pipes.

### Key concepts

**1. Redirection is the *same* `dup2` trick as pipes, aimed at a file.**
Stage 2 pointed a child's stdout at a pipe's write end. Redirection points it at
an **open file descriptor** instead. `open()` returns an fd for the file;
`dup2(fd, STDOUT_FILENO)` makes fd 1 a copy of it, so the unchanged program
writes to "stdout" and the bytes land in the file. Input is the mirror image:
`dup2(fd, STDIN_FILENO)`. Pipes and files are interchangeable behind a file
descriptor — that uniformity ("everything is a file descriptor") is one of Unix's
core design ideas, and this stage is it in miniature.

**2. The `open()` flags ARE the feature.**
The only difference between overwrite and append is the flag set:
- `>`  → `O_WRONLY | O_CREAT | O_TRUNC`  — create if absent, else truncate to 0.
- `>>` → `O_WRONLY | O_CREAT | O_APPEND` — create if absent, else each write
  seeks to end first (atomically, even with concurrent writers).
- `<`  → `O_RDONLY` — open existing file for reading; no create.

The third argument to `open` (`0644`) is the permission mode for a newly created
file, modified by the process umask. It's ignored when no file is created.

**3. Where redirection is applied — child only, and *after* pipe wiring.**
`apply_redirection` runs inside the child, at the top of `exec_child`, which is
*after* the pipeline code has already dup2'd any pipe ends onto stdin/stdout. So
an explicit file redirection **overrides** the pipe default. That's the correct
precedence: in `a | b > out`, `b`'s stdout must go to the file, not onward; in
`sort < in | head`, `sort`'s stdin comes from the file while its stdout still
feeds the pipe. Putting redirection in the shared `exec_child` means both the
single-command and pipeline paths get it for free.

**4. Close the fd after `dup2`.**
Once `dup2(fd, STDOUT_FILENO)` has copied the file onto fd 1, the original `fd`
number is redundant — we `close(fd)` immediately. Same discipline as pipes:
don't leave extra descriptors open. (Leaving a file fd open is less catastrophic
than a pipe fd — it won't hang anything — but it's a descriptor leak, and on a
long-running shell descriptors are finite.)

**5. Parsing: operators are tokens, not arguments.**
The parser recognizes `<`, `>`, `>>` as their own tokens, takes the **next**
token as the filename, and stores them in `command_t.input_file` /
`output_file` / `append_mode` — adding *none* of them to `args`. The program
itself never sees the operator or the filename; it just finds its standard
streams already redirected. That's exactly how a real shell works:
`wc -l < file` runs `wc` with argv `["wc","-l"]` and stdin attached to the file,
which is why it prints just a number with no filename (unlike `wc -l file`).

**6. Syntax errors and a new `-1` return contract.**
A dangling operator like `ls >` has no filename. `parse_command` now returns
`-1` (distinct from `0` = blank line) after printing
`mysh: syntax error: expected filename after '>'`; `parse_pipeline` propagates
it, and `main` treats `<= 0` as "nothing to run." A bad filename at runtime
(e.g. `< nonexistent`) is caught by `open` in the child, which reports and exits
with failure — the shell itself is untouched.

### Bugs & mistakes (and fixes)

Like Stage 2, this compiled clean and ran leak-free on the first build — because
the one genuine design subtlety was handled deliberately. The points worth
recording:

**1. The precedence ordering (the real design decision).**
The trap: if file redirection were applied *before* the pipe `dup2`s (or in the
pipeline code instead of `exec_child`), then `a | b > out` would let the pipe
clobber the file redirection and `b`'s output would go to the wrong place. Fix /
decision: apply redirection **after** all pipe wiring, inside `exec_child`, so the
explicit file always wins. Verified live with both `ls src | grep .c > file`
(redirect on the last stage) and `sort < file | head -1` (redirect on the first
stage).

**2. Distinguishing "blank line" from "syntax error."**
Before Stage 3, `parse_*` returned `0` for "nothing." Redirection needs a third
outcome — a real error the user should see. Rather than overload `0`, the contract
grew a `-1` case, and `main`'s check changed from `== 0` to `<= 0`. Small, but it
keeps "empty input" (silent re-prompt) and "you typed something invalid" (printed
error) cleanly separate.

**3. Known limitations, accepted for now.**
- **Attached operators** aren't supported: you must write `ls > out.txt`, not
  `ls >out.txt`. The token-based parser treats `>out.txt` as one token. Bash
  splits it; we don't (yet).
- **Redirection with no command** (`> file` alone, which bash uses to truncate a
  file) is a no-op here — a zero-arg segment is skipped before any file is
  touched.
Both are candidates for the Stage 7 polish pass and are documented in `parser.h`.

### Reading the output

```
mysh> echo first line  > /tmp/t.txt
mysh> echo second line >> /tmp/t.txt     ← append, doesn't clobber
mysh> cat /tmp/t.txt
first line
second line
mysh> grep second < /tmp/t.txt           ← stdin from file
second line
mysh> wc -l < /tmp/t.txt                  ← just a number (no filename arg)
       2
mysh> ls src | grep .c > /tmp/c.txt       ← pipe AND redirect together
mysh> cat < /tmp/nonexistent
mysh: /tmp/nonexistent: No such file or directory
mysh> ls >
mysh: syntax error: expected filename after '>'
```

### Likely interview questions

- *How does output redirection actually work?* → `open` the file, `dup2` its fd
  onto STDOUT_FILENO so the program's stdout writes go to the file, then close
  the original fd.
- *What's the difference between `>` and `>>` at the syscall level?* → the open
  flags: `O_TRUNC` empties the file first; `O_APPEND` makes every write go to the
  current end. Everything else is identical.
- *Why is `wc -l < file` different from `wc -l file`?* → with `<`, the shell
  attaches the file to `wc`'s stdin and `wc` sees no filename argument (so it
  prints only the count); with `file`, `wc` opens it itself and prints the name
  too.
- *How do redirection and pipes interact in `a | b > out`?* → both use `dup2`;
  redirection is applied after the pipe wiring so the file wins — `b`'s output
  goes to `out`, not to a downstream pipe.
- *Where do you apply the redirection — parent or child?* → the child, after fork
  and after any pipe setup, before exec; it must not affect the shell's own fds.
- *What's the third argument to `open` and when does it matter?* → the permission
  mode for a newly created file (e.g. 0644), masked by umask; ignored if the file
  already exists or isn't being created.
- *How do you handle a redirection to a file you can't open?* → `open` fails in
  the child; report via `strerror(errno)` and exit the child with failure, leaving
  the shell running.

---

*Stages 4–7 are documented here as they're built — one section each, same shape:
what was built, key concepts, bugs & fixes, and interview Q&A.*
