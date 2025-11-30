#include "CentralCache.h"
#include "PageCache.h"

nsCentralCache::CentralCache nsCentralCache::CentralCache::_sInst;

//向page cache获取一个非空的span
Span* nsCentralCache::CentralCache::GetOneSpan(SpanList& list, size_t byte_size)
{
	//有非空的span就返回
	Span* it = list.Begin();
	Span* eit = list.End();
	while (it != eit)
	{
		if (it->_freeList != nullptr)
		{
			return it;
		}
		else
		{
			it = it->_next;
		}
	}
	//没有非空的span就向page cache申请
	//这里先解除通锁，因为这里的任务是向page cache申请并切分，没有对桶进行读写，所以解锁提高整体效率
	list._mtx.unlock();

	nsPageCache::PageCache::GetInstance()->_pageMtx.lock();
	Span* newSpan = nsPageCache::PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(byte_size));
	newSpan->_isUse = true;
	newSpan->_objSize = byte_size;
	nsPageCache::PageCache::GetInstance()->_pageMtx.unlock();

	//切分newSpan
	char* start = (char*)(newSpan->_pageId << PAGE_SHIFT);
	size_t bytes = newSpan->_n << PAGE_SHIFT;
	char* end = start + bytes;
	//头插
	newSpan->_freeList = start;
	void* tail = start;
	start += byte_size;
	while (start < end)
	{
		NextObj(tail) = start;
		tail = start;
		start += byte_size;
	}

	NextObj(tail) = nullptr;

	//切分好之后，需要把newSpan挂到桶里面取，需要再加锁
	list._mtx.lock();
	list.PushFront(newSpan);

	return newSpan;
}

//从central cache获取一定数量的内存块给thread cache
size_t nsCentralCache::CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size)
{
	size_t index = SizeClass::Index(size);
	//从桶里获取需要加上锁
	_spanLists[index]._mtx.lock();

	//取出一个非空的span
	Span* span = GetOneSpan(_spanLists[index], size);
	assert(span);
	assert(span->_freeList);

	//在这个非空的span中获取batchNum个小内存块对象
	//如果不足batchNum个，有多少要多少
	start = span->_freeList;
	end = start; //这里至少有一个
	size_t actualNum = 1;
	while (NextObj(end) != nullptr && actualNum < batchNum)
	{
		++actualNum;
		end = NextObj(end);
	}
	span->_freeList = NextObj(end);
	NextObj(end) = nullptr;
	span->_useCount += actualNum;

	_spanLists[index]._mtx.unlock();//解锁
	
	return actualNum; //返回实际获取的小内存块个数和获取的小内存块链表
}

//将一定数量的对象释放到span
void nsCentralCache::CentralCache::ReleaseListToSpans(void* start, size_t byte_size)
{
	assert(start);

	size_t index = SizeClass::Index(byte_size);

	//加桶锁
	_spanLists[index]._mtx.lock();

	void* next = nullptr;
	while (start)
	{
		next = NextObj(start);

		//通过哈希映射，将Span* 和 pageId（可以通过地址转换成pageId）联系起来 -- 这里每一个小内存块对应的span可能都是不一样的
		Span* span = nsPageCache::PageCache::GetInstance()->MapObjectToSpan(start);
		NextObj(start) = span->_freeList;
		span->_freeList = start;
		span->_useCount--;
		
		//通过span->_useCount计算，如果该值为0，那么说明这个span里的小内存块都回来了，
		//这个span就可以再回收给page cache，page cache可以再尝试去做前后页的合并
		if (span->_useCount == 0)
		{
			_spanLists[index].Erase(span);
			span->_freeList = nullptr;
			span->_next = span->_prev = nullptr;
			
			//解桶锁
			_spanLists[index]._mtx.unlock();

			//加page cache大锁
			nsPageCache::PageCache::GetInstance()->_pageMtx.lock();
			nsPageCache::PageCache::GetInstance()->ReleaseSpanToPageCache(span);
			nsPageCache::PageCache::GetInstance()->_pageMtx.unlock();

			_spanLists[index]._mtx.lock();
		}
		start = next;
	}
	_spanLists[index]._mtx.unlock();
}