/*
 *    tclln is a library for integrating a tcl-shell with custom commands
 *    Copyright (C) 2015  Andreas Dixius
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Lesser General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "tclln.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#include <glib.h>

#include "linenoise.h"


/*************************************************
 * data struct
 *************************************************/

struct tclln_data {
    /* tcl */
    Tcl_Interp *tcl_interp;

    /* prompt */
    const char *prompt_string_main;
    const char *prompt_string_multiline;
    bool       multiline;

    /* completion */
    GString      *completion_begin;

    GList        *completion_list;
    GStringChunk *completion_strings;

    GTree        *completion_arg_table;
    GStringChunk *completion_arg_strings;

    /* exit */
    int          return_code;
    bool         exit_tcl;
};


/*************************************************
 * non-header header
 *************************************************/

static gboolean free_glist_value_in_tree (gpointer key, gpointer value, gpointer data);

static const char *prompt (struct tclln_data *tclln);

static void completion (const char *input_buffer, linenoiseCompletions *linenoise_completion);
static void completion_table_add_command (struct tclln_data *tclln, const char *command, const char *const arg_complete_list[]);
static void completion_table_add_defaults (struct tclln_data *tclln);
static void completion_generate (struct tclln_data *tclln);
static void completion_add_tcl_result (Tcl_Interp *interp, GStringChunk *gs_chunk, GList **res_list);
static void completion_generate_tcl_procs (Tcl_Interp *interp, GStringChunk *gs_chunk, GList **res_list, const char *base);
static void completion_generate_tcl_vars  (Tcl_Interp *interp, GStringChunk *gs_chunk, GList **res_list, const char *base);
static void completion_generate_args (GTree *arg_table, GStringChunk *gs_chunk, GList **res_list, const char *command, const char *base);
static int tcl_completion_add_command (ClientData client_data, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

static int exit_command (ClientData client_data, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

/*************************************************
 * constants
 *************************************************/

static const int default_history_size       = 100;

static const char *default_prompt_main      = "> ";
static const char *default_prompt_multiline = ": ";

/*************************************************
 * global data
 *************************************************/

static struct tclln_data *tclln_completion;

/*************************************************
 * init / free
 *************************************************/

struct tclln_data * tclln_new (const char *prog_name)
{
    struct tclln_data *tclln = (struct tclln_data *) malloc (sizeof (struct tclln_data));
    if (tclln == NULL) return NULL;

    /* data */
    tclln->multiline               = false;
    tclln->prompt_string_main      = default_prompt_main;
    tclln->prompt_string_multiline = default_prompt_multiline;

    tclln->tcl_interp             = NULL;
    tclln->completion_begin       = NULL;
    tclln->completion_list        = NULL;
    tclln->completion_strings     = NULL;
    tclln->completion_arg_strings = NULL;
    tclln->completion_arg_table   = NULL;

    tclln->return_code = 0;
    tclln->exit_tcl    = false;

    /* tcl interpreter */
    tclln->tcl_interp = Tcl_CreateInterp();

    if (tclln->tcl_interp == NULL) goto tclln_init_error;
    Tcl_Preserve (tclln->tcl_interp);

    /* set utf-8 as system encoding */
    if (Tcl_Eval (tclln->tcl_interp, "encoding system utf-8\n") != TCL_OK) {
        fprintf(stderr, "Error: could not set system encoding to utf-8\n");
    }

    /* init script */
    if (Tcl_Init (tclln->tcl_interp) == TCL_ERROR) {
        fprintf(stderr, "Error: could not source init script\n");
    }

    /* completion */
    tclln->completion_begin   = g_string_new (NULL);
    tclln->completion_strings = g_string_chunk_new (32);

    if (tclln->completion_begin == NULL) {
        goto tclln_init_error;
    }
    if (tclln->completion_strings == NULL) goto tclln_init_error;

    tclln->completion_arg_strings = g_string_chunk_new (32);
    tclln->completion_arg_table   = g_tree_new ((GCompareFunc) strcmp);

    if (tclln->completion_arg_strings == NULL) goto tclln_init_error;
    if (tclln->completion_arg_table   == NULL) goto tclln_init_error;

    completion_table_add_defaults (tclln);

    /* exit */
    Tcl_CreateObjCommand (tclln->tcl_interp, "exit", exit_command, (ClientData) tclln, NULL);

    /* success */
    return tclln;

    /* error */
tclln_init_error:
    tclln_free (tclln);

    fprintf (stderr, "Error: initializing tclln failed\n");

    return NULL;
}

void tclln_free (struct tclln_data *tclln)
{
    if (tclln == NULL) return;

    if (tclln->tcl_interp != NULL) {
        Tcl_DeleteInterp (tclln->tcl_interp);
        Tcl_Release (tclln->tcl_interp);
        tclln->tcl_interp = NULL;
    }
    if (tclln->completion_begin != NULL) {
        g_string_free (tclln->completion_begin, true);
    }
    if (tclln->completion_list != NULL) {
        g_list_free (tclln->completion_list);
    }
    if (tclln->completion_strings != NULL) {
        g_string_chunk_free (tclln->completion_strings);
    }
    if (tclln->completion_arg_table != NULL) {
        g_tree_foreach (tclln->completion_arg_table, free_glist_value_in_tree, NULL);
        g_tree_destroy (tclln->completion_arg_table);
    }
    if (tclln->completion_arg_strings != NULL) {
        g_string_chunk_free (tclln->completion_arg_strings);
    }

    free (tclln);
}

static gboolean free_glist_value_in_tree (gpointer key, gpointer value, gpointer data)
{
    GList *list = (GList *) value;

    if (list != NULL) {
        g_list_free (list);
    }

    return false;
}

/*************************************************
 * run
 *************************************************/

bool tclln_run (struct tclln_data *tclln)
{
    /* for multi-line inputs */
    GString *gs_input = g_string_new (NULL);

    /* prepare linenoise for interaction with this */
    tclln_completion = tclln;
    linenoiseSetCompletionCallback (completion);
    linenoiseSetMultiLine (1);
    linenoiseHistorySetMaxLen (default_history_size);

    while (true) {
        if (tclln->exit_tcl) {
            break;
        }

        char *line = linenoise (prompt (tclln));

        if (line == NULL) {
            if (errno == EAGAIN) {
                continue;
            }
            break;
        }

        if (strlen (line) > 0) {
            linenoiseHistoryAdd (line);
        }

        /* multiline? */
        if (tclln->multiline) {
            gs_input = g_string_append (gs_input, "\n");
            gs_input = g_string_append (gs_input, line);

            free (line);
            line     = gs_input->str;
        }

        bool brace_match = (Tcl_CommandComplete (line) == 1 ? true : false);

        if (!brace_match) {
            if (!tclln->multiline) {
                gs_input = g_string_append (gs_input, line);
                free (line);
                tclln->multiline = true;
            }

            continue;
        }

        int tcl_res = Tcl_Eval (tclln->tcl_interp, line);

        const char *result_string = Tcl_GetString (Tcl_GetObjResult (tclln->tcl_interp));

        if (strlen (result_string) > 0) {
            fprintf ((tcl_res == TCL_OK ? stdout : stderr), "%s\n", result_string);
        }

        /* end of multiline */
        if (tclln->multiline) {
            tclln->multiline = false;
            gs_input = g_string_assign (gs_input, "");
        } else {
            free (line);
        }
    }

    g_string_free (gs_input, true);

    return true;
}


#define FILE_BUF_LEN 1024
bool tclln_run_file (struct tclln_data *tclln, const char *script_name, bool verbose)
{
    /* check for filename */
    if (script_name == NULL) {
        fprintf (stderr, "Error: no filename specified\n");
        return false;
    }

    /* try to open file */
    FILE *infile = fopen (script_name, "r");
    if (infile == NULL) {
        fprintf (stderr, "Error: failed to open file %s\n", script_name);
        return false;
    }

    /* for multi-line inputs */
    GString *gs_input = g_string_new (NULL);

    char buf [FILE_BUF_LEN];

    while (true) {
        if (tclln->exit_tcl) {
            break;
        }

        /* try reading */
        char *line = fgets (buf, FILE_BUF_LEN, infile);

        if (line == NULL) {
            /* EOF and nothing left to execute? -> finish */
            if (gs_input->len == 0) break;
        } else {
            gs_input = g_string_append (gs_input, line);

            /* line not yet finished? -> wait for more input */
            if (gs_input->len == 0) continue;

            char end = gs_input->str[gs_input->len - 1];
            if ((end != '\n') && (end != '\r')) continue;
        }

        bool brace_match = (Tcl_CommandComplete (gs_input->str) == 1 ? true : false);
        if (!brace_match) {
            /* no complete tcl command? wait for more input */
            continue;
        }

        /* verbose? print command: */
        if (verbose) {
            fprintf (stdout, "%s", gs_input->str);
        }

        /* enough input for script execution: */
        int tcl_res = Tcl_Eval (tclln->tcl_interp, gs_input->str);

        if (verbose || (tcl_res != TCL_OK)) {
            const char *result_string = Tcl_GetString (Tcl_GetObjResult (tclln->tcl_interp));

            if (strlen (result_string) > 0) {
                fprintf (stdout, "%s\n", result_string);
            }

            if (tcl_res != TCL_OK) {
                break;
            }
        }

        /* next input */
        g_string_assign (gs_input, "");
    }

    fclose (infile);
    g_string_free (gs_input, true);

    return true;
}

/*************************************************
 * custom commands
 *************************************************/

Tcl_Command tclln_add_command (
                struct tclln_data *tclln, const char *command_name, const char * const arg_complete_list[],
                Tcl_ObjCmdProc *command_proc, ClientData client_data, Tcl_CmdDeleteProc *delete_proc)
{
    Tcl_Command result = Tcl_CreateObjCommand (tclln->tcl_interp, command_name, command_proc, client_data, delete_proc);

    if (arg_complete_list != NULL) {
        completion_table_add_command (tclln, command_name, arg_complete_list);
    }

    return result;
}

/*************************************************
 * prompt string
 *************************************************/

void tclln_set_prompt (struct tclln_data *tclln, const char *prompt_main, const char *prompt_multiline)
{
    if (prompt_main == NULL) {
        tclln->prompt_string_main = default_prompt_main;
    } else {
        tclln->prompt_string_main = prompt_main;
    }

    if (prompt_multiline == NULL) {
        tclln->prompt_string_multiline = default_prompt_multiline;
    } else {
        tclln->prompt_string_multiline = prompt_multiline;
    }
}

static const char *prompt (struct tclln_data *tclln) {
    if (tclln == NULL) {
        return default_prompt_main;
    } else {
        if (tclln->multiline) {
            return tclln->prompt_string_multiline;
        } else {
            return tclln->prompt_string_main;
        }
    }
}


/*************************************************
 * completion
 *************************************************/

void tclln_provide_completion_command (struct tclln_data *tclln, const char *command_name)
{
    if (tclln == NULL) return;

    if (command_name == NULL) command_name = "tclln::add_completion";

    Tcl_CreateObjCommand (tclln->tcl_interp, command_name, tcl_completion_add_command, (ClientData) tclln, NULL);
}

static int tcl_completion_add_command (ClientData client_data, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    if (objc < 3) {
        Tcl_SetObjResult (interp, Tcl_NewStringObj ("wrong number of arguments: expeted at least command name and 1 possible argument", -1));
        return TCL_ERROR;
    }

    const char *command_name = Tcl_GetString (objv[1]);

    struct tclln_data *tclln = (struct tclln_data*) client_data;
    if (tclln == NULL) {
        return TCL_ERROR;
    }

    const char **arg_list = (const char **) malloc (sizeof (const char *) * (objc-1));
    if (arg_list == NULL) {
        return TCL_ERROR;
    }

    for (int i = 0; i < objc-2; i++) {
        arg_list[i] = Tcl_GetString (objv[2+i]);
    }
    arg_list[objc-2] = NULL;

    completion_table_add_command (tclln, command_name, arg_list);

    free (arg_list);

    Tcl_SetObjResult (interp, Tcl_NewBooleanObj (true));

    return TCL_OK;
}

static void completion_table_add_command (struct tclln_data *tclln, const char *command, const char *const arg_complete_list[])
{
    GList *arg_list = NULL;

    for (int i = 0; arg_complete_list[i] != NULL; i++) {
        arg_list = g_list_prepend (arg_list, g_string_chunk_insert_const (tclln->completion_arg_strings, arg_complete_list[i]));
    }

    arg_list = g_list_sort (arg_list, (GCompareFunc) strcmp);

    char *key = g_string_chunk_insert_const (tclln->completion_arg_strings, command);
    GList *old_list = (GList *) g_tree_lookup (tclln->completion_arg_table, key);

    g_tree_insert (tclln->completion_arg_table, key, arg_list);

    if (old_list != NULL) {
        g_list_free (old_list);
    }
}

static void completion_table_add_defaults (struct tclln_data *tclln)
{
    completion_table_add_command (tclln, "after",
        (const char * const []) {"cancel", "idle", "info", NULL});

    completion_table_add_command (tclln, "array",
        (const char * const []) {"anymore", "donesearch", "exists", "get", "names", "nextelement", "set", "size",
            "startsearch", "statistics", "unset", NULL});

    completion_table_add_command (tclln, "binary",
        (const char * const []) {"decode", "encode", "format", "scan", "base64", "hex", "uuencode", "-maxlen",
            "-wrapchar", "-strict", NULL});

    completion_table_add_command (tclln, "chan",
        (const char * const []) {"blocked", "close", "configure", "copy", "create", "current", "end", "eof", "event",
            "flush", "gets", "names", "pending", "pipe", "pop", "postevent", "push", "puts", "read", "seek", "start",
            "tell", "truncate", "-blocking", "-buffering", "-buffersize", "-encoding", "-eofchar", "-eofchar", "-nonewline",
            "-translation", NULL});

    completion_table_add_command (tclln, "chan",
        (const char * const []) {"add", "clicks", "format", "microseconds", "milliseconds", "scan", "seconds", "-base",
        "-format", "-gmt", "-locale", "-timezone", NULL});

    completion_table_add_command (tclln, "dict",
        (const char * const []) {"append", "create", "exists", "filter", "key", "script", "value", "for", "get", "incr",
            "info", "keys", "lappend", "map", "merge", "remove", "replace", "set", "size", "unset", "update", "values",
            "with", NULL});

    completion_table_add_command (tclln, "encoding",
        (const char * const []) {"convertfrom", "convertto", "dirs", "names", "system", NULL});

    completion_table_add_command (tclln, "fconfigure",
        (const char * const []) {"-blocking", "-buffering", "-buffersize", "-encoding", "-eofchar", "-eofchar", "-translation",
        "-translation", NULL});

    completion_table_add_command (tclln, "fcopy",
        (const char * const []) {"-size", "-command", NULL});

    completion_table_add_command (tclln, "file",
        (const char * const []) {"atime", "attributes", "channels", "copy", "-force", "dirname", "executable", "exists",
            "extension", "isdirectory", "isfile", "join", "link", "lstat", "mkdir", "mtime", "nativename", "normalize",
            "owned", "pathtype", "readable", "readlink", "rename", "rootname", "separator", "size", "split", "stat",
            "system", "tail", "tempfile", "type", "volumes", "writable", NULL});

    completion_table_add_command (tclln, "fileevent",
        (const char * const []) {"readable", "writeable", NULL});

    completion_table_add_command (tclln, "glob",
        (const char * const []) {"-directory", "-join", "-nocomplain", "-path", "-tails", "-types", NULL});

    completion_table_add_command (tclln, "history",
        (const char * const []) {"add", "change", "clear", "event", "info", "keep", "nextid", "redo", NULL});

    completion_table_add_command (tclln, "info",
        (const char * const []) {"args", "body", "class", "cmdcount", "commands", "complete", "coroutine", "default",
            "errorstack", "exists", "frame", "function", "globals", "hostname", "level", "library", "loaded",
            "locals", "nameofexecutable", "object", "patchlevel", "procs", "script", "sharedlibextension",
            "tclversion", "vars", NULL});

    completion_table_add_command (tclln, "interp",
        (const char * const []) { "alias", "aliases", "bgerror", "cancel", "create", "debug", "delete", "eval",
            "exists", "expose", "hide", "hidden", "invokehidden", "issafe", "limit", "marktrusted", "recursionlimit",
            "share", "slaves", "target", "transfer", NULL});

    completion_table_add_command (tclln, "load",
        (const char * const []) {"-global", "-lazy", NULL});

    completion_table_add_command (tclln, "lsearch",
        (const char * const []) {"-exact", "-glob", "-regexp", "-sorted", "-all", "-inline", "-not", "-start",
            "-ascii", "-dictionary", "-integer", "-nocase", "-real", "-decreasing", "-increasing", "-bisect",
            "-index", "-subindices", NULL});

    completion_table_add_command (tclln, "lsort",
        (const char * const []) {"-ascii", "-dictionary", "-integer", "-real", "-command", "-increasing", "-decreasing",
            "-indices", "-index", "-stride", "-nocase", "-unique", NULL});

    completion_table_add_command (tclln, "namespace",
        (const char * const []) {"children", "code", "current", "delete", "ensemble", "eval", "exists", "export", "-clear",
            "forget", "import", "-force", "inscope", "origin", "parent", "path", "qualifiers", "tail", "upvar", "unknown",
            "which", "-command", "-variable", NULL});

    completion_table_add_command (tclln, "package",
        (const char * const []) {"forget", "ifneeded", "names", "present", "provide", "require", "unknown", "vcompare",
            "versions", "vsatisfies", "prefer", NULL});

    completion_table_add_command (tclln, "puts",
        (const char * const []) {"-nonewline",  NULL});

    completion_table_add_command (tclln, "read",
        (const char * const []) {"-nonewline",  NULL});

    completion_table_add_command (tclln, "regexp",
        (const char * const []) {"-about", "-expanded", "-indices", "-line", "-linestop", "-lineanchor", "-nocase",
            "-all", "-inline", "-start", NULL});

    completion_table_add_command (tclln, "regsub",
        (const char * const []) {"-all", "-expanded", "-line", "-linestop", "-lineanchor", "-nocase", "-start", NULL});

    completion_table_add_command (tclln, "return",
        (const char * const []) {"ok", "error", "return", "break", "continue", "-code", "-errorcode", "-errorinfo",
            "-errorstack", "-level", "-options", NULL});

    completion_table_add_command (tclln, "seek",
        (const char * const []) {"start", "current", "end", NULL});

    completion_table_add_command (tclln, "self",
        (const char * const []) {"call", "caller", "class", "filter", "method", "namespace", "next", "object",
            "target", NULL});

    completion_table_add_command (tclln, "socket",
        (const char * const []) {"-async", "-connecting", "-error", "-myaddr", "-myport", "-peername", "-server",
            "-sockname", NULL});

    completion_table_add_command (tclln, "source",
        (const char * const []) {"-encoding", NULL});

    completion_table_add_command (tclln, "string",
        (const char * const []) { "-failindex", "-length", "-nocase", "-strict", "alnum", "alpha", "ascii", "boolean",
            "cat", "compare", "control", "digit", "double", "entier", "equal", "false", "first", "graph", "index",
            "integer", "is", "last", "length", "list", "lower", "map", "match", "print", "punct", "range", "repeat",
            "replace", "reverse", "space", "tolower", "totitle", "toupper", "trim", "trimleft", "trimright", "true",
            "upper", "wideinteger", "wordchar", "xdigit", NULL});

    completion_table_add_command (tclln, "subst",
        (const char * const []) {"-nobackslashes", "-nocommands", "-novariables", NULL});

    completion_table_add_command (tclln, "switch",
        (const char * const []) {"-exact", "-glob", "-regexp", "-nocase", "-matchvar", "-indexvar", NULL});

    completion_table_add_command (tclln, "trace",
        (const char * const []) {"add", "array", "command", "delete", "enter", "enterstep", "execution", "info",
            "leave", "leavestep", "read", "remove", "rename", "unset", "variable", "vdelete", "vinfo", "write", NULL});

    completion_table_add_command (tclln, "unload",
        (const char * const []) {"-nocomplain", "-keeplibrary", NULL});

    completion_table_add_command (tclln, "unset",
        (const char * const []) {"-nocomplain", NULL});

    completion_table_add_command (tclln, "update",
        (const char * const []) {"idletasks", NULL});
}


static void completion (const char *input_buffer, linenoiseCompletions *linenoise_completion)
{
    struct tclln_data *tclln = tclln_completion;

    if (tclln == NULL) return;

    /* empty line? */
    if (strcmp (input_buffer, "") == 0) return;

    tclln->completion_begin = g_string_assign (tclln->completion_begin, input_buffer);

    /* generate completion data */
    completion_generate (tclln);
    tclln->completion_list = g_list_sort (tclln->completion_list, (GCompareFunc) strcmp);

    if (tclln->completion_list == NULL) {
        return;
    }

    /* fill completion */
    GList *completion_list = tclln->completion_list;

    GString *temp_str = g_string_new (NULL);

    while (completion_list != NULL) {
        g_string_assign (temp_str, tclln->completion_begin->str);
        g_string_append (temp_str, completion_list->data);
        linenoiseAddCompletion (linenoise_completion, temp_str->str);
        completion_list = completion_list->next;
    }

    g_string_free (temp_str, true);

    return;
}

static void completion_generate (struct tclln_data *tclln)
{
    /* init */
    if (tclln->completion_list != NULL) {
        g_list_free (tclln->completion_list);
        tclln->completion_list = NULL;
    }

    g_string_chunk_clear (tclln->completion_strings);

    /* find out what to complete */
    int pos_cmd = 0;
    int len_cmd = 0;
    int pos_start = strlen (tclln->completion_begin->str);

    char *str_cmd;
    char *str_base;

    const char *in_buf = tclln->completion_begin->str;

    /* find beginning of relevant section */
    int brace_count = 0;
    int pos_tmp = tclln->completion_begin->len - 1;
    while (pos_tmp >= 0) {
        char current = in_buf[pos_tmp];
        if (current == ']') brace_count++;
        if (current == '}') brace_count++;
        if (current == '[') brace_count--;
        if (current == '{') brace_count--;
        if (brace_count < 0) break;
        pos_tmp --;
    }
    pos_cmd = pos_tmp + 1;

    while (isspace (in_buf[pos_cmd]) && (in_buf[pos_cmd] != '\0')) pos_cmd++;
    while ((!isspace (in_buf[pos_cmd + len_cmd])) && (in_buf[pos_cmd + len_cmd] != '\0')) len_cmd++;
    str_cmd = g_string_chunk_insert_len (tclln->completion_strings, &(in_buf[pos_cmd]), len_cmd);

    while ((!isspace (in_buf[pos_start])) && (pos_start > pos_cmd)) pos_start--;
    if (pos_start > pos_cmd) pos_start++;
    str_base = g_string_chunk_insert (tclln->completion_strings, &(in_buf[pos_start]));

    if (pos_start == pos_cmd) {
        /* nothing ? */
        if (len_cmd == 0) return;

        /* command or var */
        if (*str_cmd != '$') {
            /* complete procs/commands */
            completion_generate_tcl_procs (tclln->tcl_interp, tclln->completion_strings, &(tclln->completion_list), str_cmd);
            g_string_truncate (tclln->completion_begin, pos_cmd);
            return;
        }
    }

    /* variable or argument */
    if (*str_base == '$') {
        /* variable */
        str_base++;
        pos_start++;

        /* nothing? */
        if (*str_base == '\0') return;

        completion_generate_tcl_vars (tclln->tcl_interp, tclln->completion_strings, &(tclln->completion_list), str_base);
        g_string_truncate (tclln->completion_begin, pos_start);
        return;
    }

    /* argument: */
    completion_generate_args (tclln->completion_arg_table, tclln->completion_strings, &(tclln->completion_list), str_cmd, str_base);
    g_string_truncate (tclln->completion_begin, pos_start);

    return;
}

static void completion_add_tcl_result (Tcl_Interp *interp, GStringChunk *gs_chunk, GList **res_list)
{
    Tcl_Obj *list = Tcl_GetObjResult (interp);

    int len;

    if (Tcl_ListObjLength (interp, list, &len) != TCL_OK) return;

    if (len <= 0) return;

    for (int i = 0; i < len; i++) {
        Tcl_Obj *elem;

        if (Tcl_ListObjIndex (interp, list, i, &elem) != TCL_OK) continue;

        char * elem_str = g_string_chunk_insert (gs_chunk, Tcl_GetString (elem));

        *res_list = g_list_prepend (*res_list, elem_str);
    }
}

static void completion_generate_tcl_vars (Tcl_Interp *interp, GStringChunk *gs_chunk, GList **res_list, const char *base)
{
    /* vars */
    GString *tcl_command = g_string_new (NULL);
    if (tcl_command == NULL) return;

    g_string_printf (tcl_command, "info vars %s*\n", base);

    if (Tcl_Eval (interp, tcl_command->str) == TCL_OK) {
        /* handle result */
        completion_add_tcl_result (interp, gs_chunk, res_list);
    }

    /* finish */
    g_string_free (tcl_command, true);
}

static void completion_generate_tcl_procs (Tcl_Interp *interp, GStringChunk *gs_chunk, GList **res_list, const char *base)
{
    /* commands */
    GString *tcl_command = g_string_new (NULL);
    if (tcl_command == NULL) return;

    g_string_printf (tcl_command, "info commands %s*\n", base);

    if (Tcl_Eval (interp, tcl_command->str) == TCL_OK) {
        /* handle result */
        completion_add_tcl_result (interp, gs_chunk, res_list);
    }

    /* procs */
    g_string_printf (tcl_command, "info procs %s*\n", base);

    if (Tcl_Eval (interp, tcl_command->str) == TCL_OK) {
        /* handle result */
        completion_add_tcl_result (interp, gs_chunk, res_list);
    }

    /* finish */
    g_string_free (tcl_command, true);
}

static void completion_generate_args (GTree *arg_table, GStringChunk *gs_chunk, GList **res_list, const char *command, const char *base)
{
    if (command == NULL) return;

    /* remove "::" at beginning before lookup */
    if (strlen (command) > 2) {
        if ((command[0] == ':') && (command[1] == ':')) {
            command = command+2;
        }
    }

    GList *arg_list = (GList *) g_tree_lookup (arg_table, command);

    if (arg_list == NULL) return;

    GList *candidates = *res_list;

    int len = strlen (base);
    if (len <= 0) return;

    for (GList *i_elem = arg_list; i_elem != NULL; i_elem = i_elem->next) {
        if (strncmp (i_elem->data, base, len) != 0) continue;

        candidates = g_list_prepend (candidates, i_elem->data);
    }

    *res_list = candidates;
}


/*************************************************
 * exit
 *************************************************/

static int exit_command (ClientData client_data, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    int return_code = 0;

    if (objc > 2) {
        Tcl_SetObjResult (interp, Tcl_NewStringObj ("wrong # args: should be \"exit ?returnCode?\"", -1));
        return TCL_ERROR;
    }

    if (objc == 2) {
        if (Tcl_GetIntFromObj (interp, objv[1], &return_code) != TCL_OK) {
            GString *msg = g_string_new (NULL);

            g_string_printf (msg, "expected integer but got \"%s\"", Tcl_GetString (objv[1]));
            Tcl_SetObjResult (interp, Tcl_NewStringObj (msg->str, -1));

            g_string_free (msg, true);
            return TCL_ERROR;
        }
    }

    struct tclln_data *tclln = (struct tclln_data *) client_data;

    tclln->exit_tcl    = true;
    tclln->return_code = return_code;

    return TCL_OK;
}


