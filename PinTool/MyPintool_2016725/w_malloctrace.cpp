/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2016 Intel Corporation. All rights reserved.
 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
/* ===================================================================== */
/* This example demonstrates finding a function by name on Windows.      */
//	本例通过名称在windows中查找函数。
/* ===================================================================== */

#include "pin.H"
namespace WINDOWS
{
#include<Windows.h>
}
#include <iostream>
#include <fstream>

/* ===================================================================== */
/* Global Variables 全局变量*/
/* ===================================================================== */

std::ofstream TraceFile;

/* ===================================================================== */
/* Commandline Switches 命令行开关*/
/* ===================================================================== */

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "w_malloctrace.out", "specify trace file name");

/* ===================================================================== */
/* Print Help Message  显示帮助信息                                      */
/* ===================================================================== */

INT32 Usage()
{
    cerr << "This tool produces a trace of calls to RtlAllocateHeap.";
    cerr << endl << endl;
    cerr << KNOB_BASE::StringKnobSummary();
    cerr << endl;
    return -1;
}

/* ===================================================================== */
/* Analysis routines 分析程序                                            */
/* ===================================================================== */
 
VOID Before(CHAR * name, WINDOWS::HANDLE hHeap,
            WINDOWS::DWORD dwFlags, WINDOWS::DWORD dwBytes) 
{
    TraceFile << "Before: " << name << "(" << hex << hHeap << ", "
              << dwFlags << ", " << dwBytes << ")" << dec << endl;
}

VOID After(CHAR * name, ADDRINT ret)
{
    TraceFile << "After: " << name << "  returns " << hex
              << ret << dec << endl;
}


/* ===================================================================== */
/* Instrumentation routines     检测程序                                 */
/* ===================================================================== */
   
VOID Image(IMG img, VOID *v)
{
    // Walk through the symbols in the symbol table.
    //	遍历符号表中的符号
    for (SYM sym = IMG_RegsymHead(img); SYM_Valid(sym); sym = SYM_Next(sym))
    {
        string undFuncName = PIN_UndecorateSymbolName(SYM_Name(sym), UNDECORATION_NAME_ONLY);

        //  Find the RtlAllocHeap() function.
		//	查找RtlAllocHeap()函数
        if (undFuncName == "RtlAllocateHeap")
        {
            RTN allocRtn = RTN_FindByAddress(IMG_LowAddress(img) + SYM_Value(sym));
            
            if (RTN_Valid(allocRtn))
            {
                // Instrument to print the input argument value and the return value.
				//	检测并显示输入参数值和返回值。
                RTN_Open(allocRtn);
                
                RTN_InsertCall(allocRtn, IPOINT_BEFORE, (AFUNPTR)Before,
                               IARG_ADDRINT, "RtlAllocateHeap",
                               IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                               IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
                               IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
                               IARG_END);
                RTN_InsertCall(allocRtn, IPOINT_AFTER, (AFUNPTR)After,
                               IARG_ADDRINT, "RtlAllocateHeap",
                               IARG_FUNCRET_EXITPOINT_VALUE,
                               IARG_END);
                
                RTN_Close(allocRtn);
            }
        }
    }
}

/* ===================================================================== */

VOID Fini(INT32 code, VOID *v)
{
    TraceFile.close();
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char *argv[])
{
    // Initialize pin & symbol manager
	//	初始化pin和符号管理器
    PIN_InitSymbols();
    if( PIN_Init(argc,argv) )
    {
        return Usage();
    }
    
    // Write to a file since cout and cerr maybe closed by the application
	//	从应用程序可能关闭处将标准和错误输出写入一个文件。
    TraceFile.open(KnobOutputFile.Value().c_str());
    TraceFile << hex;
    TraceFile.setf(ios::showbase);
    
    // Register Image to be called to instrument functions.
	//	检测函数调用（记录）控件？
    IMG_AddInstrumentFunction(Image, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Never returns	无返回值
    PIN_StartProgram();
    
    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
