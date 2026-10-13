/* The .gesso file and gessetto scripts, both Filo.

   A file is a script that only describes an image: (gesso version width
   height), then (palette "#rrggbb" ...) for indices 1 and up, then one
   (frame duration-ms "row" ...) per frame with two hex digits per pixel,
   then (tag name first last). It runs with Filo's core builtins and those
   four, under a step limit, so opening a file can do nothing else.

   An ops script adds the edits of gesso.h on a document: what the corpus
   and `gessetto -apply` run. */
#ifndef GESSO_FILO_H
#define GESSO_FILO_H

#include "filo.h"
#include "gesso.h"

typedef struct {
    filo_ctx ctx; /* first: a builtin finds the script from its ctx */
    gesso_image *img;
    gesso_doc *doc; /* NULL for a file */
    int stage;
} gesso_script;

/* mem is split between Filo's two arenas; files the size of a 256-frame
   256 x 256 image need about 64 MB. */
int gesso_script_file(gesso_script *s, gesso_image *out, uint8_t *mem, size_t cap);
int gesso_script_ops(gesso_script *s, gesso_doc *doc, uint8_t *mem, size_t cap);
/* FILO_OK, or FILO_ERR with the message and position in the script's ctx. */
int gesso_script_run(gesso_script *s, const uint8_t *src, size_t len);

/* Writes img as a .gesso file. Returns the length it needs; writes nothing
   past cap. */
size_t gesso_write(const gesso_image *img, uint8_t *dst, size_t cap);

#endif
