/* script.c -- sieve script functions
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

/* sv_rsamd5 */
#include "md5global.h"
#include "md5.h"

/* sv_include */
#include "sieve_interface.h"
#include "sieve2_interface.h"

/* sv_interface */
#include "message.h"
#include "message2.h"
#include "interp.h"
#include "script.h"
#include "sieve.h"
#include "tree.h"

/* sv_util */
#include "util.h"

/* Sieve 2 update: allow support to be filled directly
 * without requiring interp to contain anything valid
 * */
/* Checks if interpreter supports specified action */
int script_require(sieve_script_t *s, char *req)
{
    if (0 == strcmp("fileinto", req)) {
        if (s->interp.fileinto)
	    s->support.fileinto = 1;
        return s->support.fileinto;
    } else if (0 == strcmp("reject", req)) {
        if (s->interp.reject)
	    s->support.reject = 1;
        return s->support.reject;
    } else if (!strcmp("envelope", req)) {
	if (s->interp.getenvelope)
	    s->support.envelope = 1;
        return s->support.reject;
    } else if (!strcmp("vacation", req)) {
	if (s->interp.vacation)
	    s->support.vacation = 1;
        return s->support.vacation;
    } else if (!strcmp("imapflags", req)) {
	if (s->interp.markflags->flag)
	    s->support.imapflags = 1;
        return s->support.imapflags;
    } else if (!strcmp("notify",req)) {
	if (s->interp.notify)
	    s->support.notify = 1;
        return s->support.notify;
#ifdef ENABLE_REGEX
    /* If regex is enabled then it is supported! */
    } else if (!strcmp("regex", req)) {
	s->support.regex = 1;
	return 1;
#endif
    /* Subaddress support is built into the parser! */
    } else if (!strcmp("subaddress", req)) {
	s->support.subaddress = 1;
	return 1;
    } else if (!strcmp("comparator-i;octet", req)) {
	return 1;
    } else if (!strcmp("comparator-i;ascii-casemap", req)) {
	return 1;
    }
    /* If we don't recognize it, then we don't support it! */
    return 0;
}

/* given an interpretor and a script, produce an executable script */
/* this one takes the script in as a char array rather than a FILE */
int sieve_script_buffer(sieve_interp_t *interp, char *script,
		       void *script_context, sieve_script_t **ret)
{
    sieve_script_t *s;
    int res = SIEVE_OK;
    extern int sievelineno;

    res = interp_verify(interp);
    if (res != SIEVE_OK) {
	return res;
    }

    s = (sieve_script_t *) sv_malloc(sizeof(sieve_script_t));
    s->interp = *interp;
    s->script_context = script_context;
    /* clear all support bits */
    memset(&s->support, 0, sizeof(struct sieve_support));

    s->err = 0;

    /* These used to be in sieve.y, but needed to be higher */
    addrlexalloc();
    sievelexalloc();
    sievelineno = 1;		/* reset line number */
    s->cmds = sieve_parse_buffer(s, script);
    if (s->err > 0) {
	if (s->cmds) {
	    free_tree(s->cmds);
	}
	s->cmds = NULL;
	res = SIEVE_PARSE_ERROR;
    }

    *ret = s;
    return res;
}

/* given an interpretor and a script, produce an executable script */
/* this one takes the script in as a FILE rather than a char array */
int sieve_script_parse(sieve_interp_t *interp, FILE *script,
		       void *script_context, sieve_script_t **ret)
{
    sieve_script_t *s;
    int res = SIEVE_OK;
    extern int sievelineno;

    res = interp_verify(interp);
    if (res != SIEVE_OK) {
	return res;
    }

    s = (sieve_script_t *) sv_malloc(sizeof(sieve_script_t));
    s->interp = *interp;
    s->script_context = script_context;
    /* clear all support bits */
    memset(&s->support, 0, sizeof(struct sieve_support));

    s->err = 0;

    /* These used to be in sieve.y, but needed to be higher */
    addrlexalloc();
    sievelexalloc();
    sievelineno = 1;		/* reset line number */
    s->cmds = sieve_parse(s, script);
    if (s->err > 0) {
	if (s->cmds) {
	    free_tree(s->cmds);
	}
	s->cmds = NULL;
	res = SIEVE_PARSE_ERROR;
    }

    *ret = s;
    return res;
}

int sieve_script_free(sieve_script_t **s)
{
    if (*s) {
	if ((*s)->cmds) {
	    free_tree((*s)->cmds);
	}
	sv_free(*s);
    }
    
    /* These used to be in sieve.y, but needed to be higher */
    addrlexfree();
    sievelexfree();

    return SIEVE_OK;
}

