//#include "ObjectPool.h"
//#include "ThreadCache.h"
//#include "ConcurrentAlloc.h"
//
//void TestObjectPool()
//{
//	nsObjectPool::ObjectPool<int> obPool;
//	std::vector<int*> v1, v2;
//	clock_t t1 = clock();
//	for (int i = 0; i < 1000; i++)
//	{
//		for (int j = 0; j < 100; j++)
//		{
//			v1.push_back(obPool.New());
//		}
//		for (int j = 0; j < 100; j++)
//		{
//			obPool.Delete(v1[0]);
//			v1.erase(v1.begin());
//		}
//	}
//	clock_t t2 = clock();
//	cout << "ObjectPool: " << t2 - t1 << endl;
//
//	t1 = clock();
//	size_t len = sizeof(int);
//	for (int i = 0; i < 1000; i++)
//	{
//		for (int j = 0; j < 100; j++)
//		{
//			v1.push_back((int*)malloc(len));
//		}
//		for (int j = 0; j < 100; j++)
//		{
//			free(v1[0]);
//			v1.erase(v1.begin());
//		}
//	}
//	t2 = clock();
//	cout << "malloc: " << t2 - t1 << endl;
//}
//
//void thread1()
//{
//	for (int i = 0; i < 10; i++)
//	{
//		void* ptr = ConcurrentAlloc(5);
//		ConcurrentFree(ptr, 5);
//	}
//}
//
//void thread2()
//{
//	for (int i = 0; i < 100; i++)
//	{
//		void* ptr = ConcurrentAlloc(5 * 1024);
//		ConcurrentFree(ptr, 5 * 1024);
//	}
//}
//
//void TestConcurrent()
//{
//	std::thread t1(thread1);	
//	t1.join();
//	std::thread t2(thread2);
//	t2.join();
//}
//
//int main()
//{
//	//TestObjectPool();
//	TestConcurrent();
//	
//	return 0;
//}