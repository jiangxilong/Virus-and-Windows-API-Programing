#include <windows.h>
#include <stdio.h>
DWORD dwSizeOfCode;
PBYTE pCode, pOrignCode;

__declspec(naked) void code_start()
{
    __asm {
// ---------------------------------------------------------
// type : DWORD GetBaseKernel32()
				//call _GetBaseKernel32 //
_GetBaseKernel32:
				//pop ebx
        push    ebp
        mov     ebp, esp
        push    esi
        push    edi
        xor     ecx, ecx                    // ECX = 0
        mov     esi, fs:[0x30]              // ESI = &(PEB) ([FS:0x30])
        mov     esi, [esi + 0x0c]           // ESI = PEB->Ldr (LDR_DATA)
        mov     esi, [esi + 0x1c]           // ESI = PEB->Ldr.InInitOrder
_next_module:
        mov     eax, [esi + 0x08]           // EBP = InInitOrder[X].base_address
        mov     edi, [esi + 0x20]           // EBP = InInitOrder[X].module_name (unicode) 这个地址指向的位置存放着一个unicode编码的字符串，字符串内容就是dll的文件名
        mov     esi, [esi]                  // ESI = InInitOrder[X].flink (next module) 
        cmp     [edi + 12*2], cx            // modulename[12] == 0 ? 一个unicode的长度为2个字节，所以*2，然后kernel32.dll有12*2个字节长，那么第25个字节必为0，是字符串的\0 且后面填充了8个AB，也就是说字符串长度多了就是25的位置就是字符，少了就是AB 反正不是\0

        jne     _next_module                 // No: try next module.
        pop     edi
        pop     esi
        mov     esp, ebp
        pop     ebp
      // ret
      	pop 	ebx//将原进程的下一条指令地址eip存入ebx，相当于去掉jmp指令的ret
      	push  eax
      	push 	ebx
        
        
        // type : DWORD GetGetProcAddrBase(DWORD base)
_GetGetProcAddrBase:
        push    ebp
        mov     ebp, esp
        push    edx
        push    ebx
        push    edi
        push    esi
        mov     ebx, [ebp+8] //此处加8应该是参数，而加4是返回地址
        //mov     ebx, [ebp+4]
        mov     eax, [ebx + 0x3c] // edi = BaseAddr, eax = pNtHeader(RVA)
        mov     edx, [ebx + eax + 0x78]
        add     edx, ebx          // edx = Export Table (RVA)+BaseAddr
        mov     ecx, [edx + 0x18] // ecx = NumberOfNames
        mov     edi, [edx + 0x20] //
        add     edi, ebx          // ebx = AddressOfNames
_search:
        dec     ecx
        mov     esi, [edi + ecx*4]//ecx--,从后往前检索name的表，第一个属性就是name字符串的RVA
        add     esi, ebx
        mov     eax, 0x50746547 // "PteG" 先看前四个字节是不是GetP
        cmp     [esi], eax
        jne     _search //不是的话继续查找下一个name单元
        mov     eax, 0x41636f72 //"Acor" 是的话再看后四个单元是不是rocA
        cmp     [esi+4], eax
        jne     _search //不是的话继续查找下一个name单元
        mov     edi, [edx + 0x24] //
        add     edi, ebx      // edi = AddressOfNameOrdinals
        mov     cx, word ptr [edi + ecx*2]  // ecx = GetProcAddress-orinal
        mov     edi, [edx + 0x1c] //
        add     edi, ebx      // edi = AddressOfFunction
        mov     eax, [edi + ecx*4]
        add     eax, ebx      // eax = GetProcAddress
        
        pop     esi
        pop     edi
        pop     ebx
        pop     edx
        
        mov     esp, ebp
        pop     ebp
        ret
      }
    }
    __declspec(naked) void code_end()
{
    __asm _emit 0xCC
}
 
 DWORD make_code()
{
    int off; 
    __asm {
        mov edx, offset code_start
        mov dword ptr [pOrignCode], edx
        mov eax, offset code_end
        sub eax, edx
        mov dword ptr [dwSizeOfCode], eax
    }
    pCode= VirtualAlloc(NULL, dwSizeOfCode, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (pCode== NULL) {
        printf("[E]: VirtualAlloc failed\n");
        return 0;
    }
    printf("[I]: VirtualAlloc ok --> at 0x%08x.\n", pCode);
    for (off = 0; off<dwSizeOfCode; off++) {
        *(pCode+off) = *(pOrignCode+off);
    }
    printf("[I]: Copy code ok --> from 0x%08x to 0x%08x with size of 0x%08x.\n", 
        pOrignCode, pCode, dwSizeOfCode);
    return dwSizeOfCode;
} 

int main(int argc ,char* argv[]){
		DWORD pid = 0;
		DWORD sizeOfInjectedCode;
		DWORD addrGetProcAddress;
		int TID,num,old;
		PBYTE rcode;
		DWORD hproc,hthread;
		
    // 为pid赋值为hello.exe的进程ID
    if (argc < 2) {
        printf("Usage: %s pid\n", argv[0]);
        return -1;
    }
    pid = atoi(argv[1]);
		if (pid <= 0) {
        	printf("[E]: pid must be positive (pid>0)!\n"); 
        	return -2;
		}
		sizeOfInjectedCode=make_code();//将代码编译并拷贝入新的区域，返回代码长度
    hproc = OpenProcess(
          PROCESS_CREATE_THREAD  | PROCESS_QUERY_INFORMATION
        | PROCESS_VM_OPERATION   | PROCESS_VM_WRITE 
        | PROCESS_VM_READ, FALSE, pid);//创建远程进程句柄
        
    rcode=(PBYTE)VirtualAllocEx(hproc,0,sizeOfInjectedCode,MEM_COMMIT,PAGE_EXECUTE_READWRITE);//open the space in remote process
		if(!WriteProcessMemory(hproc, rcode, pCode, sizeOfInjectedCode, &num)){//写入代码
			printf("[E]Write pCode Failed\n");
			}else 
			{
			printf("[I]Write pCode Success\n");
			}
		hthread = CreateRemoteThread(hproc,NULL, 0, (LPTHREAD_START_ROUTINE)rcode,0, 0 , &TID);//创建线程
		WaitForSingleObject(hthread, 0xffffffff);//异步等待
		if(!GetExitCodeThread(hthread, &addrGetProcAddress)){//获取返回值
  		printf("[E]Get exitCode Failed\n");
  		}else 
  		{
  		printf("[I]Get exitCode Success\n");
  		}
		printf("The address of GetProcAddress is 0x%08x\n",addrGetProcAddress);//打印
		
	}  