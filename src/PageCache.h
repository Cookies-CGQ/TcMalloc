#pragma once

#include "Common.h"
#include "CentralCache.h"
#include "ObjectPool.h"
#include "PageMap.h"

namespace nsPageCache
{
	//1、page cache是一个以页为单位的span自由链表
	//2、为了保证全局只有唯一的page cache，这个类被设计成了单例模式
	class PageCache
	{
	public:
		static PageCache* GetInstance()
		{
			return &_sInst;
		}

		Span* NewSpan(size_t k);

		Span* MapObjectToSpan(void* obj);

		void ReleaseSpanToPageCache(Span* span);

		std::mutex _pageMtx; //大锁

	private:
		PageCache()
		{}
		PageCache(const PageCache&) = delete;

	private:
		SpanList _spanLists[NPAGES];
		nsObjectPool::ObjectPool<Span> _spanPool; //用于管理Span类型的定长内存

		//std::map<PageID, Span*> _idSpanMap;
		TCMalloc_PageMap1<32 - PAGE_SHIFT> _idSpanMap;
		
		//单例模式
		static PageCache _sInst;
	};
}