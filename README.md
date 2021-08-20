Stubborn *shuf*
===============
Shuffle lines from a file or stream. Toy clone of GNU shuf.

Why
---
This version of *shuf* has a superpower: it stubbornly tries to deal
with files and streams that don't fit into memory. When reading the
input, this *shuf* tries the following:

 1. Memory-mapping the input file/stream directly with *mmap()*.
 2. If that fails, it tries reading the file into memory.
 3. If memory is exhausted, a temporary file is created and the portion
    of the input already read, plus the rest, is copied into that
    temporary file. Then that file is memory-mapped.

Example
-------
Basic example:

    $ cat 3-lines.txt
    one
    two
    three

    $ ./shuf 3-lines.txt
    one
    three
    two

To see the above process play out, we pipe a 1 GB text file to *shuf*
(so it can't mmap it directly) on OpenBSD with strict memory limits to
force it to use a temporary file. The `-vv` option gets us debug output:

    $ cat text-1G.txt | ./shuf -vv >/dev/null
    trying mmap... fseek failed
    trying full read... realloc failed
    using a tmpfile... succeeded
    locating lines
    shuffling
    printing

Caveats
-------
On systems with overcommit (practically any stock Linux set up) the
system will kill the utility instead of failing an allocation, so it'll
never get to using a temporary file.

The Windows version doesn't have any superpower. It just tries to read
the input into memory.

Author
------
Sijmen Mulder (<ik@sjmulder.nl>).
