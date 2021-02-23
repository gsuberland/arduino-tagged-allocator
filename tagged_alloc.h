#pragma once

#include "pch.h"

/*
 * Tagged allocator. Tracks allocations with size, time, tag (4 chars), and the pointer.
 * Written by Graham Sutherland
 * Released under MIT license - see MIT-LICENSE.txt for full text.
 * https://github.com/gsuberland/arduino-tagged-allocator/
 * 
 */

/*
Example usage:

  TaggedAlloc::Init();
  SomeType* obj = TaggedAlloc::Allocate<SomeType>("abcd");
  float* array = TaggedAlloc::AllocateArray<float>(32, "FlAr");
  size_t numberOfActiveAllocations = TaggedAlloc::GetAllocationCount();
  size_t sizeOfAllocations = TaggedAlloc::GetTotalSize();
  TaggedAlloc::PrintStats();
  TaggedAlloc::Free(obj);
  TaggedAlloc::Free(array);

*/

/*
 * NOTE: 
 * The current memory allocation failure policy is to throw an assertion failure. This ensures quick clean failure in the case where something goes wrong.
 * This may not be ideal for all circumstances, e.g. speculative allocation of large buffers to see if there's enough memory to perform an action.
 * Future versions of this code will include a global policy option and per-call "don't panic" flag.
 */

/*
 * NOTE: This code requires some specific mutex features!
 * Put this in your %AppData%\Local\Arduino15\packages\esp32\hardware\esp32\platform.local.txt:

extras.defines=-DconfigSUPPORT_STATIC_ALLOCATION=1 -DconfigUSE_RECURSIVE_MUTEXES=1 -DCONFIG_SUPPORT_STATIC_ALLOCATION=1 -DconfigUSE_MUTEXES=1 -DconfigUSE_RECURSIVE_MUTEXES=1 -DconfigUSE_COUNTING_SEMAPHORES=1
build.defines={extras.defines}
compiler.cpp.extra_flags={extras.defines}
compiler.c.elf.extra_flags={extras.defines}
compiler.c.extra_flags={extras.defines}
compiler.S.extra_flags={extras.defines}

 * This enables the FreeRTOS mutex features needed for this code.
 */

/* 
 * WARNING:
 * Static mutex initialisation is currently broken for arduino-esp32, due to missing xQueueCreateMutexStatic.
 * See: https://github.com/espressif/arduino-esp32/issues/4851
 * This means that we must dynamically allocate a mutex at the Init() call. This means that Init() can fail if memory is very low when it is called.
 * For now this is just accepted, there's nothing we can do.
 * 
 */

/*****************
 * Configuration *
 *****************/

// the minimum size of the allocation table (in entires)
#ifndef TAGGED_ALLOC_MIN_TABLE_SIZE
#define TAGGED_ALLOC_MIN_TABLE_SIZE 32
#endif

// the initial allocation table size (in entries)
#ifndef TAGGED_ALLOC_INITIAL_TABLE_SIZE
#define TAGGED_ALLOC_INITIAL_TABLE_SIZE 64
#endif

// the number of excess (empty) entries we must have in the allocation table before it is shrunk
// set this to less than TAGGED_ALLOC_TABLE_EXPAND_STEP, so that there's some hysteresis between expansion and shrinking.
#ifndef TAGGED_ALLOC_TABLE_SHRINK_STEP
#define TAGGED_ALLOC_TABLE_SHRINK_STEP 64
#endif

// the number of entries that will be added to the allocation table once it is full
#ifndef TAGGED_ALLOC_TABLE_EXPAND_STEP
#define TAGGED_ALLOC_TABLE_EXPAND_STEP 32
#endif

// how long should the locking mutex around the allocation table wait for, before an assertion fail is thrown?
// 5ms is the default here. it really should not take that long to acquire a mutex!
#ifndef TAGGED_ALLOC_WAIT_TIME
#define TAGGED_ALLOC_WAIT_TIME (5 / portTICK_PERIOD_MS) 
#endif

// uncomment this if you want to save a little bit of memory (and some millis() calls) by not tracking the allocation time
//#define TAGGED_ALLOC_NO_TIME_TRACKING


/**********
 * Macros *
 **********/

// macro to check if a particular TaggedAllocationDescriptor is valid
#define TAGGED_ALLOC_IS_VALID(t) ((t).Object != nullptr)


/********************
 * Class definition *
 ********************/

class TaggedAlloc
{  
private:
  // internal descriptor struct for allocations
  struct TaggedAllocationDescriptor
  {
    void* Object;
    size_t Size;
    char Tag[4];
#ifndef TAGGED_ALLOC_NO_TIME_TRACKING
    uint32_t Time;
#endif
  };

