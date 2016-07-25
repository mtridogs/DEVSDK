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
/*
 * Sample buffering tool
 * 
 * This tool collects an address trace of instructions that access memory
 * by filling a buffer.  When the buffer overflows,the callback writes all
 * of the collected records to a file.
 *	此程序跟踪收集（填入缓冲区）访问内存的指令地址。
	当缓冲区溢出发生时，这个回调函数将所有收集到的记录写入一个文件中。
 */

#include <iostream>
#include <fstream>
#include <stdlib.h>

#include "pin.H"
#include "portability.H"
using namespace std;

/*
 * Name of the output file	输出文件名
 */
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "buffer.out", "output file");


/*
 * Control of writing to the output file
 *	
 */
KNOB<BOOL> KnobDoWriteToOutputFile(KNOB_MODE_WRITEONCE, "pintool", "emit", "1", "control output to file");

/*
 * The ID of the buffer	缓冲区ID
 */
BUFFER_ID bufId;

/*
 * The lock for I/O.	锁 I/O
 */
PIN_LOCK fileLock;

/*
 * There is an isolation bug in the Pin windows support that prevents
 * the pin tool from opening files ina callback routine.  If a tool
 * does this, deadlock occurs.  Instead, open one file in main, and
 * write the thread id along with the data.
 *	在windows下有一个已隔离的Bug：pin工具会阻止在一个回调程序中打开文件，
	如果一个工具试图尝试这么做，就会形成死锁。
	相反可行的是，在主函数中打开此文件，并且将进程ID同数据一并写入文件。
 */
ofstream ofile;

/*
 * Number of OS pages for the buffer	缓冲区 OS 页数
 */
#define NUM_BUF_PAGES 1024


/*
 * Record of memory references.  Rather than having two separate
 * buffers for reads and writes, we just use one struct that includes a
 * flag for type.
	记录内存的引用。不是对单独两块缓冲区进行读写，
	我们只是用一个结构，表示包含了类型。
 */
struct MEMREF
{
    THREADID    tid;
    ADDRINT     pc;
    ADDRINT     ea;
    UINT32      size;
    UINT32      read;
};



/**************************************************************************
 *
 *  Instrumentation routines	检测程序
 *
 **************************************************************************/

/*
 * Insert code to write data to a thread-specific buffer for instructions
 * that access memory.
	在访问内存的代码中，将代码插入数据写入指定线程缓冲区。
 */	
VOID Trace(TRACE trace, VOID *v)
{
    for(BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl=BBL_Next(bbl))
    {
        for(INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins=INS_Next(ins))
        {
            UINT32 memoryOperands = INS_MemoryOperandCount(ins);

            for (UINT32 memOp = 0; memOp < memoryOperands; memOp++)
            {
                UINT32 refSize = INS_MemoryOperandSize(ins, memOp);
                
                // Note that if the operand is both read and written we log it once
                // for each.
				//	如果操作是读和写，我们对每次操作进行记录。
                if (INS_MemoryOperandIsRead(ins, memOp))
                {
                    INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                         IARG_INST_PTR, offsetof(struct MEMREF, pc),
                                         IARG_MEMORYOP_EA, memOp, offsetof(struct MEMREF, ea),
                                         IARG_UINT32, refSize, offsetof(struct MEMREF, size), 
                                         IARG_BOOL, TRUE, offsetof(struct MEMREF, read),
                                         IARG_END);
                }

                if (INS_MemoryOperandIsWritten(ins, memOp))
                {
                    INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                         IARG_INST_PTR, offsetof(struct MEMREF, pc),
                                         IARG_MEMORYOP_EA, memOp, offsetof(struct MEMREF, ea),
                                         IARG_UINT32, refSize, offsetof(struct MEMREF, size), 
                                         IARG_BOOL, FALSE, offsetof(struct MEMREF, read),
                                         IARG_END);
                }
            }
        }
    }
}