static int sysaddr(char *addr)
{
    if (!strncasecmp(addr, "MAILER-DAEMON", 13))
	return 1;

    if (!strncasecmp(addr, "LISTSERV", 8))
	return 1;

    if (!strncasecmp(addr, "majordomo", 9))
	return 1;

    if (strstr(addr, "-request"))
	return 1;

    if (!strncmp(addr, "owner-", 6))
	return 1;

    return 0;
}

/* look for myaddr and myaddrs in the body of a header - return the match */
static char* look_for_me(char *myaddr, stringlist_t *myaddrs, const char **body)
{
    char *found = NULL;
    int l;
    stringlist_t *sl;

    /* loop through each TO header */
    for (l = 0; body[l] != NULL && !found; l++) {
	struct address *data = NULL;
	struct addr_marker *marker = NULL;
	char *addr;
	
	parse_address(body[l], &data, &marker);
	/* loop through each address in the header */
	while (!found && ((addr = get_address(ADDRESS_ALL, 
					      &data, &marker, 1)) != NULL)) {
	    if (!strcasecmp(addr, myaddr)) {
		found = myaddr;
		break;
	    }

	    for (sl = myaddrs; sl != NULL && !found; sl = sl->next) {
		struct address *altdata = NULL;
		struct addr_marker *altmarker = NULL;
		char *altaddr;

		/* is this address one of my addresses? */
		parse_address(sl->s, &altdata, &altmarker);
		altaddr = get_address(ADDRESS_ALL, &altdata, &altmarker, 1);
		if (!strcasecmp(addr, altaddr))
		    found = sl->s;

		free_address(&altdata, &altmarker);
	    }
	}
	free_address(&data, &marker);
    }

    return found;
}

/* evaluates the test t. returns 1 if true, 0 if false.
 */
static int evaltest(sieve_interp_t *i, test_t *t, void *m)
{
    testlist_t *tl;
    stringlist_t *sl;
    patternlist_t *pl;
    int res = 0;
    int addrpart = 0;

    switch (t->type) {
    case ADDRESS:
    case ENVELOPE:
	res = 0;
	switch (t->u.ae.addrpart) {
	case ALL: addrpart = ADDRESS_ALL; break;
	case LOCALPART: addrpart = ADDRESS_LOCALPART; break;
	case DOMAIN: addrpart = ADDRESS_DOMAIN; break;
	case USER: addrpart = ADDRESS_USER; break;
	case DETAIL: addrpart = ADDRESS_DETAIL; break;
	}
	for (sl = t->u.ae.sl; sl != NULL && !res; sl = sl->next) {
	    int l;
	    const char **body;

	    /* use getheader for address, getenvelope for envelope */
	    if (((t->type == ADDRESS) ? 
		   call_getheader(i, m, sl->s, &body) :
		   call_getenvelope(i, m, sl->s, &body)) != SIEVE_OK) {
		continue; /* try next header */
	    }
	    for (pl = t->u.ae.pl; pl != NULL && !res; pl = pl->next) {
		for (l = 0; body[l] != NULL && !res; l++) {
		    /* loop through each header */
		    struct address *data = NULL;
		    struct addr_marker *marker = NULL;
		    char *val;

		    parse_address(body[l], &data, &marker);
                    val = get_address(addrpart, &data, &marker, 0);
		    while (val != NULL && !res) { 
			/* loop through each address */
			res |= t->u.ae.comp(pl->p, val);
			val = get_address(addrpart, &data, &marker, 0);
       		    }
		    free_address(&data, &marker);
		}
	    }
	}
	break;
    case ANYOF:
	res = 0;
	for (tl = t->u.tl; tl != NULL && !res; tl = tl->next) {
	    res |= evaltest(i, tl->t, m);
	}
	break;
    case ALLOF:
	res = 1;
	for (tl = t->u.tl; tl != NULL && res; tl = tl->next) {
	    res &= evaltest(i, tl->t, m);
	}
	break;
    case EXISTS:
	res = 1;
	for (sl = t->u.sl; sl != NULL && res; sl = sl->next) {
	    const char **headbody = NULL;
	    res &= (call_getheader(i, m, sl->s, &headbody) == SIEVE_OK);
	}
	break;
    case SFALSE:
	res = 0;
	break;
    case STRUE:
	res = 1;
	break;
    case HEADER:
	res = 0;
	for (sl = t->u.h.sl; sl != NULL && !res; sl = sl->next) {
	    const char **val;
	    size_t l;
	    if (call_getheader(i, m, sl->s, &val) != SIEVE_OK)
		continue;
	    for (pl = t->u.h.pl; pl != NULL && !res; pl = pl->next) {
		for (l = 0; val[l] != NULL && !res; l++) {
		    res |= t->u.h.comp(pl->p, val[l]);
		}
	    }
	}
	break;
    case NOT:
	res = !evaltest(i, t->u.t, m);
	break;
    case SIZE:
    {
	int sz;

	if (call_getsize(i, m, &sz) != SIEVE_OK)
	    break;

	if (t->u.sz.t == OVER) {
	    res = (sz > t->u.sz.n);
	} else { /* UNDER */
	    res = (sz < t->u.sz.n);
	}
	break;
    }
    }

    return res;
}

