#pragma once


#include "Common.h"
#include "ThreadCache.h"
#include "PageCache.h"
#include "ObjectPool.h"

static void* ConcurrentAlloc(size_t size)
{
	//如果大于256KB，直接去系统（堆）申请
	if (size > MAX_BYTES)
	{
		size_t alignSize = SizeClass::RoundUp(size);
		size_t kpage = alignSize >> PAGE_SHIFT;

		nsPageCache::PageCache::GetInstance()->_pageMtx.lock();
		Span* span = nsPageCache::PageCache::GetInstance()->NewSpan(kpage);
		span->_objSize = alignSize;
		nsPageCache::PageCache::GetInstance()->_pageMtx.unlock();

		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);

		return ptr;
	}
	else
	{
		//通过TLS每个线程无锁的获取自己的专属ThreadCache对象
		if (nsThreadCache::pTLSThreadCache == nullptr)
		{
			static nsObjectPool::ObjectPool<nsThreadCache::ThreadCache> tcPool;
			nsThreadCache::pTLSThreadCache = tcPool.New();
			//cout << "Thread start# thred_id: " << std::this_thread::get_id() << " pTLSThreadCache: " << nsThreadCache::pTLSThreadCache << endl;
		}

		return nsThreadCache::pTLSThreadCache->Allocate(size);
	}
}

static void ConcurrentFree(void* ptr)
{
	assert(ptr);

	Span* span = nsPageCache::PageCache::GetInstance()->MapObjectToSpan(ptr);
	size_t size = span->_objSize;

	if (size > MAX_BYTES)
	{
		nsPageCache::PageCache::GetInstance()->_pageMtx.lock();
		nsPageCache::PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		nsPageCache::PageCache::GetInstance()->_pageMtx.unlock();
	}
	else
	{
		assert(nsThreadCache::pTLSThreadCache);
		nsThreadCache::pTLSThreadCache->Deallocate(ptr, size);
	}
}