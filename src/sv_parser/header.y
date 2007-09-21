%{
/* header.y -- RFC 2/822 Header Parser
 * Aaron Stone
 * $Id$
 */
/* * * *
 * Copyright 2005 by Aaron Stone
 *
 * Licensed under the GNU Lesser General Public License (LGPL)
 * version 2.1, and other versions at the author's discretion.
 * * * */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Better yacc error messages please */
#define YYERROR_VERBOSE
/* Must be defined before header.h */
#define YYSTYPE char *

/* sv_util */
#include "util.h"
#include "callbacks2.h"

/* sv_parser */
#include "header.h"
#include "headerinc.h"
#include "header-lex.h"

/* sv_include */
#include "sieve2_error.h"

#define THIS_MODULE "sv_parser"
#define THIS_CONTEXT context

#define YYLEX_PARAM context->header_scanner
static header_list_t *hl = NULL;
%}

%name-prefix="libsieve_header"
%parse-param { struct sieve2_context *context }
%lex-param { void *yyscanner }

%token NAME COLON TEXT WRAP
%start headers

%%
headers: header                 {
                /* Allocate a new cache block */
                if (libsieve_headerappend(context, &hl) != SIEVE2_OK)
                    /* Problems... */;
                }
        | headers header        {
                /* Allocate a new cache block */
                if (libsieve_headerappend(context, &hl) != SIEVE2_OK)
                    /* Problems... */;
                };

header: NAME COLON              {
                TRACE_DEBUG( "header: NAME COLON: %s:", $1 );
                libsieve_headerentry(context, hl->h, $1, NULL);
                }
        | NAME COLON body       {
                TRACE_DEBUG( "header: NAME COLON body: %s:%s", $1, $3 );
                libsieve_headerentry(context, hl->h, $1, $3);
                };

body: TEXT                      {
                /* Default action is $$ = $1 */
                TRACE_DEBUG( "body: TEXT: %s", $1 );
                }
        | body WRAP             {
                TRACE_DEBUG( "body: body WRAP: %s %s", $1, $2 );
                $$ = libsieve_strbuf(context->ml, libsieve_strconcat( $1, $2, NULL ), strlen($1)+strlen($2), FREEME);
                };

%%

/* copy header error message into buffer provided by sieve parser */
void libsieve_headererror(struct sieve2_context *context, const char *s)
{
    TRACE_DEBUG( "Header parse error: %s", s);
}

/* Wrapper for headerparse() which sets up the 
 * required environment and allocates variables
 * */
header_list_t *libsieve_header_parse_buffer(struct sieve2_context *context, header_list_t **data, char **ptr, char **err)
{
    header_list_t *newdata = NULL;
    extern header_list_t *hl;

    hl = NULL;
    if (libsieve_headerappend(context, &hl) != SIEVE2_OK)
        /* Problems... */;

    context->header_ptr = *ptr;
    context->header_len = strlen(*ptr);
    libsieve_headerlex_init_extra(context, &context->header_scanner);

    if (libsieve_headerparse(context)) {
        libsieve_headerlex_destroy(context->header_scanner);
	while (hl) {
	    header_list_t *next = hl->next;
            libsieve_free(hl->h->contents);
            libsieve_free(hl->h);
            libsieve_free(hl);
	    hl = next;
	}
	hl = NULL;
	return NULL;
    }

    /* Get to the tail end... */
    newdata = *data;
    while(newdata != NULL) {
        newdata = newdata->next;
    }

    /* Same thing with that extra struct... */
    newdata = hl->next;
    libsieve_free(hl->h->contents);
    libsieve_free(hl->h);
    libsieve_free(hl);
    libsieve_headerlex_destroy(context->header_scanner);

    if(*data == NULL)
        *data = newdata;

    hl = newdata;
    return *data;
}

int libsieve_headerappend(struct sieve2_context *context, header_list_t **hl)
{
    header_list_t *newlist = NULL;
    header_t *newhead = NULL;
    char **c = NULL;

    newlist = (header_list_t *)libsieve_malloc(sizeof(header_list_t));
    if (newlist == NULL)
        return SIEVE2_ERROR_NOMEM;

    newhead = (header_t *)libsieve_malloc(sizeof(header_t));
    if (newhead == NULL) {
        libsieve_free(newlist);
        return SIEVE2_ERROR_NOMEM;
    }

    c = (char **)libsieve_malloc(2 * sizeof(char *));
    if (c == NULL) {
        libsieve_free(newlist);
        libsieve_free(newhead);
        return SIEVE2_ERROR_NOMEM;
    }

    TRACE_DEBUG( "Prepending a new headerlist and header struct" );
    newhead->count = 0;
    newhead->space = 1;
    newhead->contents = c;
    newhead->contents[0] = NULL;
    newhead->contents[1] = NULL;
    newlist->h = newhead;
    newlist->next = *hl;
    *hl = newlist;

    return SIEVE2_OK;
}

void libsieve_headerentry(struct sieve2_context *context, header_t *h, char *name, char *body)
{
    TRACE_DEBUG( "Entering name and body into header struct" );
    if (h == NULL)
        TRACE_DEBUG( "Why are you giving me a NULL struct!?" );
	/* Hmm, big big trouble here... */;

    size_t namelen = strlen(name);
    // h->name = libsieve_strtolower(libsieve_strndup(name, namelen), namelen);
    h->name = libsieve_strtolower(name, namelen);
    h->contents[0] = body;
    h->count = 1;

    /* This function is NOT designed for general purpose
     * entries, but only for making the very first entry!
     * */
}

