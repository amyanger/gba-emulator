#ifndef FILEIO_H
#define FILEIO_H

#include <stdbool.h>
#include <stdio.h>

/* Push buffered data to disk so a full disk or I/O error surfaces before a
 * rename can replace the previous good file. */
bool fileio_sync(FILE* f);

/* Atomically replace dst with src. MSVC rename() fails when dst exists, so
 * Windows uses MoveFileEx, which keeps the old file until the new one is in place. */
bool fileio_replace(const char* src, const char* dst);

#endif
