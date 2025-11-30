#pragma once

#include <iostream>
#include <cstring>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include "Common.h"

#ifdef _WIN32
#include <Windows.h>
#else
//Linux
#endif

#define MALLOCSIZE 128 * 1024

using std::cout;
using std::endl;

namespace nsObjectPool
{
	//固定大小元素的内存池
	template<class T>
	class ObjectPool
	{
	public:
		ObjectPool()
			:_memory(nullptr), _remainBytes(0), _freeList(nullptr)
		{}
		T* New()
		{
			T * obj = nullptr; //表示要拿的目标内存块
			//如果自由链表有，直接去自由链表拿就行
			if (_freeList != nullptr)
			{
				//直接头删
				void* next = *((void**)_freeList);
				obj = (T*)_freeList;
				_freeList = next;
			}
			//自由链表没有的话，直接去大块内存拿
			else
			{
				//存在两种无法直接拿的情况：1、没有内存了；2、有内存，但不满足T类型大小；
				//但可以归为一类，就是剩余内存不够一个对象大小，此时要重新开大块空间
					if (_remainBytes < sizeof(T))
					{
						if (_memory != nullptr) //还剩一点内存，给它释放掉
						{
							free(_memory);
							_memory = nullptr;
						}
						//申请新的一大块内存
						_remainBytes = MALLOCSIZE;
						_memory = (char*)SystemAlloc(_remainBytes >> PAGESIZE);
						if (_memory == nullptr)
						{
							cout << "malloc failed" << endl;
							throw std::bad_alloc();
						}
					}

					//拿内存块
					obj = (T*)_memory;
					//由于内存块被释放后，需要加入自由链表，这个内存块在自由链表时需要存储下一块内存块的地址，
					//所以一个内存块的地址应该不低于一个地址的大小 -- x86-4字节，x64-8字节
					size_t size = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
					_memory += size;
					_remainBytes -= size;
			}
			//到这里就已经拿到一个T类型的内存了
			//c++的new会自动调用T类型的构造函数，但malloc
			//这里调用T类型的构造函数并返回 -- 使用定位new，显示调用T的构造函数
			new(obj)T;

			return obj;
		}
		void Delete(T* obj)
		{
			//c++的delete会自动调用T类型的析构函数,这里调用析构函数
			obj->~T();
			//头插
			*(void**)obj = _freeList;
			_freeList = obj;
		}
	private:
		char* _memory; //指向大块内存
		size_t _remainBytes; //大块内存切分后剩余多少字节
		void* _freeList; //还回来后用于存储内存块的自由链表
	};
}