  // this is set when Init() is called, to signify that we have initialised OK.
  static volatile bool InitOK;
  // number of active allocations that are present in the table.
  static volatile size_t AllocationCount;
  // the size (in entries, not bytes) of the allocation table.
  static volatile size_t AllocationTableSize;
  // the allocation table. this stores the allocation descriptors.
  static TaggedAllocationDescriptor* AllocationTable;
  // mutex for the allocation table.
  static SemaphoreHandle_t AllocationTableMutex;
  //static StaticSemaphore_t AllocationTableMutexStatic;


  // this sets the allocation time using millis()
  // you can turn the time tracking off by defining TAGGED_ALLOC_NO_TIME_TRACKING
  static inline void SetTaggedAllocationDescriptorTime(TaggedAllocationDescriptor* allocation) __attribute__((always_inline))
  {
#ifndef TAGGED_ALLOC_NO_TIME_TRACKING
    allocation->Time = millis();
#endif
  }
  
  static bool IsAllocationTableFragmented(size_t start, size_t* firstEmptyIndex, size_t* firstValidIndex);
  static void DefragAllocationTable();
  static bool GetFirstEmptySlot(size_t* index);
  static void ResizeAllocationTable(size_t entryCount);
  static void InsertAllocation(TaggedAllocationDescriptor ta);
  static void RemoveAllocation(void* objectPointer);
  
  template<typename T>
  static T* AllocateInternal(size_t count, char tag[4]);

public:
  // Initialise. This must be called at least once before any allocations can be performed.
  static void Init()
  {
    /*  Note that this is technically a race condition here, where if two calls are made to Init() at the same time (by two different cores) 
     *  and no call to Init() has been made before, we could end up initialising twice and making a mess of things.
     *  You can solve this by calling Init() in your setup function, rather than trying to lazily initialise in tasks or something weird.
     */
    if (InitOK)
      return;

    // normally I'd use static allocation here, but arduino-esp32 didn't include the static implementations.
    // see: https://github.com/espressif/arduino-esp32/issues/4851
    //AllocationTableMutex = xSemaphoreCreateRecursiveMutexStatic(&AllocationTableMutexStatic);
    AllocationTableMutex = xSemaphoreCreateRecursiveMutex();
    assert(AllocationTableMutex);
    
    size_t allocationBufferSize = AllocationTableSize * sizeof(TaggedAllocationDescriptor);
    AllocationTable = static_cast<TaggedAllocationDescriptor*>(malloc(allocationBufferSize));
    assert(AllocationTable);

    InitOK = true;
  }
  
  static size_t GetAllocationCount();

  static size_t GetTotalSize();
  
  static void PrintStats();

  template<typename T>
  static T* Allocate(char tag[4]);

  template<typename T>
  static T* AllocateArray(size_t count, char tag[4]);

  template<typename T>
  static void Free(T* object);
};


/************************
 * Static variable init *
 ************************/

volatile bool TaggedAlloc::InitOK = false;
volatile size_t TaggedAlloc::AllocationCount = 0;
volatile size_t TaggedAlloc::AllocationTableSize = TAGGED_ALLOC_INITIAL_TABLE_SIZE;
TaggedAlloc::TaggedAllocationDescriptor* TaggedAlloc::AllocationTable = nullptr;
SemaphoreHandle_t TaggedAlloc::AllocationTableMutex = nullptr;
//StaticSemaphore_t TaggedAlloc::AllocationTableMutexStatic;


/********************
 * Public functions *
 ********************/

// allocate a thing
template<typename T>
T* TaggedAlloc::Allocate(char tag[4])
{
  return AllocateInternal<T>(1, tag);
}


// allocate an array of things
template<typename T>
T* TaggedAlloc::AllocateArray(size_t count, char tag[4])
{
  return AllocateInternal<T>(count, tag);
}

// free the thing
template<typename T>
void TaggedAlloc::Free(T* object)
{
  // remove the allocation from the table, then free the object
  RemoveAllocation((void*)object);
  free(object);
}


// how many allocations do we have?
size_t TaggedAlloc::GetAllocationCount()
{
  assert(xSemaphoreTake(AllocationTableMutex, TAGGED_ALLOC_WAIT_TIME));
  
  volatile size_t count = AllocationCount;
  
  xSemaphoreGive(AllocationTableMutex);

  return count;
}


// what's the sum of the size of all the allocations?
size_t TaggedAlloc::GetTotalSize()
{
  assert(xSemaphoreTake(AllocationTableMutex, TAGGED_ALLOC_WAIT_TIME));
  
  size_t totalSize = 0;
  for (size_t index = 0; index < AllocationTableSize; index++)
  {
    if (TAGGED_ALLOC_IS_VALID(AllocationTable[index]))
    {
      totalSize += AllocationTable[index].Size;
    }
  }
  
  xSemaphoreGive(AllocationTableMutex);
  
  return totalSize;
}


