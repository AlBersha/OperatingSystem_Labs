#include "allocator.h"
#include <iostream>
#include <algorithm>
#include <iomanip>

uint8_t Allocator::memory[MEMORY_SIZE];
const size_t Allocator::pageNum;
map<void*, PageHeader> Allocator::pageHeaders;
vector<void*> Allocator::freePages;
map<int, vector<void*>> Allocator::classifiedPages;

Allocator::Allocator() 
{
	initializePageHeaders();
	initializeFreePages();
	initializeClassifiedPages();
}

void Allocator::initializePageHeaders() 
{
	uint8_t* pagePtr = (uint8_t*)memory;

	for (int i = 0; i < pageNum; i++)
	{
		PageHeader header = { Free, 0, 0, NULL };
		pageHeaders.insert({pagePtr, header});
		pagePtr += PAGE_SIZE;
	}
}

void Allocator::initializeFreePages() 
{
	uint8_t* pagePtr = (uint8_t*)memory;

	for (int i = 0; i < pageNum; i++)
	{
		freePages.push_back(pagePtr);
		pagePtr += PAGE_SIZE;
	}
}

void Allocator:: initializeClassifiedPages()
{
	int classSize = 8;

	for (; classSize <= PAGE_SIZE / 2; classSize <<= 1)
	{
		classifiedPages.insert({ classSize, {} });
	}
}

size_t Allocator::roundToPowerOfTwo(size_t size)
{
	size_t powerOfTwo = 1;

	while (powerOfTwo < size) 
	{
		powerOfTwo <<= 1;
	}

	return powerOfTwo;
}

bool Allocator::dividePageIntoBlocks(size_t classSize) 
{
	if (freePages.empty())
	{
		return false;
	}

	uint8_t* pagePtr = (uint8_t*)freePages[0];
	freePages.erase(freePages.begin());

	uint8_t* currPtr = (uint8_t*)pagePtr;

	while (currPtr != pagePtr + PAGE_SIZE) 
	{
		*currPtr = true;
		currPtr += classSize;
	}

	pageHeaders[pagePtr].state = DividedIntoBlocks;
	pageHeaders[pagePtr].classSize = classSize;
	pageHeaders[pagePtr].blocksAmount = PAGE_SIZE / classSize;
	pageHeaders[pagePtr].freeBlockPtr = pagePtr;

	classifiedPages[classSize].push_back(pagePtr);

	return true;
}

void* Allocator::findFreeBlock(uint8_t* pagePtr, size_t blockSize)
{
	uint8_t* currPtr = pagePtr;

	while (currPtr != pagePtr + PAGE_SIZE) 
	{
		if ((bool)*currPtr) 
		{
			return currPtr;
		}

		currPtr += blockSize;
	}

	return NULL;
}

size_t Allocator::calculateNumOfPage(size_t size) 
{
	size_t remainderOfDivision = size % PAGE_SIZE;
	size_t neededNumOfPage = size / PAGE_SIZE + (remainderOfDivision == 0 || size <= PAGE_SIZE / 2 ? 0 : 1);
	return neededNumOfPage;
}


bool Allocator::isValid(void* address)
{
	if (address < memory || address > memory + PAGE_SIZE * pageNum)
	{
		return false;
	}

	size_t pageNumber = ((uint8_t*)address - memory) / PAGE_SIZE;
	uint8_t* pagePtr = memory + pageNumber * PAGE_SIZE;
	bool isValid = false;

	if (pageHeaders[pagePtr].state == DividedIntoBlocks)
	{
		uint8_t* currBlock = pagePtr;
		for (int i = 0; i < PAGE_SIZE / pageHeaders[pagePtr].classSize; i++)
		{
			if (currBlock == address)
			{
				isValid = true;
				break;
			}
			currBlock += pageHeaders[pagePtr].classSize;
		}
	}
	else if (pageHeaders[pagePtr].state == MultiPageBlock)
	{
		size_t pageNumInBlock = pageHeaders[pagePtr].classSize / PAGE_SIZE;
		if (pageHeaders[pagePtr].blocksAmount + 1 == pageNumInBlock)
		{
			isValid = pagePtr == address;
		}
	}

	return isValid;
}