/* If i is a version 1 interpreter, call the getheader callback
 * otherwise find the header information in the message struct */
/* call to this function used to be...
 * if (i->getheader(m, sl->s, &val) != SIEVE_OK) */
int call_getheader(sieve_interp_t *i, void *m, const char *s, const char ***val)
{
    if(i->getheader != NULL) {
        i->getheader(m, s, val);
    } else {
        message2_getheader(m, s, val);
    }
    if(*val == NULL)
        return SIEVE_DONE;
    return SIEVE_OK;
}

/* If i is a version 1 interpreter, call the getsize callback
 * otherwise find the size information in the message struct */
/* calls to this function used to be...
 * if (i->getsize(m, &sz) != SIEVE_OK) */
int call_getsize(sieve_interp_t *i, void *m, int *sz)
{
    if(i->getsize != NULL) {
        i->getsize(m, sz);
    } else {
        message2_getsize(m, sz);
    }
    if(sz < 0)
        return SIEVE_DONE;
    return SIEVE_OK;
}

/* If i is a version 1 interpreter, call the getenvelope callback
 * otherwise find the envelope information in the message struct */
/* calls to this function used to be...
 * if (i->getenvelope(m, f, &c) != SIEVE_OK) */
int call_getenvelope(sieve_interp_t *i, void *m, const char *f, const char ***c)
{
    if(i->getenvelope != NULL) {
        i->getenvelope(m, f, c);
    } else {
        message2_getenvelope(m, f, c);
    }
    if(*c == NULL)
        return SIEVE_DONE;
    return SIEVE_OK;
}

/* evaluate the script c.  returns negative if error was encountered,
   0 if it exited off the end, or positive if a stop action was encountered.

   note that this is very stack hungry; we just evaluate the AST in
   the naivest way.  if we implement some sort of depth limit, we'll
   be ok here; otherwise we'd want to transform it a little smarter */
