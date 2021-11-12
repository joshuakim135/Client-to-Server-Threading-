#ifndef BoundedBuffer_h
#define BoundedBuffer_h

#include <stdio.h>
#include <queue>
#include <string>
#include <mutex>
#include "Semaphore.h"

using namespace std;

class BoundedBuffer
{
private:
	int cap; // max number of items in the buffer
	queue<vector<char>> q;	/* the queue of items in the buffer. Note
	that each item a sequence of characters that is best represented by a vector<char> because: 
	1. An STL std::string cannot keep binary/non-printables
	2. The other alternative is keeping a char* for the sequence and an integer length (i.e., the items can be of variable length), which is more complicated.*/
	// add necessary synchronization variables (e.g., sempahores, mutexes) and variables
	Semaphore emptyslots;
	Semaphore fullslots;
	Semaphore mt;

public:
	BoundedBuffer(int _cap) : cap(_cap), emptyslots(_cap), fullslots(0), mt(1), q() {}
	~BoundedBuffer(){}

	void push(vector<char> data) {
		emptyslots.P();
		mt.P();
		q.push(data);
		mt.V();
		fullslots.V();
	}

	vector<char> pop(){
		fullslots.P();
		mt.P();
		vector<char> data = q.front();
		q.pop();
		mt.V();
		emptyslots.V();
		return data;
	}
};

#endif /* BoundedBuffer_ */