// show some stats over serial output
void TaggedAlloc::PrintStats()
{
  Serial.println("*** TAGGED ALLOCATION STATS ***");
  Serial.println("> Capturing allocation table...");
  
  // capture a copy of the allocation table
  assert(xSemaphoreTake(AllocationTableMutex, TAGGED_ALLOC_WAIT_TIME));
  volatile size_t tableSize = AllocationTableSize * sizeof(TaggedAllocationDescriptor);
  volatile size_t allocCount = AllocationCount;
  TaggedAllocationDescriptor* allocationTableCopy = static_cast<TaggedAllocationDescriptor*>(malloc(tableSize));
  memcpy(allocationTableCopy, AllocationTable, tableSize);
  xSemaphoreGive(AllocationTableMutex);

  // print summary
  Serial.print("Allocation count: ");
  Serial.println(allocCount);
  Serial.print("Table size: ");
  Serial.print(allocCount);
  Serial.print(" (");
  Serial.print(allocCount * sizeof(TaggedAllocationDescriptor));
  Serial.println(" bytes)");

  // print allocations
  for (size_t index = 0; index < tableSize; index++)
  {
    TaggedAllocationDescriptor alloc = allocationTableCopy[index];
    if (TAGGED_ALLOC_IS_VALID(alloc))
    {
      Serial.print("Tag: ");
      Serial.write((uint8_t*)alloc.Tag, 4);
      Serial.print(", Size: ");
      Serial.print(alloc.Size);
#ifndef TAGGED_ALLOC_NO_TIME_TRACKING
      Serial.print(", Time: ");
      Serial.print(alloc.Time / 1000.0);
#endif
      Serial.print(", Pointer: 0x");
      uint32_t objectPtrValue = (uint32_t)alloc.Object;
      Serial.print(objectPtrValue, HEX);
      Serial.println("");
    }
  }
  
  free(allocationTableCopy);
}


/*********************
 * Private functions *
 *********************/

bool TaggedAlloc::GetFirstEmptySlot(size_t* index)
{
  assert(xSemaphoreTake(AllocationTableMutex, TAGGED_ALLOC_WAIT_TIME));
  
  bool result = false;
  for (size_t n = 0; n < AllocationTableSize; n++)
  {
    if (!TAGGED_ALLOC_IS_VALID(AllocationTable[n]))
    {
      *index = n;
      result = true;
      break;
    }
  }
  
  xSemaphoreGive(AllocationTableMutex);
  return result;
}


/*bool TaggedAlloc::GetNextValidEntry(size_t start, size_t* index)
{
  assert(index);
  
  assert(xSemaphoreTake(AllocationTableMutex, TAGGED_ALLOC_WAIT_TIME));

  assert(*index < AllocationTableSize);
  
  bool result = false;
  for (size_t n = *index; n < AllocationTableSize; n++)
  {
    if (TAGGED_ALLOC_IS_VALID(AllocationTable[n]))
    {
      *index = n;
      result = true;
      break;
    }
  }
  
  xSemaphoreGive(AllocationTableMutex);
  return result;
}*/

// this checks to see if the allocation table is fragmented, i.e. not all of the allocations are contiguously at the top of the table.
// start sets where we start looking in the table, which is useful for repeated calls during defragmenting (because we know how many contiguous entries we have)
// firstEmptyIndex is a pointer to a size_t that receives the first index in the table that is empty (does not contain an allocation), if there is one.
// firstValidIndex is a pointer to a size_t that receives the first index in the table that contains a valid allocation, if there is one.
// returns true if the table is fragmented, otherwise false.
bool TaggedAlloc::IsAllocationTableFragmented(size_t start, size_t* firstEmptyIndex, size_t* firstValidIndex)
{
  assert(firstEmptyIndex);
  assert(firstValidIndex);
  
  assert(xSemaphoreTake(AllocationTableMutex, TAGGED_ALLOC_WAIT_TIME));

  assert(start < AllocationTableSize);

  bool reachedInvalidEntry = false;
  bool fragmented = false;
  TaggedAllocationDescriptor* entry = AllocationTable;
  int index = 0;
  for (size_t index = start; index < AllocationTableSize; index++)
  {
    bool validEntry = TAGGED_ALLOC_IS_VALID(*entry);
    if (validEntry && reachedInvalidEntry)
    {
      // we reached a valid entry after reaching an invalid entry, so the table is fragmented
      *firstValidIndex = index;
      fragmented = true;
      break;
    }
    if (!validEntry)
    {
      // reached a invalid entry, mark it and continue.
      if (!reachedInvalidEntry)
      {
        *firstEmptyIndex = index;
        reachedInvalidEntry = true;
      }
    }
    entry++;
  }
  
  xSemaphoreGive(AllocationTableMutex);
  return fragmented;
}