int eval(sieve_interp_t *i, commandlist_t *c, 
		void *m, action_list_t *actions,
		const char **errmsg)
{
    int res = 0;
    stringlist_t *sl;

    while (c != NULL) {
	switch (c->type) {
	case IF:
	    if (evaltest(i, c->u.i.t, m))
		res = eval(i, c->u.i.do_then, m, actions, errmsg);
	    else
		res = eval(i, c->u.i.do_else, m, actions, errmsg);
	    break;
	case REJCT:
	    res = do_reject(actions, c->u.str);
	    if (res == SIEVE_RUN_ERROR)
		*errmsg = "Reject can not be used with any other action";
	    break;
	case FILEINTO:
	    res = do_fileinto(actions, c->u.str, &i->curflags);
	    if (res == SIEVE_RUN_ERROR)
		*errmsg = "Fileinto can not be used with Reject";
	    break;
	case REDIRECT:
	    res = do_redirect(actions, c->u.str);
	    if (res == SIEVE_RUN_ERROR)
		*errmsg = "Redirect can not be used with Reject";
	    break;
	case KEEP:
	    res = do_keep(actions, &i->curflags);
	    if (res == SIEVE_RUN_ERROR)
		*errmsg = "Keep can not be used with Reject";
	    break;
	case VACATION:
	    {
		const char **body;
		char buf[128], *fromaddr;
		char *found = NULL;
		char *myaddr = NULL;
		char *reply_to = NULL;
		int l = SIEVE_OK;
		struct address *data = NULL;
		struct addr_marker *marker = NULL;
		char *tmp;

		/* is there an Auto-Submitted keyword other than "no"? */
		strcpy(buf, "auto-submitted");
		if (call_getheader(i, m, buf, &body) == SIEVE_OK) {
		    /* we don't deal with comments, etc. here */
		    /* skip leading white-space */
		    while (*body[0] && isspace((int) *body[0])) body[0]++;
		    if (strcasecmp(body[0], "no")) l = SIEVE_DONE;
		}

		/* is there a Precedence keyword of "junk | bulk | list"? */
		strcpy(buf, "precedence");
		if (call_getheader(i, m, buf, &body) == SIEVE_OK) {
		    /* we don't deal with comments, etc. here */
		    /* skip leading white-space */
		    while (*body[0] && isspace((int) *body[0])) body[0]++;
		    if (!strcasecmp(body[0], "junk") ||
			!strcasecmp(body[0], "bulk") ||
			!strcasecmp(body[0], "list"))
			l = SIEVE_DONE;
		}

		/* Note: the domain-part of all addresses are canonicalized */

		/* grab my address from the envelope */
		if (l == SIEVE_OK) {
		    strcpy(buf, "to");
		    l = call_getenvelope(i, m, buf, &body);
		    if (body[0]) {
			parse_address(body[0], &data, &marker);
			tmp = get_address(ADDRESS_ALL, &data, &marker, 1);
			myaddr = (tmp != NULL) ? sv_strdup(tmp, strlen(tmp)) : NULL;
			free_address(&data, &marker);
		    }
		}
		if (l == SIEVE_OK) {
		    strcpy(buf, "from");
		    l = call_getenvelope(i, m, buf, &body);
		}
		if (l == SIEVE_OK && body[0]) {
		    /* we have to parse this address & decide whether we
		       want to respond to it */
		    parse_address(body[0], &data, &marker);
		    tmp = get_address(ADDRESS_ALL, &data, &marker, 1);
		    reply_to = (tmp != NULL) ? sv_strdup(tmp, strlen(tmp)) : NULL;
		    free_address(&data, &marker);

		    /* first, is there a reply-to address? */
		    if (reply_to == NULL) {
			l = SIEVE_DONE;
		    }

		    /* first, is it from me? */
		    if (l == SIEVE_OK && !strcmp(myaddr, reply_to)) {
			l = SIEVE_DONE;
		    }

		    /* ok, is it any of the other addresses i've
		       specified? */
		    if (l == SIEVE_OK)
			for (sl = c->u.v.addresses; sl != NULL; sl = sl->next)
			    if (!strcmp(sl->s, reply_to))
				l = SIEVE_DONE;
		
		    /* ok, is it a system address? */
		    if (l == SIEVE_OK && sysaddr(reply_to)) {
			l = SIEVE_DONE;
		    }
		}

		if (l == SIEVE_OK) {
		    /* ok, we're willing to respond to the sender.
		       but is this message to me?  that is, is my address
		       in the TO, CC or BCC fields? */
		    if (strcpy(buf, "to"), 
			call_getheader(i, m, buf, &body) == SIEVE_OK)
			found = look_for_me(myaddr, c->u.v.addresses, body);

		    if (!found && (strcpy(buf, "cc"),
				   (call_getheader(i, m, buf, &body) == SIEVE_OK)))
			found = look_for_me(myaddr, c->u.v.addresses, body);

		    if (!found && (strcpy(buf, "bcc"),
				   (call_getheader(i, m, buf, &body) == SIEVE_OK)))
			found = look_for_me(myaddr, c->u.v.addresses, body);

		    if (!found)
			l = SIEVE_DONE;
		}

		if (l == SIEVE_OK) {
		    /* ok, ok, if we got here maybe we should reply */
		
		    if (c->u.v.subject == NULL) {
			/* we have to generate a subject */
			const char **s;
		    
			strcpy(buf, "subject");
			if (call_getheader(i, m, buf, &s) != SIEVE_OK ||
			    s[0] == NULL) {
			    strcpy(buf, "Automated reply");
			} else {
			    /* s[0] contains the original subject */
			    const char *origsubj = s[0];
			    
			    while (!strncasecmp(origsubj, "Re: ", 4)) {
				origsubj += 4;
			    }
			    snprintf(buf, sizeof(buf), "Re: %s", origsubj);
			}
		    } else {
			/* user specified subject */
			strncpy(buf, c->u.v.subject, sizeof(buf)-1);
			buf[sizeof(buf)-1] = '\0';
		    }

		    /* who do we want the message coming from? */
		    fromaddr = found;
		
		    res = do_vacation(actions, reply_to,
				      sv_strdup(fromaddr, strlen(fromaddr)),
				      sv_strdup(buf, strlen(buf)),
				      c->u.v.message, c->u.v.days, c->u.v.mime);
		
		     if (res == SIEVE_RUN_ERROR)
			 *errmsg = "Vacation can not be used with Reject or Vacation";

		} else {
                    sv_free(reply_to);
		    if (l != SIEVE_DONE) res = -1; /* something went wrong */
		}
		sv_free(myaddr);
		break;
	    }
	case STOP:
	    res = 1;
	    break;
	case DISCARD:
	    res = do_discard(actions);
	    break;
	case SETFLAG:
	    sl = c->u.sl;
	    res = do_setflag(actions, sl->s);
	    for (sl = sl->next; res == 0 && sl != NULL; sl = sl->next) {
		res = do_addflag(actions, sl->s);
	    }
	    if (res == SIEVE_RUN_ERROR)
		*errmsg = "Setflag can not be used with Reject";
	    break;
	case ADDFLAG:
	    for (sl = c->u.sl; res == 0 && sl != NULL; sl = sl->next) {
		res = do_addflag(actions, sl->s);
	    }
	    if (res == SIEVE_RUN_ERROR)
		*errmsg = "Addflag can not be used with Reject";
	    break;
	case REMOVEFLAG:
	    for (sl = c->u.sl; res == 0 && sl != NULL; sl = sl->next) {
		res = do_removeflag(actions, sl->s);
	    }
	    if (res == SIEVE_RUN_ERROR)
		*errmsg = "Removeflag can not be used with Reject";
	    break;
	case MARK:
	    res = do_mark(actions);
	    if (res == SIEVE_RUN_ERROR)
		*errmsg = "Mark can not be used with Reject";
	    break;
	case UNMARK:
	    res = do_unmark(actions);
	    if (res == SIEVE_RUN_ERROR)
		*errmsg = "Unmark can not be used with Reject";
	    break;
	case NOTIFY:
	    res = do_notify(actions, c->u.n.id, c->u.n.method,
			    c->u.n.options, c->u.n.priority, c->u.n.message);
			    
	    break;
	case DENOTIFY:
	    res = do_denotify(actions, c->u.d.comp, c->u.d.pattern,
			      c->u.d.priority);
	    break;

	}

	if (res) /* we've either encountered an error or a stop */
	    break;

	/* execute next command */
	c = c->next;
    }

    return res;
}

