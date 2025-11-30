#pragma once

#include "Common.h"

namespace nsCentralCache
{
	class CentralCache
	{
	public:
		static CentralCache* GetInstance()
		{
			return &_sInst;
		}

		//向page cache获取一个非空的span
		Span* GetOneSpan(SpanList& list, size_t byte_size);

		//从central cache获取一定数量的内存块给thread cache
		size_t FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size);

		//将一定数量的对象释放到span跨度
		void ReleaseListToSpans(void* start, size_t byte_size);

	private:
		SpanList _spanLists[NFREELISTS];

	private:
		CentralCache()
		{}
		CentralCache(const CentralCache&) = delete;

		static CentralCache _sInst;
	};
}