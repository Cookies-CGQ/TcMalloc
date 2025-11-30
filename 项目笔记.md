# 项目--高并发内存池

原项目：Google的tcmalloc

## 解决问题：

1、效率问题
2、（堆）内存碎片问题：
	a、外碎片（这个项目解决的）
	b、内碎片

![image-20250813171719970](C:\Users\CGQ\AppData\Roaming\Typora\typora-user-images\image-20250813171719970.png)
![image-20250813171732766](C:\Users\CGQ\AppData\Roaming\Typora\typora-user-images\image-20250813171732766.png)

（malloc和free：
	C++ 的 `new` 和 `delete` 通过调用 `operator new` 和 `operator delete` 来实现内存管理，而这些运算符函数通常（但不是必须）会调用 `malloc` 和 `free`，同时在这一层封装中加入了异常处理机制，使得内存分配失败时可以抛出异常而不是简单地返回空指针。）
![image-20250813174228349](image-20250813174228349.png)

|                   概念                    |                           说明                            |                    示例                     |
| :---------------------------------------: | :-------------------------------------------------------: | :-----------------------------------------: |
|           `new[]` / `delete[]`            | 用于数组分配，调用 `operator new[]` / `operator delete[]` |   `int* arr = new int[10]; delete[] arr;`   |
| 类专属 `operator new` / `operator delete` |                可以自定义类的内存分配方式                 | `MyClass* obj = new MyClass(); delete obj;` |
|       定位 `new` (`placement new`)        |          在已分配的内存上构造对象，不分配新内存           |          `new (buffer) MyClass();`          |

![image-20250813175826865](image-20250813175826865.png)

## 项目特点：

tcmalloc比malloc在多线程情况下更快

## part 1 定长内存池

c++的非类型模板和类型模板；

跳过malloc直接使用系统调用申请内存：
Windows：VirtualAlloc
Linux：brk和mmap
可以指通过直接调用系统调用，脱离掉malloc（内存池申请大块内存部分）

```c++
#include <iostream>
#include <vector>
#include <time.h>

using std::cout;
using std::endl;

// //定长内存池 -- 非类型模板
// template <size_t N>
// class ObjectPool
// {};

// 定长内存池 -- 类型模板
template <class T>
class ObjectPool
{
#define SIZE 512 * 1024 // 512KB
public:
    T *New()
    {
        T* obj = nullptr;
        // 优先利用还回来的内存块
        if (_freeList)
        {
            void *next = *((void **)_freeList);
            obj = (T*)_freeList;
            _freeList = next;
        }
        else
        {
            // 申请内存 -- 为空 || 剩余内存不足一个T对象大小
            if (_remainBytes < sizeof(T)) // if(_memory == nullptr || _remainBytes < sizeof(T))
            {
                _memory = (char *)malloc(SIZE);
                if (_memory == nullptr)
                {
                    throw std::bad_alloc();
                }
                _remainBytes = SIZE;
            }

            obj = (T*)_memory;
            size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
            _memory += objSize;
            _remainBytes -= objSize;
        }

        // 定位new初始化,用于对一块已经分配的内存进行初始化
        // malloc不会自动调用构造函数
        // new可以自动调用构造函数
        new(obj) T;

        return obj;
    }
    void Delete(T *obj)
    {
        // 显式调用析构函数
        // free不会自动调用析构函数
        // delete可以自动调用析构函数
        obj->~T();

        // if(_freeList == nullptr)
        // {
        //     _freeList = obj;
        //     //*(int*) obj = nullptr; //只能在32位系统
        //     *(void**) obj = nullptr; // 32位和64位系统都兼容64位系统
        // }
        // else
        // {
        //     //头插
        //     *(void**) obj = _freeList;
        //     _freeList = obj;
        // }
        // 等价于下面写法
        *(void **)obj = _freeList;
        _freeList = obj;
    }
    ~ObjectPool()
    {
        if(!_memory)
        {
            free(_memory);
        }
        _memory = nullptr;
        _remainBytes = 0;
        _freeList = nullptr;
    }

private:
    // 为什么是char* 而不是void*？
    // 因为void* 无法确定大小，而char* 刚好一个字节，方便切分内存块 -- 例如直接++指针
    char *_memory = nullptr;   // 内存池
    size_t _remainBytes = 0;   // 剩余内存大小
    void *_freeList = nullptr; // 空闲内存链表
};

void testPool()
{
    ObjectPool<long long> pool;

    std::vector<long long *> vec;
    
    //测试性能 -- 时间
    clock_t start = clock();
    for(int i = 0; i < 50000; i++)
    {
        for(int j = 0; j < 10000; j++)
        {
            vec.push_back((long long*)pool.New());
        }
        for(int j = 0; j < 10000; j++)
        {
            pool.Delete(vec[j]);
        }
        vec.clear();
    }
    clock_t end = clock();
    cout << "pool time: " << end - start << endl;
}

void testMalloc()
{
    std::vector<long long*> vec;
    //测试性能 -- 时间
    clock_t start = clock();
    for(int i = 0; i < 50000; i++)
    {
        for(int j = 0; j < 10000; j++)
        {
            vec.push_back((long long*)malloc(sizeof(long long)));
        }
        for(int j = 0; j < 10000; j++)
        {
            free(vec[j]);
        }
        vec.clear();
    }
    clock_t end = clock();
    cout << "malloc time: " << end - start << endl;
}

int main()
{
    testPool();
    testMalloc();

    return 0;
}
```

## 高并发内存池整体框架

![image-20250814112522433](image-20250814112522433.png)
![image-20250814113546331](image-20250814113546331.png)

## thread cache

![image-20250814152515652](image-20250814152515652.png)

TLS--线程局部存储--无锁

## central cache--承上启下的作用 

 ![image-20250815161535036](image-20250815161535036.png)
![image-20250815161553257](image-20250815161553257.png)

关于预定义常量：WIN32、 _WIN32、 _WIN64：![image-20250815174341472](image-20250815174341472.png)

## page cache

![image-20250816113326841](image-20250816113326841.png)



递归互斥锁，C++的RAII的锁

基数树优化 -- Span_id 与 Span* 的map映射 -- 为什么要使用基数树优化map，因为map慢，map越慢，就会导致锁竞争更激烈；使用基数树，查找效率更搞，并且不用加锁！！！！

之前的map需要加锁的原因 -- （性能瓶颈）：
![image-20250819223643339](image-20250819223643339.png)

使用基数树不用加锁的原因：
基数树一次提前把空间开好（数组），不会随着多个线程的增删而改变整体的结构。 ![image-20250819224855288](image-20250819224855288.png)

读写分离：写的时候是申请和释放的时候（没有人用的时候）；读是有人用的时候。所以读写分离。

需要自己额外学习的内容：
Windows/Linux系统的申请内存系统调用
c++的锁（注意：递归互斥锁）
基数树
TLS

项目未完成：
导入基数树，看懂
使用性能测试工具测性能
