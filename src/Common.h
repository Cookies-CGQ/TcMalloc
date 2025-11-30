#pragma once

#include <iostream>
#include <cstring>
#include <vector>
#include <string>
#include <unordered_map>
#include <map>
#include <algorithm>
#include <time.h>
#include <cstdlib>
#include <cassert>
#include <thread>
#include <mutex>
#include "Common.h"

#ifdef _WIN32
#include <Windows.h>
#else //Linux
#include <sys / syscall.h>
#include <unistd.h>
#endif

using std::cout;
using std::endl;

#ifdef _WIN64
#define PAGESIZE 13 //8k，2^13，13位
#elif _WIN32
#define PAGESIZE 13 //4k，2^12，12位
#endif

//小于MAX_BYTES，直接找thread cache申请
//大于MAX_BYTES，直接找page cache或者系统申请
static const size_t MAX_BYTES = 256 * 1024;

//thread cache和central cache自由链表哈希桶的表大小
static const size_t NFREELISTS = 208;

//page cache 管理span list哈希表大小
static const size_t NPAGES = 129;

//页大小转换偏移，即一页定义为2^13，也就是8KB
static const size_t PAGE_SHIFT = 13;

//地址大小类型，32位下是4byte类型，64位下是8byte类型
#ifdef _WIN64
typedef unsigned long long ADDRES_INT;
#else
typedef size_t ADDRES_INT;
#endif

//页编号类型，32位下是4byte类型，64位下是8byte类型
#ifdef _WIN64
typedef unsigned long long PageID;
#else
typedef size_t PageID;
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////
//直接去堆上按页申请内存
inline static void* SystemAlloc(size_t kpage)
{
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, kpage << PAGESIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else //Linux -- brk -- glibc封装的brk函数或者直接调用系统调用，这里选择系统调用
	// 获取当前堆顶地址
	void* current_brk = (void*)syscall(SYS_brk, 0);
	// 扩展堆字节
	void* ptr = (void*)syscall(SYS_brk, (unsigned long)current_brk + (kpage << PAGESIZE));
#endif

	if (ptr == nullptr)
	{
		cout << "SystemAlloc failed" << endl;
		throw std::bad_alloc();
	}
	return ptr;
}

