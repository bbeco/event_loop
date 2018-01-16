# Evented server example

## Overview
This is a simple code to show how an event based server actually works.

## How the code works
The server is a single thread single process application. It uses the select
syscall to block until one of his opened file descriptor is ready to perform
the requested operation.
Once the select() returns, the server checks its event list for the task
whose file descriptor match the one that is ready. The server then calls the proper
callback.

