#include <stdio.h>
#include <stdint.h>

#include "tclln.h"


int custom_command (ClientData client_data, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    int        activation = 0;
    int        activate   = 3;
    int        deactivate = 2;
    double     value      = 10;
    const char *name      = "";

    Tcl_ArgvInfo arg_table [] = {
        {TCL_ARGV_CONSTANT,  "-activate",   (void*) (intptr_t) activate,    (void*) &activation, "activate something",   NULL},
        {TCL_ARGV_CONSTANT,  "-deactivate", (void*) (intptr_t) deactivate,  (void*) &activation, "deactivate something", NULL},
        {TCL_ARGV_FLOAT,     "-value",      (void*) NULL,                   (void*) &value,      "add the value",        NULL},
        {TCL_ARGV_STRING,    "-name",       (void*) NULL,                   (void*) &name,       "give the name",        NULL},
        TCL_ARGV_AUTO_HELP,
        TCL_ARGV_TABLE_END
    };

    int result = Tcl_ParseArgsObjv (interp, arg_table, &objc, objv, NULL);
    if (result != TCL_OK) return result;

    printf ("my custom command\n");

    if (activation != 0) {
        printf (" - activated: %d\n", (activation == activate ? 1 : 0));
    }
    printf (" - value: %lf\n", value);
    printf (" - name: %s\n", name);

    return TCL_OK;
}

int main (int argc, const char *argv[])
{
    TclLN tclln;

    tclln = tclln_new (argv[0]);

    tclln_provide_completion_command (tclln, NULL);
    tclln_add_command (tclln, "mycommand", (const char * const []) {"-activate", "-deactivate", "-value", "-name", "-help", NULL}, custom_command, NULL, NULL);
    tclln_set_prompt  (tclln, "tcllnsh> ",
                               "       : ");

    if (argc > 1) {
        if (argc > 2) {
            printf ("Too many arguments - expected 0 or 1\n");
            return 1;
        }

        tclln_run_file (tclln, argv[1], true);
    }

    tclln_run (tclln);

    tclln_free (tclln);

    return 0;
}