#define GROW_AMOUNT 100

static void add_header(sieve_interp_t *i, int isenv, char *header, 
		       void *message_context, char **out, 
		       size_t *outlen, size_t *outalloc)
{
    const char **h;
    int addlen;
    /* get header value */
    if (isenv)
	call_getenvelope(i, message_context, header, &h);	
    else
	call_getheader(i, message_context, header, &h);	

    if (!h || !h[0])
	return;

    addlen = strlen(h[0]) + 1;

    /* realloc if necessary */
    if ( (*outlen) + addlen >= *outalloc)
    {
	*outalloc = (*outlen) + addlen + GROW_AMOUNT;
	*out = sv_realloc(*out, *outalloc);
    }

    /* add header value */
    strcat(*out,h[0]);

    *outlen += addlen;
}

static int fillin_headers(sieve_interp_t *i, char *msg, 
			  void *message_context, char **out, size_t *outlen)
{
    size_t allocsize = GROW_AMOUNT;
    char *c;
    size_t n;

    *out = sv_malloc(GROW_AMOUNT);
    *outlen = 0;
    (*out)[0]='\0';

    if (msg == NULL) return SIEVE_OK;

    /* construct the message */
    c = msg;
    while (*c) {
	/* expand variables */
	if (!strncasecmp(c, "$from$", 6)) {
	    add_header(i, 0 ,"From", message_context, out, outlen, &allocsize);
	    c += 6;
	}
	else if (!strncasecmp(c, "$env-from$", 10)) {
	    add_header(i, 1, "From", message_context, out, outlen, &allocsize);
	    c += 10;
	}
	else if (!strncasecmp(c, "$subject$", 9)) {
	    add_header(i, 0, "Subject", message_context, out, outlen, &allocsize);
	    c += 9;
	}
	/* XXX need to do $text$ variables */
	else {
	    /* find length of plaintext up to next potential variable */
	    n = strcspn(c+1, "$") + 1; /* skip opening '$' */
	    /* realloc if necessary */
	    if ( (*outlen) + n+1 >= allocsize) {
		allocsize = (*outlen) + n+1 + GROW_AMOUNT;
		*out = sv_realloc(*out, allocsize);
	    }
	    /* copy the plaintext */
	    strncat(*out, c, n);
	    (*out)[*outlen+n]='\0';
	    (*outlen) += n;
	    c += n;
	}
    }

    return SIEVE_OK;
}