void* Allocator::mem_alloc(size_t size)
{
	void* ptr = NULL;

	if (size <= PAGE_SIZE / 2) 
	{
		//size incremented, because 1 byte needed to header, which consist bool flag(determines block is free or not)
		size_t classSize = roundToPowerOfTwo(size + 1);

		if (classifiedPages[classSize].empty()) 
		{
			if (!dividePageIntoBlocks(classSize))
			{
				cout << "### All memory is occupied! There is no page with corresponding class! ###";
				return NULL;
			}
		}

		uint8_t* pagePtr = (uint8_t*)classifiedPages[classSize].at(0);
		uint8_t* allocBlock = pageHeaders[pagePtr].freeBlockPtr;

		*allocBlock = false;
		pageHeaders[pagePtr].blocksAmount -= 1;

		if (pageHeaders[pagePtr].blocksAmount > 0) 
		{
			pageHeaders[pagePtr].freeBlockPtr = (uint8_t*)findFreeBlock(pagePtr, classSize);
		}
		else 
		{
			auto iterator = find(classifiedPages[classSize].begin(), classifiedPages[classSize].end(), pagePtr);
			classifiedPages[classSize].erase(iterator);
		}
		cout << "mem_alloc(" << size << ") - Allocate block from class " << classSize << endl;
		ptr = allocBlock;
	}
	else 
	{
		size_t neededNumOfPage = calculateNumOfPage(size);

		if (freePages.size() < neededNumOfPage)
		{
			cout << "### Not enough memory to allocate! ###";
			return NULL;
		}

		ptr = freePages[0];

		for (int i = 1; i <= neededNumOfPage; i++)
		{
			uint8_t* pagePtr = (uint8_t*)freePages[0];
			uint8_t* nextPagePtr = neededNumOfPage == i ? NULL : (uint8_t*)freePages[1];

			pageHeaders[pagePtr].state = MultiPageBlock;
			pageHeaders[pagePtr].classSize = neededNumOfPage * PAGE_SIZE;
			pageHeaders[pagePtr].blocksAmount = neededNumOfPage - i;
			pageHeaders[pagePtr].freeBlockPtr = nextPagePtr;

			freePages.erase(freePages.begin());
		}

		cout << "mem_alloc(" << size << ") - Allocate " << neededNumOfPage << " pages" << endl;
	}

	return ptr;
}

void* Allocator::mem_realloc(void* address, size_t size)
{
	if (address == NULL) 
	{
		return mem_alloc(size);
	}

	if (!isValid(address)) 
	{
		cout << "### Incorrect address! ###";
		return address;
	}

	void* ptr = address;
	size_t pageNumber = ((uint8_t*)address - memory) / PAGE_SIZE;
	uint8_t* pagePtr = memory + pageNumber * PAGE_SIZE;

	if (pageHeaders[pagePtr].state == MultiPageBlock) 
	{
		size_t oldSize = pageHeaders[pagePtr].classSize;
		size_t oldPageNum = pageHeaders[pagePtr].blocksAmount + 1;
		size_t newPageNum = calculateNumOfPage(size);

		if (oldPageNum == newPageNum) 
		{
			ptr = address;
		}
		else if (oldPageNum > newPageNum) 
		{
			uint8_t* currPage = pagePtr;
			uint8_t* nextPage;
			for (int i = 0; i < oldPageNum; i++)
			{
				nextPage = pageHeaders[currPage].freeBlockPtr;
				if (i >= newPageNum) 
				{
					pageHeaders[currPage].state = Free;
					pageHeaders[currPage].classSize = 0;
					pageHeaders[currPage].blocksAmount = 0;
					pageHeaders[currPage].freeBlockPtr = NULL;

					freePages.push_back(currPage);
				}
				else 
				{
					pageHeaders[currPage].classSize = newPageNum * PAGE_SIZE;
					pageHeaders[currPage].blocksAmount = newPageNum - i - 1;
					pageHeaders[currPage].freeBlockPtr = newPageNum - 1 == i ? NULL : nextPage;
				}
				currPage = nextPage;
			}

			sort(freePages.begin(), freePages.end());

			if (size <= PAGE_SIZE / 2) 
			{
				ptr = mem_alloc(size);
				for (int i = 0; i < size; i++)
				{
					*((uint8_t*)ptr + i + 1) = *((uint8_t*)address + i);
				}
			}
		}
		else if (oldPageNum < newPageNum) 
		{
			size_t neededPage = newPageNum - oldPageNum;
			if (freePages.size() < neededPage) 
			{
				cout << "### Not enough memory to reallocate! ###";
				return address;
			}

			uint8_t* currPage = pagePtr;
			for (int i = 1; i <= newPageNum; i++)
			{
				if (i >= oldPageNum)
				{
					pageHeaders[currPage].freeBlockPtr = newPageNum == i ? NULL : (uint8_t*)freePages[0];
					freePages.erase(freePages.begin());
				}
				pageHeaders[currPage].state = MultiPageBlock;
				pageHeaders[currPage].classSize = newPageNum * PAGE_SIZE;
				pageHeaders[currPage].blocksAmount = newPageNum - i;

				currPage = pageHeaders[currPage].freeBlockPtr;
			}
			ptr = address;
		}
	}
	else if (pageHeaders[pagePtr].state == DividedIntoBlocks) 
	{
		size_t classSize = roundToPowerOfTwo(size);
		if (pageHeaders[pagePtr].classSize == classSize) 
		{
			ptr = address;
		}
		else 
		{
			ptr = mem_alloc(size);
			if (ptr) {
				for (int i = 0; i < classSize; i++)
				{
					*((uint8_t*)ptr + i) = *((uint8_t*)address + i);
				}
			}
			mem_free(address);
		}
	}
	return ptr;
}

