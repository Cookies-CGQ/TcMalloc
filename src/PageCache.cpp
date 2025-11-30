#include "PageCache.h"

nsPageCache::PageCache nsPageCache::PageCache::_sInst;

//获取一个k页的span
Span* nsPageCache::PageCache::NewSpan(size_t k)
{
	assert(k > 0 && k < NPAGES); // [ 1, 128 ]
	
	//大于128page的直接向系统（堆）申请
	if (k > NPAGES - 1)
	{
		void* ptr = SystemAlloc(k);
		Span* span = _spanPool.New();

		span->_pageId = (ADDRES_INT)ptr >> PAGE_SHIFT;
		span->_n = k;

		//_idSpanMap[span->_pageId] = span; //这个大于128page的span，只在map里面记录键值对，不插入page cache的SpanLists
		_idSpanMap.set(span->_pageId, span);

		return span;
	}

	//小于等于128page
	// 1、先检查第k个桶里面有没有span，有就直接返回
	if (!_spanLists[k].Empty())
	{
		Span* kSpan = _spanLists[k].PopFront();
		//建立id和span的映射，方便central cache回收小内存块时，查找对应的span
		for (PageID i = 0; i < kSpan->_n; ++i)
		{
			//_idSpanMap[kSpan->_pageId + i] = kSpan;
			_idSpanMap.set(kSpan->_pageId + i, kSpan);
		}
		return kSpan;
	}

	// 2、第k个桶里面没有span，检查后面的桶里面有没有span，如果有可以把它进行切分
	for (size_t i = k + 1; i < NPAGES; ++i)
	{
		if ( !_spanLists[i].Empty() )
		{
			//如果找到第i桶有span，将span切分成两块，一块k页，另一块 i - k 页
			Span* nSpan = _spanLists[i].PopFront();
			//Span* kSpan = new Span;
			Span* kSpan = _spanPool.New();

			//nSpan头切k页给kSpan
			//pageId和地址可以互相转换
			kSpan->_n = k;
			kSpan->_pageId = nSpan->_pageId;;

			nSpan->_n -= k;
			nSpan->_pageId += k;

			//将nSpan根据页数，分配到对应的桶里面
			_spanLists[nSpan->_n].PushFront(nSpan);

			//_idSpanMap[nSpan->_pageId] = nSpan;
			//_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;

			_idSpanMap.set(nSpan->_pageId, nSpan);
			_idSpanMap.set(nSpan->_pageId + nSpan->_n - 1, nSpan);

			//建立id和span的映射，方便central cache回收小内存块时，查找对应的span
			for (PageID i = 0; i < kSpan->_n; ++i)
			{
				/*_idSpanMap[kSpan->_pageId + i] = kSpan;*/
				_idSpanMap.set(kSpan->_pageId + i, kSpan);
			}

			//返回kSpan
			return kSpan;
		}
	}

	//3、走到这里，说明已经没有更大的页可以切分了，就需要找系统（堆）要一个128页的span
	//Span* bigSpan = new Span();
	Span* bigSpan = _spanPool.New();
	void* ptr = SystemAlloc(NPAGES - 1); //128页
	bigSpan->_n = NPAGES - 1;
	bigSpan->_pageId = (ADDRES_INT)ptr >> PAGE_SHIFT;

	//插入桶中
	_spanLists[bigSpan->_n].PushFront(bigSpan);

	return NewSpan(k); //递归回调
}

Span* nsPageCache::PageCache::MapObjectToSpan(void* obj)
{
	PageID id = (ADDRES_INT)obj >> PAGE_SHIFT;

	//加锁（RAII风格的锁）-- 因为读取_idSpanMap时要上锁 -- 因为内部数据结构在其他线程访问时可能会发生结构上的改变
	//std::unique_lock<std::mutex> lock(_pageMtx);
	//这个场景下，读写不用加锁，因为读写分离

	//auto ret = _idSpanMap.find(id);
	//if (ret != _idSpanMap.end())
	//{
	//	return ret->second;
	//}
	//else
	//{
	//	assert(false);
	//	return nullptr;
	//}

	auto ret = (Span*)_idSpanMap.get(id);
	assert(ret != nullptr);

	return ret;
}

void nsPageCache::PageCache::ReleaseSpanToPageCache(Span* span)
{
	//如果回收的page大于128，直接还给系统（堆）
	if (span->_n > NPAGES - 1)
	{
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		SystemFree(ptr);
		_spanPool.Delete(span);

		return;
	}
	
	//对span前后页尝试进行合并，缓解内存碎片问题
	while (1)
	{
		PageID prevId = span->_pageId - 1;
		auto ret = (Span*)_idSpanMap.get(prevId);
		if (ret == nullptr)
		{
			//没有前一页，不合并了
			break;
		}
		//有前一页，但是得判断是否在使用，如果在使用，就不合并了
		Span* prevSpan = ret;
		if (prevSpan->_isUse == true)
		{
			break;
		}
		//合并出超出128页的span没办法管理，不合并了
		if (prevSpan->_n + span->_n > NPAGES - 1)
		{
			break;
		}

		//合并
		span->_pageId = prevSpan->_pageId;
		span->_n += prevSpan->_n;

		_spanLists[prevSpan->_n].Erase(prevSpan);
		//delete prevSpan;
		_spanPool.Delete(prevSpan);
	}
	while (1)
	{
		PageID nextId = span->_pageId + span->_n;
		auto ret = (Span*)_idSpanMap.get(nextId);
		if (ret == nullptr)
		{
			break;
		}
		
		Span* nextSpan = ret;
		if (nextSpan->_isUse == true)
		{
			break;
		}
		if (nextSpan->_n + span->_n > NPAGES - 1)
		{
			break;
		}

		//合并
		span->_n += nextSpan->_n;

		_spanLists[nextSpan->_n].Erase(nextSpan);
		//delete nextSpan;
		_spanPool.Delete(nextSpan);
	}

	//合并之后重新插入对应页数的位置
	_spanLists[span->_n].PushFront(span);
	span->_isUse = false;
	
	//_idSpanMap[span->_pageId] = span;
	//_idSpanMap[span->_pageId + span->_n - 1] = span;

	_idSpanMap.set(span->_pageId, span);
	_idSpanMap.set(span->_pageId + span->_n - 1, span);
}