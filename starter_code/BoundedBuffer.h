#ifndef BoundedBuffer_h
#define BoundedBuffer_h

#include <stdio.h>
#include <queue>
#include <string>
#include "Semaphore.h"

using namespace std;

class BoundedBuffer
{
private:
	int cap; // max number of bytes in the buffer
	queue<vector<char>> q;
	Semaphore emptySlots;
	Semaphore fullSlots;
	Semaphore mutx;


public:
	BoundedBuffer(int _cap): cap(_cap), emptySlots(_cap), fullSlots(0), mutx(1), q() {}	
	~BoundedBuffer(){}

	void push(char* data, int len){
		emptySlots.P();
		mutx.P();
		vector<char> d (data, data+len);
		q.push(d);
		mutx.V();
		fullSlots.V();
	}

	vector<char> pop(char* buf, int bufcap){
		fullSlots.P();
		mutx.P();
		vector<char> data = q.front();
		memcpy (buf, data.data(), data.size());
		q.pop();
		mutx.V();
		emptySlots.V();
		return data;
	}
};

#endif /* BoundedBuffer_ */