void Allocator::mem_free(void* address) 
{
	if (!isValid(address))
	{
		cout << "### Incorrect address! ###";
		return;
	}

	if (freePages.size() == pageNum) 
	{
		cout << "### All memory is free! ###";
		return;
	}

	size_t pageNumber = ((uint8_t*)address - memory) / PAGE_SIZE;
	uint8_t* pagePtr = memory + pageNumber * PAGE_SIZE;
	size_t numOfPage = pageHeaders[pagePtr].blocksAmount + 1;

	if (pageHeaders[pagePtr].state == MultiPageBlock) 
	{
		uint8_t* currPage = pagePtr;
		for (int i = 0; i < numOfPage ; i++) 
		{
			uint8_t* nextPage = pageHeaders[currPage].freeBlockPtr;

			pageHeaders[currPage].state = Free;
			pageHeaders[currPage].classSize = 0;
			pageHeaders[currPage].blocksAmount = 0;
			pageHeaders[currPage].freeBlockPtr = NULL;

			freePages.push_back(currPage);

			currPage = nextPage;
		}
		sort(freePages.begin(), freePages.end());
	}
	else if (pageHeaders[pagePtr].state == DividedIntoBlocks) 
	{
		size_t classSize = pageHeaders[pagePtr].classSize;
		uint8_t* currBlock = pagePtr;

		for (int i = 0; i < PAGE_SIZE / classSize; i++) 
		{
			if (currBlock == address) 
			{
				*(uint8_t*)address = true;
				break;
			}
			currBlock += classSize;
		}
		pageHeaders[pagePtr].blocksAmount += 1;

		if (pageHeaders[pagePtr].blocksAmount == PAGE_SIZE / classSize) 
		{
			pageHeaders[pagePtr] = { Free, 0, 0, NULL };
			freePages.push_back(pagePtr);
			sort(freePages.begin(), freePages.end());

			auto iterator = find(classifiedPages[classSize].begin(), classifiedPages[classSize].end(), pagePtr);
			classifiedPages[classSize].erase(iterator);		
		}
		if (pageHeaders[pagePtr].blocksAmount == 1) 
		{
			classifiedPages[classSize].push_back(pagePtr);
			sort(classifiedPages[classSize].begin(), classifiedPages[classSize].end());
		}
	}
	else 
	{
		cout << "### This address is already free! ###";
	}
}

void Allocator::mem_dump() 
{
	uint8_t* currPage = memory;

	cout << "### mem_dump ###" << endl;
	cout << "============================================" << endl;

	for (int i = 0; i < pageNum; i++)
	{
		PageHeader header = pageHeaders[currPage];

		string state;
		switch (header.state)
		{
		case Free: 
			state = "Free";
			break;
		case DividedIntoBlocks:
			state = "DividedIntoBlocks";
			break;
		case MultiPageBlock:
			state = "MultiPageBlock";
			break;
		}

		int paddingLeft = 2 + (i < 9 ? 1 : 0);
		cout << "Page" << setw(paddingLeft) << "#" << i + 1;
		cout << ". Address: " << (uint16_t*)currPage;
		cout << ". PageState: " << state;
		if (header.state == DividedIntoBlocks)
		{
			cout << ". ClassSize: " << header.classSize << endl << endl;

			uint8_t* currBlock = currPage;
			for (int j = 0; j < PAGE_SIZE / header.classSize; j++)
			{
				paddingLeft = 2 + (j < 9 ? 1 : 0);
				cout << "\tBlock" << setw(paddingLeft) << "#" << j + 1;
				cout << ". Address:" << (uint16_t*)currBlock;
				cout << ". IsFree: " << (bool)*currBlock << endl;
				currBlock += header.classSize;
			}
			cout << endl;
		}
		else if (header.state == MultiPageBlock) 
		{
			cout << ". BlockSize: " << header.classSize;
			cout << ". Part #" << header.classSize / PAGE_SIZE - header.blocksAmount;
			cout << ". NextPage: " << (uint16_t*)header.freeBlockPtr << endl;
		}
		else 
		{
			cout << ". PageSize: " << PAGE_SIZE << endl;
		}

		currPage += PAGE_SIZE;
	}
	cout << "============================================" << endl;
}