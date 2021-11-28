/*
Copyright 2021 The Foedag team

GPL License

Copyright (c) 2021 The Open-Source FPGA Foundation

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "TclInterpreter.h"

using namespace FOEDAG;

#include <tcl.h>

TclInterpreter::TclInterpreter(const char *argv0) : interp(nullptr) {
  static bool initLib;
  if (!initLib) {
    Tcl_FindExecutable(argv0);
    initLib = true;
  }
  interp = Tcl_CreateInterp();
  if (!interp) throw new std::runtime_error("failed to initialise Tcl library");
}

TclInterpreter::~TclInterpreter() {
  if (interp) Tcl_DeleteInterp(interp);
}

std::string TclInterpreter::evalFile(const std::string &filename) {
  int code = Tcl_EvalFile(interp, filename.c_str());

  if (code >= TCL_ERROR) {
    return std::string("Tcl Error: " +
                       std::string(Tcl_GetStringResult(interp)));
  }
  return std::string(Tcl_GetStringResult(interp));
}

std::string TclInterpreter::evalCmd(const std::string cmd) {
  int code = Tcl_Eval(interp, cmd.c_str());

  if (code >= TCL_ERROR) {
    return std::string("Tcl Error: " +
                       std::string(Tcl_GetStringResult(interp)));
  }
  return std::string(Tcl_GetStringResult(interp));
}

void TclInterpreter::registerCmd(const std::string &cmdName, Tcl_CmdProc proc,
                                 ClientData clientData,
                                 Tcl_CmdDeleteProc *deleteProc) {
  Tcl_CreateCommand(interp, cmdName.c_str(), proc, clientData, deleteProc);
}

std::string TclInterpreter::evalGuiTestFile(const std::string &filename) {
  std::string testHarness = R"(
  proc test_harness { gui_script } {
    global CONT
    set fid [open $gui_script]
    set content [read $fid]
    close $fid
    set errorInfo ""

    catch {
        
        # Schedule commands
        set lines [split $content "\n"]
        set time 500
        foreach line $lines {
            if {[regexp {^#} $line]} {
                continue
            }
            if {$line == ""} {
                continue
            }
            after  $time $line 
            
            set time [expr $time + 500]
        }
    }
    
    # Schedule GUI exit
    set time [expr $time + 500]
    after $time "puts \"GUI EXIT\" ; flush stdout; set CONT 0"
    
    # Enter loop
    set CONT 1 
    while {$CONT} {
        set a 0
        after 100 set a 1
        vwait a
    }
    
    if {$errorInfo != ""} {
        puts $errorInfo
        exit 1
    }
    
    puts "Tcl Exit" ; flush stdout
    tcl_exit
  }

  )";

  std::string call_test = "proc call_test { } {\n";
  call_test += "test_harness " + filename + "\n";
  call_test += "}\n";

  std::string completeScript = testHarness + "\n" + call_test;

  int code = Tcl_Eval(interp, completeScript.c_str());

  if (code >= TCL_ERROR) {
    return std::string("Tcl Error: " +
                       std::string(Tcl_GetStringResult(interp)));
  }
  return std::string(Tcl_GetStringResult(interp));
}