static int sieve_addflag(sieve_imapflags_t *imapflags, char *flag)
{
    int n;
 
    /* search for flag already in list */
    for (n = 0; n < imapflags->nflags; n++) {
	if (strcmp(imapflags->flag[n], flag) == 0)
	    break;
    }
 
    /* add flag to list, iff not in list */
    if (n == imapflags->nflags) {
	imapflags->nflags++;
	imapflags->flag =
	    (char **) sv_realloc((char *)imapflags->flag,
			       imapflags->nflags*sizeof(char *));
	imapflags->flag[imapflags->nflags-1] = sv_strdup(flag, strlen(flag));
    }
 
    return SIEVE_OK;
}

static int sieve_removeflag(sieve_imapflags_t *imapflags, char *flag)
{
    int n;
 
    /* search for flag already in list */
    for (n = 0; n < imapflags->nflags; n++) {
	if (!strcmp(imapflags->flag[n], flag))
	    break;
    }
 
    /* remove flag from list, iff in list */
    if (n < imapflags->nflags) {
	sv_free(imapflags->flag[n]);
	imapflags->nflags--;
 
	for (; n < imapflags->nflags; n++)
	    imapflags->flag[n] = imapflags->flag[n+1];
 
	imapflags->flag =
	    (char **) sv_realloc((char *)imapflags->flag,
			       imapflags->nflags*sizeof(char *));
    }
 
    return SIEVE_OK;
}

static int send_notify_callback(sieve_script_t *s, void *message_context, 
				notify_list_t *notify, char *actions_string,
				const char **errmsg)
{
    sieve_notify_context_t nc;
    char *out_msg;
    size_t out_msglen;
    int ret;

    assert(notify->isactive);

    nc.method = notify->method;
    nc.options = notify->options;
    nc.priority = notify->priority;

    fillin_headers(&(s->interp), notify->message, message_context, 
		   &out_msg, &out_msglen);

    nc.message = sv_malloc(out_msglen + strlen(actions_string) + 30);

    strcpy(nc.message, out_msg);
    strcat(nc.message, "\n\n");
    sv_free(out_msg);

    strcat(nc.message,actions_string);

    ret = s->interp.notify(&nc,
			   s->interp.interp_context,
			   s->script_context,
			   message_context,
			   errmsg);    

    /* Not to worry, free_notify_list() does this for us.
    if (nc.options) {
	char **opts = nc.options;
	while (opts && *opts) {
	    sv_free(*opts);
	    opts++;
	}
	sv_free(nc.options);
    }
    */
    sv_free(nc.message);

    return ret;
}

static char *action_to_string(action_t action)
{
    switch(action)
	{
	case ACTION_REJECT: return "Reject";
	case ACTION_FILEINTO: return "Fileinto";
	case ACTION_KEEP: return "Keep";
	case ACTION_REDIRECT: return "Redirect";
	case ACTION_DISCARD: return "Discard";
	case ACTION_VACATION: return "Vacation";
	case ACTION_SETFLAG: return "Setflag";
	case ACTION_ADDFLAG: return "Addflag";
	case ACTION_REMOVEFLAG: return "Removeflag";
	case ACTION_MARK: return "Mark";
	case ACTION_UNMARK: return "Unmark";
	case ACTION_NOTIFY: return "Notify";
	case ACTION_DENOTIFY: return "Denotify";
	default: return "Unknown";
	}

    return "Error!";
}

static char *sieve_errstr(int code)
{
    switch (code)
	{
	case SIEVE_FAIL: return "Generic Error";
	case SIEVE_NOT_FINALIZED: return "Sieve not finalized";
	case SIEVE_PARSE_ERROR: return "Parse error";
	case SIEVE_RUN_ERROR: return "Run error";
	case SIEVE_INTERNAL_ERROR: return "Internal Error";
	case SIEVE_NOMEM: return "No memory";
	default: return "Unknown error";
	}

    return "Error!";
}

#define HASHSIZE 16

static int makehash(unsigned char hash[HASHSIZE], char *s1, char *s2)
{
    MD5_CTX ctx;

    MD5Init(&ctx);
    MD5Update(&ctx, s1, strlen(s1));
    MD5Update(&ctx, s2, strlen(s2));
    MD5Final(hash, &ctx);

    return SIEVE_OK;
}

/* execute a script on a message, producing side effects via callbacks.
   it is the responsibility of the caller to save a message if this
   returns anything but SIEVE_OK. */
