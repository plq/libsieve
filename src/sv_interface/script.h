/* script.h -- script definition
 * Larry Greenfield
 * $Id$
 */
/***********************************************************
        Copyright 1999 by Carnegie Mellon University

                      All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the name of Carnegie Mellon
University not be used in advertising or publicity pertaining to
distribution of the software without specific, written prior
permission.

CARNEGIE MELLON UNIVERSITY DISCLAIMS ALL WARRANTIES WITH REGARD TO
THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS, IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY BE LIABLE FOR
ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
******************************************************************/

#ifndef SIEVE_SCRIPT_H
#define SIEVE_SCRIPT_H

#include "sieve_interface.h"
#include "message.h"
#include "interp.h"
#include "tree.h"

struct sieve_script {
    sieve_interp_t interp;
    sieve_support_t support;
    void *script_context;
    commandlist_t *cmds;
    int err;
    int lineno;

    /* The following must correspond
     * to what is set in sieve2_script_register.
     * Sieve 1 just ignores these variables.
     * */
    char *char_array;
//    size_t size;
//    void *callback;
//    void *include_callback;
//    FILE *file_pointer;
};

typedef struct sieve_imaponeflag {
    char *flag;
} sieve_imaponeflag_t;

int libsieve_call_getheader(sieve_interp_t *i, void *m, const char *s, const char ***val);
int libsieve_call_getsize(sieve_interp_t *i, void *m, int *sz);
int libsieve_call_getenvelope(sieve_interp_t *i, void *m, const char *f, const char ***c);
int libsieve_eval(sieve_interp_t *i, commandlist_t *c, 
		void *m, action_list_t *actions,
		const char **errmsg);

#endif /* SIEVE_SCRIPT_H */