// defragments the allocation table, shifting all descriptors to the top of the table.
void TaggedAlloc::DefragAllocationTable()
{
  assert(xSemaphoreTake(AllocationTableMutex, TAGGED_ALLOC_WAIT_TIME));

  size_t firstEmptyIndex = 0;
  size_t firstValidIndex = 0;
  // shift allocations down while we detect fragmentation
  // this is somewhere between O(n log n) and O(n^2), but n is small enough that it should be ok, and we only do this during table shrinkage anyway.
  while (IsAllocationTableFragmented(firstEmptyIndex, &firstEmptyIndex, &firstValidIndex))
  {
    // move the first valid allocation into the first empty slot
    AllocationTable[firstEmptyIndex] = AllocationTable[firstValidIndex];
    AllocationTable[firstValidIndex] = { 0 };
  }
  
  xSemaphoreGive(AllocationTableMutex);
}


// resizes the allocation table to the given size.
void TaggedAlloc::ResizeAllocationTable(size_t newEntryCount)
{
  assert(newEntryCount < TAGGED_ALLOC_MIN_TABLE_SIZE);
  
  assert(xSemaphoreTake(AllocationTableMutex, TAGGED_ALLOC_WAIT_TIME));

  if (newEntryCount != AllocationTableSize)
  {
    if (newEntryCount < AllocationTableSize)
    {
      // We're shrinking the table. Need to defrag it first!
      DefragAllocationTable();
    }
    size_t newSize = newEntryCount * sizeof(TaggedAllocationDescriptor);
    AllocationTable = static_cast<TaggedAllocationDescriptor*>(realloc(AllocationTable, newSize));
    assert(AllocationTable != nullptr);
    AllocationTableSize = newSize;
  }
  
  xSemaphoreGive(AllocationTableMutex);
}


// inserts a new TaggedAllocationDescriptor object into the allocation table, resizing if necessary.
void TaggedAlloc::InsertAllocation(TaggedAllocationDescriptor ta)
{
  assert(xSemaphoreTake(AllocationTableMutex, TAGGED_ALLOC_WAIT_TIME));
  
  size_t insertIndex = 0;
  if (!GetFirstEmptySlot(&insertIndex))
  {
    // the table is full, need to resize it.
    ResizeAllocationTable(AllocationTableSize + TAGGED_ALLOC_TABLE_EXPAND_STEP);
  }
  if (!GetFirstEmptySlot(&insertIndex))
  {
    // critical failure, we just resized the buffer and it still didn't find an empty slot.
    assert(false);
  }
  AllocationTable[insertIndex] = ta;
  AllocationCount++;
    
  xSemaphoreGive(AllocationTableMutex);
}


// finds an object in the allocation table, via its pointer, and removes it
// this is called by Free()
void TaggedAlloc::RemoveAllocation(void* objectPointer)
{
  assert(xSemaphoreTake(AllocationTableMutex, TAGGED_ALLOC_WAIT_TIME));
  
  for (size_t n = 0; n < AllocationTableSize; n++)
  {
    if (TAGGED_ALLOC_IS_VALID(AllocationTable[n]))
    {
      if (AllocationTable[n].Object == objectPointer)
      {
        // clear allocation
        AllocationTable[n] = { 0 };
        break;
      }
    }
  }
  AllocationCount--;

  // Have we removed enough allocations to justify shrinking the table, as long as we wouldn't be shrinking it too much?
  if ((AllocationCount > TAGGED_ALLOC_MIN_TABLE_SIZE) && 
     ((AllocationCount + TAGGED_ALLOC_TABLE_SHRINK_STEP) < AllocationTableSize))
  {
    size_t shrunkSize = AllocationTableSize - TAGGED_ALLOC_TABLE_SHRINK_STEP;
    ResizeAllocationTable(shrunkSize);
  }
  
  xSemaphoreGive(AllocationTableMutex);
}


// generic allocation function that actually builds the allocation descriptor
template<typename T>
T* TaggedAlloc::AllocateInternal(size_t count, char tag[4])
{
  // create a descriptor
  TaggedAllocationDescriptor ta;
  // set the time (this is inlined, and does nothing if the TAGGED_ALLOC_NO_TIME_TRACKING preprocessor flag is set
  SetTaggedAllocationDescriptorTime(&ta);
  ta.Size = sizeof(T) * count;
  // copy tag
  ta.Tag[0] = tag[0];
  ta.Tag[1] = tag[1];
  ta.Tag[2] = tag[2];
  ta.Tag[3] = tag[3];
  // allocate object, and throw an assertion fail if the malloc() call fails
  ta.Object = malloc(ta.Size);
  assert(ta.Object);
  // insert the descriptor into the allocation table
  InsertAllocation(ta);
  // done :)
  return (T*)ta.Object;
}
