# Arduino Tagged Allocator

A memory allocator wrapper for Arduino/FreeRTOS that keeps a table of descriptors and tags for active allocations, similar to ExAllocatePoolWithTag in the Windows kernel.

```cpp
TaggedAlloc::Init();
SomeType* obj = TaggedAlloc::Allocate<SomeType>("abcd");
float* array = TaggedAlloc::AllocateArray<float>(32, "FlAr");
size_t numberOfActiveAllocations = TaggedAlloc::GetAllocationCount();
size_t sizeOfAllocations = TaggedAlloc::GetTotalSize();
TaggedAlloc::PrintStats();
TaggedAlloc::Free(obj);
TaggedAlloc::Free(array);
```

Example stats output:

```
*** TAGGED ALLOCATION STATS ***
> Capturing allocation table...
Allocation count: 2
Table size: 64 (1024 bytes)
Tag: abcd, Size: 12, Time: 0.0, Pointer: 0x23450
Tag: FlAr, Size: 512, Time: 0.0, Pointer: 0x23460
```
