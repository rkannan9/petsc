/* $Id: petscfe.cpp,v 1.1 2001/03/06 23:58:18 buschelm Exp balay $ */
#include <iostream>
#include "petscfe.h"

using namespace std;

int main(int argc,char *argv[]) {
  PETScFE::tool *Tool;
  if (argc>2) {
    PETScFE::tool::Create(Tool,argv[1]);
    if (Tool) {
      Tool->GetArgs(argc,argv);
      Tool->Parse();
      Tool->Execute();
      
      Tool->Destroy();
    }
  } else {
    cout << endl << "PETSc WIN32 Front End v1.0" << endl << endl;
    cout << "Usage: win32fe <tool> --<win32fe options> -<tool options> <files>" << endl;
    cout << "<tool> must follow win32fe.  Order of options and files is unimportant." << endl << endl;
    cout << "<tool>: {cl,df,bcc32,lib,tlib}" << endl;
    cout << "cl:    Microsoft 32-bit C/C++ Optimizing Compiler" << endl;
    cout << "df:    Compaq Visual Fortran Optimizing Compiler" << endl;
    cout << "bcc32: Borland C++ for Win32" << endl;
    cout << "lib:   Microsoft Library Manager" << endl;
    cout << "tlib:  Borland Library Manager" << endl << endl;
    cout << "<win32fe options>: {use,quiet}" << endl;
    cout << "--use:   Specifies the variant of <tool> to use" << endl;
    cout << "--quiet: Do not echo to stdout the translated commandline" << endl << endl;
    cout << "Ex: win32fe cl -Zi -c foo.c --quiet" << endl;
  }
  return(0);
}
