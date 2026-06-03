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
- [Stage 2 — Pipes](#stage-2--pipes) *(upcoming)*
- [Stage 3 — I/O Redirection](#stage-3--io-redirection) *(upcoming)*
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

*Stages 2–7 are documented here as they're built — one section each, same shape:
what was built, key concepts, bugs & fixes, and interview Q&A.*
