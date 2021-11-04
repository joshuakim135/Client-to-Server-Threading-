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
	mutex m;
	condition_variable cv_bufferDataExists;
	condition_variable cv_bufferSpaceExists;

public:
	BoundedBuffer(int _cap) : cap(_cap) {}
	~BoundedBuffer(){}

	void push(vector<char> data, int size) {
		//1. Perform necessary waiting (by calling wait on the right semaphores and mutexes),
		unique_lock<mutex> l(m);
		cv_bufferSpaceExists.wait(l, [this]{return q.size() < cap;}); // stays asleep unless q.size() is less than buffer capacity
		
		//2. Push the data onto the queue
		q.push(data);
		
		//3. Do necessary unlocking and notification
		l.unlock();
		cv_bufferDataExists.notify_all();
	}

	vector<char> pop(char* buffer, int bufferCapacity){
		//1. Wait using the correct sync variables
		unique_lock<mutex> l(m);
		cv_bufferDataExists.wait(l, [this]{return q.size() > 0;});

		//2. Pop the front item of the queue and save to var
		vector<char> data = q.front();
		q.pop();
		
		//3. Unlock and notify using the right sync variables
		l.unlock();
		cv_bufferSpaceExists.notify_all();

		//4. Return the popped vector
		return data;
	}
};

#endif /* BoundedBuffer_ */
