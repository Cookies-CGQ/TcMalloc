#include "ThreadCache.h"
#include "CentralCache.h"

void* nsThreadCache::ThreadCache::Allocate(size_t size)
{
	assert(size <= MAX_BYTES);
	if (size == 0)
		return nullptr;
	
	size_t alignSize = SizeClass::RoundUp(size);
	size_t index = SizeClass::Index(size);
	if ( !_freeLists[index].Empty() ) //不为空
	{
		return _freeLists[index].Pop();
	}
	//为空 -- 找central cache要
	else
	{
		return FetchFromCentralCache(index, alignSize);
	}
}

void nsThreadCache::ThreadCache::Deallocate(void* ptr, size_t size)
{
	assert(ptr);
	assert(size <= MAX_BYTES);
	assert(size != 0);

	size_t index = SizeClass::Index(size);
	_freeLists[index].Push(ptr);

	//当链表长度大于一次批量申请内存时就开始还一段内存给central cache
	if (_freeLists[index].Size() >= _freeLists[index].MaxSize())
	{
		ListTooLong(_freeLists[index], size);
	}
}

//从cache Cache获取对象
void* nsThreadCache::ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
	//慢开始反馈调节算法
	//1、最开始不回一次向central cache一次批量要太多，因为要太多了可能用不完
	//2、如果不需要这个size大小内存需求，那么batchNum就会不断增大，直到上限
	//3、size越小，一次向central cache要的batchNum就越大
	//4、size越大，一次向central cache要的batchNum就越小
	size_t batchNum = min(_freeLists[index].MaxSize(), SizeClass::NumMoveSize(size));
	if (batchNum == _freeLists[index].MaxSize())
	{
		++_freeLists[index].MaxSize();
	}

	void* start = nullptr;
	void* end = nullptr;
	size_t actualNum = nsCentralCache::CentralCache::GetInstance()->FetchRangeObj(start,end,batchNum,size);
	assert(actualNum >= 1);

	//只有一个直接给用户
	if (actualNum == 1)
	{
		assert(start == end);
		return start;
	}
	else
	{
		//申请到多个，一个给用户，剩下的插入自由链表
		_freeLists[index].PushRange(NextObj(start), end, actualNum - 1);
		return start;
	}
}
//释放对象时，自由链表过长，回收内存回到中心缓存
void nsThreadCache::ThreadCache::ListTooLong(FreeList& list, size_t size)
{
	void* start = nullptr;
	void* end = nullptr;
	//移除
	list.PopRange(start, end, list.MaxSize());
	//交给central cache
	nsCentralCache::CentralCache::GetInstance()->ReleaseListToSpans(start, size);
}