/**************************************************************************
 *
 *  Callback Routines	回调程序
 *
 **************************************************************************/

/*!
 * Called when a buffer fills up, or the thread exits, so we can process it or pass it off
 * as we see fit.
	在缓冲区被填满或线程退出时，我们可以根据需要处理（调试？）或者跳过它
 * @param[in] id		buffer handle				缓冲区句柄
 * @param[in] tid		id of owning thread			线程ID
 * @param[in] ctxt		application context			应用程序上下文
 * @param[in] buf		actual pointer to buffer	缓冲区的真实指针
 * @param[in] numElements	number of records		循环计数
 * @param[in] v			callback value				回调值
 * @return  A pointer to the buffer to resume filling.
	返回值	向缓冲区重新填入指针。
 */
VOID * BufferFull(BUFFER_ID id, THREADID tid, const CONTEXT *ctxt, VOID *buf,
                  UINT64 numElements, VOID *v)
{
    /*
    This code will work - but it is very slow, so for testing purposes we run with the Knob turned off
    这段代码能够运行，但极慢，所以为了测试方便我们在运行时添加了停止按钮。
	*/
    if (KnobDoWriteToOutputFile)
    {
        PIN_GetLock(&fileLock, 1);

        struct MEMREF * reference=(struct MEMREF*)buf;

        for(UINT64 i=0; i<numElements; i++, reference++)
        {
            if (reference->ea != 0)
                ofile << tid << "   "  << reference->pc << "   " << reference->ea << endl;
        }
        PIN_ReleaseLock(&fileLock);
    }

    return buf;
}


// This function is called when the application exits
//	在应用程序退出时调用这个函数。
VOID Fini(INT32 code, VOID *v)
{
    PIN_GetLock(&fileLock, 1);
    ofile.close();
    PIN_ReleaseLock(&fileLock);
}

/* ===================================================================== */
/* Print Help Message     显示帮助信息                                   */
/* ===================================================================== */

INT32 Usage()
{
    cerr << "This tool demonstrates the basic use of the buffering API." << endl ;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */
/*!
 * The main procedure of the tool.	这个工具的主要部分
 * This function is called when the application image is loaded but not yet started.
	在应用程序控件被加载但未开始运行时调用这个函数。
 * @param[in]   argc            total number of elements in the argv array
								数组参数中元素的总数。
 * @param[in]   argv            array of command line arguments, 
								命令行对象数组
 *                              including pin -t <toolname> -- ...
 */
int main(int argc, char *argv[])
{
    // Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid
	//	初始化pin库，输入-h	显示帮助信息否则命令行无效。
    if( PIN_Init(argc,argv) )
    {
        return Usage();
    }
    
    // Initialize the memory reference buffer;
    // set up the callback to process the buffer.
    //	初始化内存引用缓冲区，为进程缓冲区设置回调。
    bufId = PIN_DefineTraceBuffer(sizeof(struct MEMREF), NUM_BUF_PAGES,
                                  BufferFull, 0);

    if(bufId == BUFFER_ID_INVALID)
    {
        cerr << "Error: could not allocate initial buffer" << endl;
        return 1;
    }

    // Initialize the lock.
	//	初始化lock
    PIN_InitLock(&fileLock);

    // Open the output file.
	//	打开输出文件
    string filename = KnobOutputFile.Value();
    ofile.open(filename.c_str());
    if ( ! ofile )
    {
        cerr << "Error: could not open output file." << endl;
        exit(1);
    }
    ofile << hex;
    
    // Add an instrumentation function
	//	添加一个检测函数
    TRACE_AddInstrumentFunction(Trace, 0);

    // Register Fini to be called when the application exits
	//	在应用程序退出时调用FINI
    PIN_AddFiniFunction(Fini, 0);
    
   // Start the program, never returns
	//	启动程序，无返回值。
    PIN_StartProgram();
    
    return 0;
}


