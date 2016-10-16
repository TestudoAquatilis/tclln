/*
 *    tclln is a library for integrating a tcl-shell with custom commands
 *    Copyright (C) 2016  Andreas Dixius
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

#ifndef __TCLLN_H__
#define __TCLLN_H__

#include <tcl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tclln_data *TclLN;

/* initialize and returns TclLN data
 *   prog_name: name of the binary (e.g. = argv[0])
 * returns: true on success
 */
TclLN tclln_new (const char *prog_name);

/* free resources of TclLN
 *   tclln: pointer to struct whiosl resources should be freed
 */
void tclln_free (TclLN tclln);

/* run shell
 *   tclln: TclLN data
 * returns: true on success
 */
bool tclln_run (TclLN tclln);

/* run file
 *   tclln: TclLN data
 *   filename: file of tcl-script to execute
 *   verbose: print commands and return values?
 * returns: true on success
 */
bool tclln_run_file (TclLN tclln, const char *script_name, bool verbose);

/* add custom tcl command
 *   tclln: pointer to initialized TclLN data where the command should be added
 *   command_name: name of command in tcl shell
 *   arg_complete_list: null terminated array of arguments which should be added to auto-completion for given command - NULL for no auto-completion of arguments
 *   command_proc: pointer to function which handles calls to command
 *   client_data: client data handled to function
 *   delete_proc: delete proc for command (see tcl library)
 */
Tcl_Command tclln_add_command (TclLN tclln, const char *command_name, const char * const arg_complete_list[],
                Tcl_ObjCmdProc *command_proc, ClientData client_data, Tcl_CmdDeleteProc *delete_proc);

/* set prompt string
 *   tclln: TclLN data
 *   prompt_main: normal prompt to show
 *   prompt_multiline: prompt to show on successive lines when commands span multiple lines
 */
void tclln_set_prompt  (TclLN tclln, const char *prompt_main,  const char *prompt_multiline);

/* provide a tcl-command that can add completion information in a tcl script
 *   tclln: TclLN data
 *   command_name: name of command in tcl or NULL for default: "tclln::add_completion"
 */
void tclln_provide_completion_command (TclLN tclln, const char *command_name);

#ifdef __cplusplus
}
#endif

#endif
