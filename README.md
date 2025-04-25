# Simple Shell (sshell)

## Overview
This is a simple Unix shell implementation for ECS 150 (Operating Systems) Project #1. The shell supports basic command execution, built-in commands, I/O redirection, command pipelines, and background job execution.

## Features
- Basic command execution with arguments
- Built-in commands:
  - `exit`: Exit the shell (will error if background jobs are running)
  - `cd <directory>`: Change the current working directory
  - `pwd`: Print the current working directory
- I/O redirection:
  - `>`: Output redirection
  - `<`: Input redirection
- Command pipes: Chain up to 4 commands with `|`
- Background jobs: Run commands in the background with `&`
- Error handling: Comprehensive error checking for various edge cases

## Implementation
The shell is implemented in C and consists of the following major components:
- A command parser that tokenizes and validates user input
- Command execution with proper process management
- Pipe handling for command chaining
- I/O redirection implementation
- Background job management

## Usage Examples
1. Basic command execution:
   ```
   sshell@ucd$ ls -l
   ```

2. I/O redirection:
   ```
   sshell@ucd$ cat < input.txt
   sshell@ucd$ ls > output.txt
   ```

3. Command pipelines:
   ```
   sshell@ucd$ ls -la | grep .c | wc -l
   ```

4. Background jobs:
   ```
   sshell@ucd$ sleep 10 &
   ```

5. Combining features:
   ```
   sshell@ucd$ grep "main" *.c | sort > results.txt
   ```

## Building the Project
To build the shell, simply run:
```
make
```

This will compile the source code and create the `sshell` executable.

## Cleaning Up
To clean up build artifacts:
```
make clean
``` 