int sieve_execute_script(sieve_script_t *s, void *message_context)
{
    int ret = 0;
    int implicit_keep = 0;
    action_list_t *actions = NULL, *a;
    action_t lastaction = ACTION_NULL;
    char actions_string[4096] = "";
    const char *errmsg = NULL;

    actions = new_action_list();
    if (actions == NULL) {
	ret = SIEVE_NOMEM;
	goto error;
    }
 
    if (eval(&s->interp, s->cmds, message_context, actions, &errmsg) < 0)
	return SIEVE_RUN_ERROR;
  
    strcpy(actions_string,"Action(s) taken:\n");
  
    /* now perform actions attached to m */
    a = actions;
    implicit_keep = 1;
    while (a != NULL) {
	lastaction = a->a;
	errmsg = NULL;
 
	switch (a->a) {
	case ACTION_REJECT:
	    implicit_keep = 0;
	    if (!s->interp.reject)
		return SIEVE_INTERNAL_ERROR;
	    ret = s->interp.reject(a->u,
				   s->interp.interp_context,
				   s->script_context,
				   message_context,
				   &errmsg);
	    
	    if (ret == SIEVE_OK)
		snprintf(actions_string+strlen(actions_string),
			 sizeof(actions_string)-strlen(actions_string), 
			 "Rejected with: %s\n", ((sieve_reject_context_t *)(a->u))->msg);
	    sv_free((sieve_reject_context_t *)(a->u));
	    break;

	case ACTION_FILEINTO:
	    implicit_keep = 0;
	    if (!s->interp.fileinto)
		return SIEVE_INTERNAL_ERROR;
	    ret = s->interp.fileinto(a->u,
				     s->interp.interp_context,
				     s->script_context,
				     message_context,
				     &errmsg);

	    if (ret == SIEVE_OK)
		snprintf(actions_string+strlen(actions_string),
			 sizeof(actions_string)-strlen(actions_string),
			 "Filed into: %s\n",((sieve_fileinto_context_t *)(a->u))->mailbox);
	    sv_free((sieve_fileinto_context_t *)(a->u));
	    break;

	case ACTION_KEEP:
	    implicit_keep = 0;
	    if (!s->interp.keep)
		return SIEVE_INTERNAL_ERROR;
	    ret = s->interp.keep(a->u,
				 s->interp.interp_context,
				 s->script_context,
				 message_context,
				 &errmsg);
	    if (ret == SIEVE_OK)
		snprintf(actions_string+strlen(actions_string),
			 sizeof(actions_string)-strlen(actions_string),
			 "Kept\n");
	    break;

	case ACTION_REDIRECT:
	    implicit_keep = 0;
	    if (!s->interp.redirect)
		return SIEVE_INTERNAL_ERROR;
	    ret = s->interp.redirect(a->u,
				     s->interp.interp_context,
				     s->script_context,
				     message_context,
				     &errmsg);
	    if (ret == SIEVE_OK)
		snprintf(actions_string+strlen(actions_string),
			 sizeof(actions_string)-strlen(actions_string),
			 "Redirected to %s\n", ((sieve_redirect_context_t *)(a->u))->addr);
	    sv_free((sieve_redirect_context_t *)(a->u));
	    break;

	case ACTION_DISCARD:
	    implicit_keep = 0;
	    if (s->interp.discard) /* discard is optional */
		ret = s->interp.discard(NULL, s->interp.interp_context,
					s->script_context,
					message_context,
					&errmsg);
	    if (ret == SIEVE_OK)
		snprintf(actions_string+strlen(actions_string),
			 sizeof(actions_string)-strlen(actions_string),
			 "Discarded\n");
	    break;

	case ACTION_VACATION:
	    {
		unsigned char hash[HASHSIZE];

		if (!s->interp.vacation)
		    return SIEVE_INTERNAL_ERROR;

		/* first, let's figure out if we should respond to this */
		ret = makehash(hash, ((sieve2_vacation_context_t *)(a->u))->send.addr,
			       ((sieve2_vacation_context_t *)(a->u))->send.msg);
		if (ret == SIEVE_OK) {
		    ((sieve2_vacation_context_t *)(a->u))->check.hash = hash;
		    ((sieve2_vacation_context_t *)(a->u))->check.len = HASHSIZE;
		    ret = s->interp.vacation->autorespond(&((sieve2_vacation_context_t *)(a->u))->check,
							  s->interp.interp_context,
							  s->script_context,
							  message_context,
							  &errmsg);
		}
		if (ret == SIEVE_OK) {
		    /* send the response */
		    ret = s->interp.vacation->send_response(&((sieve2_vacation_context_t *)(a->u))->send,
							    s->interp.interp_context,
							    s->script_context, 
							    message_context,
							    &errmsg);

		    if (ret == SIEVE_OK)
			snprintf(actions_string+strlen(actions_string),
				 sizeof(actions_string)-strlen(actions_string),
				 "Sent vacation reply\n");

		} else if (ret == SIEVE_DONE) {
		    snprintf(actions_string+strlen(actions_string),
			     sizeof(actions_string)-strlen(actions_string),
			     "Vacation reply suppressed\n");

		    ret = SIEVE_OK;
		}
	    
		break;
	    }

 
	case ACTION_SETFLAG:
	    free_imapflags(&s->interp.curflags);
	    ret = sieve_addflag(&s->interp.curflags, ((sieve_imaponeflag_t *)(a->u))->flag);
	    break;
	case ACTION_ADDFLAG:
	    ret = sieve_addflag(&s->interp.curflags, ((sieve_imaponeflag_t *)(a->u))->flag);
	    break;
	case ACTION_REMOVEFLAG:
	    ret = sieve_removeflag(&s->interp.curflags, ((sieve_imaponeflag_t *)(a->u))->flag);
	    break;
	case ACTION_MARK:
	    {
		int n = s->interp.markflags->nflags;

		ret = SIEVE_OK;
		while (n && ret == SIEVE_OK) {
		    ret = sieve_addflag(&s->interp.curflags,
					s->interp.markflags->flag[--n]);
		}
		break;
	    }
	case ACTION_UNMARK:
	    {
		int n = s->interp.markflags->nflags;

		ret = SIEVE_OK;
		while (n && ret == SIEVE_OK) {
		    ret = sieve_removeflag(&s->interp.curflags,
					   s->interp.markflags->flag[--n]);
		}
		break;
	    }
        case ACTION_NOTIFY:
            {
                /* Process notify actions */
                if (s->support.notify) {
                    notify_list_t *n_top = (notify_list_t *)(a->u);
                    notify_list_t *n = n_top;
                    int notify_ret = SIEVE_OK;
      
                    while (n != NULL) {
                        if (n->isactive) {
                    	lastaction = ACTION_NOTIFY;
                    	notify_ret = send_notify_callback(s, message_context, n,
                    					  actions_string, &errmsg);
                    	ret |= notify_ret;
                        }
                        n = n->next;
                    }
      
                    free_notify_list(n_top);
		    sv_free(n_top);     /* The very top isn't free()ed above.*/
                    n_top = NULL;       /* Don't try any notifications again */
      
                    if (notify_ret != SIEVE_OK) {
                        goto error;	/* Process the notify error */
                    }
                }
            }
 
	case ACTION_NONE:
	    break;

	default:
	    ret = SIEVE_INTERNAL_ERROR;
	    break;
	}

	a = a->next;

	if (ret != SIEVE_OK) {
	    /* uh oh! better bail! */
	    break;
	}
    }

 error: /* report run-time errors */
 
    if (ret != SIEVE_OK) {
	if (lastaction == ACTION_NULL) /* we never executed an action */
	    snprintf(actions_string+strlen(actions_string),
		     sizeof(actions_string)-strlen(actions_string),
		     "script execution failed: %s\n",
		     errmsg ? errmsg : sieve_errstr(ret));
	else
	    snprintf(actions_string+strlen(actions_string),
		     sizeof(actions_string)-strlen(actions_string),
		     "%s action failed: %s\n",
		     action_to_string(lastaction),
		     errmsg ? errmsg : sieve_errstr(ret));
    }
 
    if ((ret != SIEVE_OK) && s->interp.err) {
	char buf[1024];
	if (lastaction == -1) /* we never executed an action */
            snprintf(buf, sizeof(buf), "%s",
                     errmsg ? errmsg : sieve_errstr(ret));
        else
	    snprintf(buf, sizeof(buf), "%s: %s", action_to_string(lastaction),
                     errmsg ? errmsg : sieve_errstr(ret));
 
	ret |= s->interp.execute_err(buf, s->interp.interp_context,
				     s->script_context, message_context);
    }

    if (implicit_keep) {
	sieve_keep_context_t keep_context;
	int keep_ret;

	implicit_keep = 0;	/* don't try an implicit keep again */

	keep_context.imapflags = &s->interp.curflags;
 
	lastaction = ACTION_KEEP;
	keep_ret = s->interp.keep(&keep_context, s->interp.interp_context,
			     s->script_context, message_context, &errmsg);
	ret |= keep_ret;
        if (keep_ret == SIEVE_OK)
            snprintf(actions_string+strlen(actions_string),
		     sizeof(actions_string)-strlen(actions_string),
		     "Kept\n");
	else {
	    goto error;		/* process the implicit keep error */
	}
    }

    if (actions)
	free_action_list(actions);
  
    return ret;
}