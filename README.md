## General
This is a repository used for our university OS course.  
The operation system we worked on is **_xv6_** and the architecture is **_RISC-V_**.  
The original repository can be found here: [xv6-riscv]([xv6-riscv](https://github.com/mit-pdos/xv6-riscv))

In the course, we implemented many improvements over the original xv6 (see below in the assignments section) and learned basic operation system concepts, some were not implemented by us.  
The programming language we used was mostly _C_. However, we did a few things with the risc-v assembly.  
We mostly worked in pair programming sessions.

This repository is partitioned to branches, each corresponding to an assignment in the course, except for the *main* branch which holds the clean xv6-riscv version as it was when we started the course.

## Course assignments
The following sub-sections lists the summary of each assignment.  

**NOTE:** There was a 4th assignment, about the file system, but it was optional and we chose not to do it.

### [assignment-1](../../tree/assignment-1): System calls and process management
- Implementing various process scheduling policies, changing the  time quantum
- Storing and tracking several performance statistics for each process
- Adding a system call for tracing other system calls
- Adding a *PATH* environment variable

The full assignment description can be found [here](assignments-descriptions/assignment-1.pdf).

### [assignment-2](../../tree/assignment-2): Threads, signals and synchronization
- Implementing threads
- Implementing a basic signals mechanism
- Implementing binary and counting semaphore

[Full assignment description](assignments-descriptions/assignment-2.pdf)

### [assignment-3](../../tree/assignment-3): Memory management
- Implementing page swapping out to the disk and back in to memory (requires page fault handling)
- Implementing various page replacement policies

[Full assignment description](assignments-descriptions/assignment-3.pdf)

## Running xv6
Firstly, make sure to install risc-v GNU compiler toolchain. Use [this](https://pdos.csail.mit.edu/6.828/2019/tools.html) or [this](https://github.com/riscv/riscv-gnu-toolchain) as guides for installing.  
Also, you'll need to install _qemu_ compiled for _riscv64-softmmu_.

In order to run xv6, navigate into the source code root directory which is the directory of git files in this case.  
Then, you can run `make qemu` to run xv6.  
You can also run `make clean qemu` to rebuild xv6 and then run.

In order to exit xv6, press `Ctrl+A` and then `X`.

## Other resources
- [RISC-V Instruction Set Manual - Volume 1: User-Level ISA](https://riscv.org/wp-content/uploads/2017/05/riscv-spec-v2.2.pdf)
- [RISC-V Instruction Manual - Volume 2: Privileged Architecture](https://riscv.org/wp-content/uploads/2017/05/riscv-privileged-v1.10.pdf)

<br/>
Below is the README file of the original xv6 repository.

----

xv6 is a re-implementation of Dennis Ritchie's and Ken Thompson's Unix
Version 6 (v6).  xv6 loosely follows the structure and style of v6,
but is implemented for a modern RISC-V multiprocessor using ANSI C.

ACKNOWLEDGMENTS

xv6 is inspired by John Lions's Commentary on UNIX 6th Edition (Peer
to Peer Communications; ISBN: 1-57398-013-7; 1st edition (June 14,
2000)). See also https://pdos.csail.mit.edu/6.828/, which
provides pointers to on-line resources for v6.

The following people have made contributions: Russ Cox (context switching,
locking), Cliff Frey (MP), Xiao Yu (MP), Nickolai Zeldovich, and Austin
Clements.

We are also grateful for the bug reports and patches contributed by
Silas Boyd-Wickizer, Anton Burtsev, Dan Cross, Cody Cutler, Mike CAT,
Tej Chajed, Asami Doi, eyalz800, , Nelson Elhage, Saar Ettinger, Alice
Ferrazzi, Nathaniel Filardo, Peter Froehlich, Yakir Goaron,Shivam
Handa, Bryan Henry, jaichenhengjie, Jim Huang, Alexander Kapshuk,
Anders Kaseorg, kehao95, Wolfgang Keller, Jonathan Kimmitt, Eddie
Kohler, Austin Liew, Imbar Marinescu, Yandong Mao, Matan Shabtay,
Hitoshi Mitake, Carmi Merimovich, Mark Morrissey, mtasm, Joel Nider,
Greg Price, Ayan Shafqat, Eldar Sehayek, Yongming Shen, Fumiya
Shigemitsu, Takahiro, Cam Tenny, tyfkda, Rafael Ubal, Warren Toomey,
Stephen Tu, Pablo Ventura, Xi Wang, Keiichi Watanabe, Nicolas
Wolovick, wxdao, Grant Wu, Jindong Zhang, Icenowy Zheng, and Zou Chang
Wei.

The code in the files that constitute xv6 is
Copyright 2006-2020 Frans Kaashoek, Robert Morris, and Russ Cox.

ERROR REPORTS

Please send errors and suggestions to Frans Kaashoek and Robert Morris
(kaashoek,rtm@mit.edu). The main purpose of xv6 is as a teaching
operating system for MIT's 6.S081, so we are more interested in
simplifications and clarifications than new features.

BUILDING AND RUNNING XV6

You will need a RISC-V "newlib" tool chain from
https://github.com/riscv/riscv-gnu-toolchain, and qemu compiled for
riscv64-softmmu. Once they are installed, and in your shell
search path, you can run "make qemu".
