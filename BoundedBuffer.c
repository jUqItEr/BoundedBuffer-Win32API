#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <process.h>
#include <time.h>
#include <Windows.h>

inline  bool  __fastcall IsNumber    (LPCWSTR);
       DWORD      WINAPI ConsumerProc(LPVOID);
       DWORD      WINAPI ProducerProc(LPVOID);

LPCWSTR lpcwErrorMessage[] = {
   TEXT("ARGUMENTS_INITIALIZATION_FAILED"),
   TEXT("ARGUMENTS_IS_NOT_ENOUGH"),
   TEXT("ARGUMENT_VALUE_HAS_NEGATIVE_NUMBER"),
   TEXT("ARGUMENTS_VALUE_IS_NOT_NUMBER"),
   TEXT("MEMORY_ALLOCATION_HAS_BEEN_FAILED"),
   TEXT("THREAD_CREATE_HAS_BEEN_FAILED")
};

HANDLE hFull;
HANDLE hMutex;
HANDLE hEmpty;

/* Implementation of the bounded buffer by queue. */
struct Queue {
    INT32* nBuffer;
    INT32  nBack;
    INT32  nFront;
} *queue;

INT32 nBufferSize;

INT32 __cdecl main(void) {
   LPWSTR* szArgList;
   HANDLE* hThreads;
   INT32*  nParams;
   INT32   nArgs;
   INT32   nErrorCode;
   INT32   nErrorLength;
   INT32   nI;
   INT32   nThreadSize;

   nErrorCode   = 0x00;
   nErrorLength = sizeof lpcwErrorMessage / sizeof *lpcwErrorMessage;

   /* Get command line arguments */
   szArgList = CommandLineToArgvW(GetCommandLineW(), &nArgs);

   /* For handling error */
   if (szArgList == NULL) {
       /* If the Win32 API can't load arguments, it must to be the error. */
       nErrorCode |= 0x01;
   } else if (nArgs != 3) {
       /* If the program can't get two arguments, it must to be the error. */
       nErrorCode |= 0x02;
   } else {
       /* Check the argument string is number and negative number.*/
       if (!IsNumber(szArgList[1]) || !IsNumber(szArgList[2])) {
           nErrorCode |= 0x08;
       } else {
           /* Get a number from szArgList[1], szArgList[2] */
           nBufferSize = wcstol(szArgList[1], NULL, 10);
           nThreadSize = wcstol(szArgList[2], NULL, 10);
           nErrorCode |= ((nBufferSize < 0) << 2) | ((nThreadSize < 0) << 2);
       }
   }
   /* Dispose Win32 Handle dynamic memory allication area */
   if (szArgList != NULL) {
       LocalFree(szArgList);
   }
   queue = malloc(sizeof(struct Queue));

   if (!queue) {
       nErrorCode |= 0x10;
   }
   if (!nErrorCode) {
       /* Set semaphores. 
        *
        * hElement: Check elements available in the buffer.
        * hMutex  : For mutual exclusion lock.
        * hSpace  : Check space available in the buffer.
        */
       hFull  = CreateSemaphore(NULL, 0,           nBufferSize, NULL);
       hMutex = CreateSemaphore(NULL, 1,           1,           NULL);
       hEmpty = CreateSemaphore(NULL, nBufferSize, nBufferSize, NULL);

       hThreads = malloc(sizeof(HANDLE) * nThreadSize);
       nParams  = malloc(sizeof(INT32) * nThreadSize);

       queue->nBuffer = malloc(sizeof(INT32) * nBufferSize);
       queue->nBack   = 0;
       queue->nFront  = 0;

       if (hThreads && nParams && queue->nBuffer) {
          for (nI = 0; nI < nThreadSize; nI += 2) {
             nParams[nI] = nI;
             hThreads[nI] = CreateThread(NULL, 0, ProducerProc, &nParams[nI], 0, NULL);
          }
          for (nI = 1; nI < nThreadSize; nI += 2) {
             nParams[nI] = nI;
             hThreads[nI] = CreateThread(NULL, 0, ConsumerProc, &nParams[nI], 0, NULL);
          }

          for (nI = 0; nI < nThreadSize; ++nI) {
             if (!hThreads[nI]) {
                nErrorCode |= 0x20;
                nI = nThreadSize;
             }
          }

          if (!nErrorCode) {
             for (nI = 0; nI < nThreadSize; ++nI) {
                WaitForSingleObject(hThreads[nI], INFINITE);
             }
             for (nI = 0; nI < nThreadSize; ++nI) {
                CloseHandle(hThreads[nI]);
             }
          }
       } else {
          nErrorCode |= 0x10;
       }
       /* Dispose the mutual exclusion handles. */
       CloseHandle(hFull);
       CloseHandle(hMutex);
       CloseHandle(hEmpty);

       if (nParams) {
          free(nParams);
       }
   }

   /* Error handling */
   for (nI = 0; nI < nErrorLength; ++nI) {
      if (nErrorCode & 1) {
         fwprintf(
            stderr,
            L"[PARENT]: The program has unexpectedly finish."
            L"The error code is '%s'.\n",
            lpcwErrorMessage[nI]
         );
         nErrorCode = EXIT_FAILURE << 1;
         nI = nErrorLength;
      }
      nErrorCode >>= 1;
   }

   exit(nErrorCode);
}