inline static void SystemFree(void* ptr)
{
#ifdef  _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	syscall(SYS_brk, (unsigned long)ptr);
#endif
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////
//获取内存对象中存储的头 4 or 8字节值，即链接的下一个对象的地址
inline void*& NextObj(void* obj)
{
	return *((void**)obj);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////
//管理切分号的内存块的自由链表
class FreeList
{
public:
	FreeList()
		:_freeList(nullptr),_maxSize(1),_size(0)
	{}

	void Push(void* obj)
	{
		assert(obj);

		//头插
		NextObj(obj) = _freeList;
		_freeList = obj;

		++_size;
	}

    //批量插入从start到end的自由链表内的内存块
	void PushRange(void* start, void* end, size_t n)
	{
		assert(start && end);
		//为什么这里直接这样就好了？因为从central cache申请的内存块之间已经建立好链表关系了
		NextObj(end) = _freeList;
		_freeList = start;

		_size += n;
	}

	//批量删除自由链表内n个内存块
	void PopRange(void*& start, void*& end, size_t n) //输入输出型参数
	{
		assert(n <= _size);

		start = _freeList;
		end = start;

		for (size_t i = 0; i < n - 1; ++i)
		{
			end = NextObj(end);
		}

		_freeList = NextObj(end);
		_size -= n;
		NextObj(end) = nullptr;
	}

	void* Pop()
	{
		assert(_freeList);

		void* obj = _freeList;
		_freeList = NextObj(_freeList);
		--_size;

		return obj;
	}

	bool Empty()
	{
		return _freeList == nullptr;
	}

	size_t& MaxSize()
	{
		return _maxSize;
	}
	
	size_t Size()
	{
		return _size;
	}
private:
	void* _freeList; //自由链表
	size_t _maxSize; //慢开始反馈调节算法
	size_t _size; //记录自由链表的个数
};
////////////////////////////////////////////////////////////////////////////////////////////////////////////
//功能：计算对齐到哪个桶；计算桶的下标；计算一次从thread cache获取多少个内存块；计算一次从page cache获取多少个页 
class SizeClass
{
public:
	static inline size_t _RoundUp(size_t bytes, size_t align)
	{
		return ((bytes + align - 1) & ~(align - 1)); //相当于(btyes / align 上取整) * align
	}
	// 整体控制在最多10%左右的内碎片浪费
	// [1, 128]									 8byte对⻬				freelist[0,16)
	// [128+1, 1024]						 16byte对⻬				freelist[16,72)
	// [1024+1, 8*1024]					 128byte对⻬			freelist[72,128)
	// [8*1024+1, 64*1024]		     1024byte对⻬			freelist[128,184)
	// [64*1024+1, 256*1024]		 8*1024byte对⻬     freelist[184,208)
	//根据申请的bytes的大小，通过对齐计算后得到应该向上对齐到哪个桶 如：8->8, 6->8, 9->16, 31->32
	static inline size_t RoundUp(size_t bytes)
	{
		if (bytes <= 128)
			return _RoundUp(bytes, 8);
		else if (bytes <= 1024)
			return _RoundUp(bytes, 16);
		else if (bytes <= 8 * 1024)
			return _RoundUp(bytes, 128);
		else if (bytes <= 64 * 1024)
			return _RoundUp(bytes, 1024);
		else if (bytes <= 256 * 1024)
			return _RoundUp(bytes, 8 * 1024);
		else //大于256KB
		{
			//直接向page cache申请
			return _RoundUp(bytes, 1 << PAGE_SHIFT);
		}
	}

	static inline size_t _Index(size_t bytes, size_t align_shfit)
	{
		return ((bytes + (1 << align_shfit) - 1) >> align_shfit) - 1;
	}

	//计算映射到哪一个自由链表桶（从下标0开始依次递增）
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);

		//每个对齐区间有多少个链表
		static int group_array[4] = { 16, 56, 56, 56 };
		if (bytes <= 128)
			return _Index(bytes, 3); // 2^3 = 8字节对齐
		else if (bytes <= 1024)
			return _Index(bytes - 128, 4) + group_array[0]; //2^4 = 16字节对齐
		else if (bytes <= 8 * 1024)
			return _Index(bytes - 1024, 7) + group_array[0] + group_array[1]; // 2^7 = 128字节对齐
		else if (bytes <= 64 * 1024)
			return _Index(bytes - 8 * 1024, 10) + group_array[0] + group_array[1] + group_array[2]; // 2^10=1024字节对齐
		else if (bytes <= 256 * 1024)
			return _Index(bytes - 64 * 1024, 13) + group_array[0] + group_array[1] + group_array[2] + group_array[3]; //2^13 = 8096字节对齐
		else
			assert(false);

		return -1;
	}

	//一次从cnetral cache获取多少个大小位size的内存块
	static size_t NumMoveSize(size_t size)
	{
		assert(size > 0);
		//小内存块一次批量上线高
		//大内存块一次批量上线低
		int num = MAX_BYTES / size;
		if (num < 2)
			num = 2;
		if (num > 512)
			num = 512;

		return num;
	}

	//计算一次向系统获取几个页
	static size_t NumMovePage(size_t size) 
	{
		size_t num = NumMoveSize(size);
		size_t npage = num * size;
		npage >>= PAGE_SHIFT;
		if (npage == 0)
			npage = 1;

		return npage;
	}
};
////////////////////////////////////////////////////////////////////////////////////////////////////////////
//span元素，管理以页为单位大块内存块，外部是连接其他span的带头双向循环链表，每个span内部是小块内存块的自由链表
struct Span
{
	//双向循环链表结构
	Span* _next = nullptr;
	Span* _prev = nullptr;

	PageID _pageId = 0; //大块内存起始页的页号 -> 计算：页号 = 地址 >> PAGE_SHIFT
	size_t _n = 0; //页的数量

	size_t _objSize = 0; //切分成小内存块对象的大小
	size_t _useCount = 0; //切分成小内存对象被分配给thread cache的计数
	
	void* _freeList = nullptr; //切分成小内存对象的自由链表

	bool _isUse = false; //是否在被使用 -- 在page cache中就是没被使用，在central cache中就是被使用
};

//以Span为元素的带头双向循环链表
class SpanList
{
public:
	SpanList()
	{
		_head = new Span;
		_head->_next = _head->_prev = _head; //循环链表
		//其他数据为空，因为头节点只是一个哨兵位，不携带有效数据，只记录位置信息
	}

	//[Begin， end)，左闭右开
	//返回哨兵位next指向的有效节点
	Span* Begin()
	{
		return _head->_next;
	}

	//返回哨兵位
	Span* End()
	{
		return _head;
	}

	bool Empty()
	{
		return _head == _head->_next;
	}
	
	void PushFront(Span* span)
	{
		assert(span);
		//Insert(_head->_next, span);
		Insert(Begin(), span);
	}

	Span* PopFront()
	{
		//return Erase(_head->_next);
		return Erase(Begin());
	}

	void Insert(Span* pos, Span* newSpan)
	{
		assert(pos);
		assert(newSpan);

		//prev newSpan pos -> pos位置前插
		pos->_prev->_next = newSpan;
		newSpan->_prev = pos->_prev;
		newSpan->_next = pos;
		pos->_prev = newSpan;
	}

	Span* Erase(Span* pos)
	{
		assert(pos);
		assert(pos != _head);

		pos->_prev->_next = pos->_next;
		pos->_next->_prev = pos->_prev;
		
		return pos;
	}

	~SpanList()
	{
		delete _head;
		_head = nullptr;
	}
	
public:
	std::mutex _mtx; //桶锁，每个桶一个
private:
	Span* _head; //头节点--哨兵位
};