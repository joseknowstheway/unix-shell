# Recording the demo

The README references `demo.cast` (an [asciinema](https://asciinema.org)
recording) and an optional `demo.gif`. Job control (Ctrl+C / Ctrl+Z) only works
on a real terminal, so this has to be recorded interactively. Here's how.

## 1. Install the tools (macOS)

```bash
brew install asciinema      # the recorder
brew install agg            # optional: converts .cast -> .gif
```

## 2. Record

From the project directory:

```bash
make
asciinema rec demo.cast --command "./mysh" --overwrite
```

This launches `mysh` inside the recording. Type the script below at a calm,
readable pace. When you type `exit`, the shell quits and the recording stops.

> Tip: keep the terminal window a modest size (e.g. 90×28) so the GIF is crisp.

## 3. The script to type

Type these lines one at a time. Comments (after `#`) are just notes — don't type
them.

```text
help                                  # show the built-ins
echo hello world                      # a simple command
ls src | grep .c | wc -l              # a 3-stage pipeline
echo "build log" > out.txt            # output redirection
cat out.txt                           # read it back
echo "second line" >> out.txt         # append
cat out.txt
grep second < out.txt                 # input redirection
export GREETING=hi                    # set an environment variable
echo $GREETING from $USER             # variable expansion
echo exit status was $?               # $? expansion
ls src/*.h                            # wildcard globbing
cat << EOF                            # here-document
hello from a heredoc
EOF
sleep 30 &                            # background job
jobs                                  # list it
sleep 100                             # foreground...
```

Now, **with `sleep 100` running in the foreground**, demonstrate job control:

1. Press **Ctrl+Z** → you'll see `[2] Stopped sleep 100`.
2. Type `jobs` → shows the running and stopped jobs.
3. Type `bg %2` → resumes it in the background.
4. Type `kill %1` then `kill %2` → terminate the jobs.
5. Run a quick `sleep 5` and press **Ctrl+C** → it dies, the shell survives.
6. Type `rm out.txt` to tidy up.
7. Type `exit`.

## 4. (Optional) Make a GIF for the README

```bash
agg demo.cast demo.gif
```

The README already points at both `demo.gif` (image) and `demo.cast` (clickable
player link), so once the files exist the demo section renders automatically.

## 5. Commit the recording

```bash
git add demo.cast demo.gif
git commit -m "Add recorded demo"
```