inline bool __fastcall IsNumber(LPCWSTR str) {
    bool   result = true;
    LPWSTR p      = (LPWSTR)str;

    for (; *p; ++p) {
        if (!iswctype(*p, 0x04) && *p != L'-') {
            result = false;
            break;
        }
    }
    return result;
}

DWORD WINAPI ConsumerProc(LPVOID lpParam) {
   INT32 nIndex = *(INT32*)lpParam;

   srand((UINT32)time(NULL) + nIndex);

   while (true) {
      INT32 nConsumeData;

      Sleep(rand() % 300);

      /* Accuire the full lock. */
      WaitForSingleObject(hFull, INFINITE);
      /* Accuire the mutax lock. */
      WaitForSingleObject(hMutex, INFINITE);

      nConsumeData = queue->nBuffer[queue->nFront];

      fwprintf(
         stdout,
         L"[CONSUMER #%03d]: The consumer thread consumed BUF[%d]=%d.\n",
         nIndex, queue->nFront, nConsumeData
      );

      queue->nBuffer[queue->nFront] = 0;
      queue->nFront = (queue->nFront + 1) % nBufferSize;

      /* Release the mutax lock. */
      ReleaseSemaphore(hMutex, 1, 0);
      /* Release the empty lock. */
      ReleaseSemaphore(hEmpty, 1, 0);
   }

   return EXIT_SUCCESS;
}

DWORD WINAPI ProducerProc(LPVOID lpParam) {
   INT32 nIndex = *(INT32*)lpParam;

   srand((UINT32)time(NULL) + nIndex);

   while (true) {
      INT32 nProduceData;

      nProduceData = rand() % 300;
      
      Sleep(nProduceData);

      /* Accuire the empty lock. */
      WaitForSingleObject(hEmpty, INFINITE);
      /* Accuire the mutax lock. */
      WaitForSingleObject(hMutex, INFINITE);

      queue->nBuffer[queue->nBack] = nProduceData;

      fwprintf(
         stdout,
         L"[PRODUCER #%03d]: The producer thread produced BUF[%d]=%d.\n",
         nIndex, queue->nBack, nProduceData
      );

      queue->nBack = (queue->nBack + 1) % nBufferSize;

      /* Release the mutax lock. */
      ReleaseSemaphore(hMutex, 1, 0);
      /* Release the full lock. */
      ReleaseSemaphore(hFull, 1, 0);
   }

   return EXIT_SUCCESS;
}