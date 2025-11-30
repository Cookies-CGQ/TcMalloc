#pragma once


#include "Common.h"

namespace nsThreadCache
{
	class ThreadCache
	{
	public:
		//申请和释放
		void* Allocate(size_t size);
		void Deallocate(void* ptr, size_t size);
		//从中心缓存获取对象
		void* FetchFromCentralCache(size_t index, size_t size);
		//释放对象时，自由链表过长，回收内存回到中心缓存
		void ListTooLong(FreeList& list, size_t size);
	private:
		FreeList _freeLists[NFREELISTS];
	};

	//TLS thread local storage
	static _declspec(thread) ThreadCache* pTLSThreadCache = nullptr;
}