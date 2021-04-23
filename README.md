# kmp - parallel string-searching using Knuth-Morris-Pratt algorithm
kmp is a parallel program based on Knuth-Morris-Pratt algorithm. It is implemented using OpenMP and MPI.

Multiple functionalities are provided. Given in input one or more words, a file-based, text-based and live network capture searches can be performed.

## Required libraries
+ [libpcap](https://www.tcpdump.org/) (a portable C library for network capture)
+ [openmp](https://www.openmp.org/) (shared-memory multiprocessing programming API)
+ [mpi](https://www.open-mpi.org/) (an open-source Message-Passing-Interface implementation)

## Compilation
make

## Usage
`./kmp [OPTION...] words...`

Try `kmp --help` or `kmp --usage` for more information.