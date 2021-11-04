#include "common.h"
#include "FIFOreqchannel.h"
#include "BoundedBuffer.h"
#include "HistogramCollection.h"
#include <sys/wait.h>
#include <thread>
#include <utility>

using namespace std;

void patient_thread_function(int p, int n, BoundedBuffer* request_buffer){
	// create a data request
	DataRequest d(p, 0.0, 1);

	// convert DataRequest to vector<char>
	vector<char> data((char*)&d, (char*)&d + sizeof(DataRequest));

	for (int i = 0; i < n; i++) {
		request_buffer->push(data, sizeof(DataRequest));
		d.seconds += 0.004; // this will update d.seconds so it pulls the next ECG val
	}
}

// Parameters: filename, filelenght, Req Buffer reference, buffer capacity
void file_thread_function(string filename, BoundedBuffer* request_buffer, FIFORequestChannel* chan, size_t buffer_cap) {
	string filepath = "received/" + filename;
	int len = sizeof(FileRequest) + filename.size() + 1;
	char buffer[len];
	FileRequest fr(0,0);
	memcpy(buffer, &fr, sizeof(FileRequest));
	strcpy(buffer + sizeof(FileRequest), filename.c_str());
	chan->cwrite(buffer, len);
	int64 filelen;
	chan->cread(&filelen, sizeof(int64));

	FILE* f = fopen(filepath.c_str(), "w");
	fseek(f, filelen, SEEK_SET);

	FileRequest* frp = (FileRequest*)buffer;
	int64 rem = filelen;
	/*
	int bytesLeft = f->length;
	// char recvBuf [f->length];
	while (bytesLeft != 0) {
		char recvBuf[min(f->length, MAX_PIPE_SIZE)];
		int bytesRead = chan.cread(recvBuf, bytesLeft);
		of.write(recvBuf, min(bytesLeft, MAX_PIPE_SIZE));
		std::cout << "bytesRead: " << bytesRead << endl;
		f->offset += bytesRead;
		bytesLeft -= bytesRead;
		std::cout << "bytesLeft: " << bytesLeft << endl;
	}
	*/
	while (rem != 0) {
		frp->length = min(rem, (int64)buffer_cap);
		vector<char> data((char*)&buffer, (char*)&buffer + sizeof(DataRequest));
		request_buffer->push(data, len);
		frp->offset += frp->length;
		rem -= frp->length;
	}
}

// Parameter: Request Buffer reference, Histogram Buffer reference
void worker_thread_function(BoundedBuffer* request_buffer, BoundedBuffer* response_buffer, FIFORequestChannel* chan, HistogramCollection* hc, size_t buffer_cap) {
	char buffer[1024];
	char recevBuffer[buffer_cap];
	double ecgVal = 0.0;

	while(true) {
		request_buffer->pop(buffer, 1024);

		// if patient packet, push response to histogram bufer
		if ((*(REQUEST_TYPE_PREFIX*)buffer) == DATA_REQ_TYPE) {
			chan->cwrite(buffer, sizeof(DataRequest));
			chan->cread(&ecgVal, sizeof(double)); // response_buffer instead of ecgVal
			// pair<int, double> peVal = make_pair(((DataRequest*)buffer)->person, ecgVal);
			hc->update(((DataRequest*)buffer)->person, ecgVal);
			// vector<char> data((char*)&response_buffer, (char*)&response_buffer + sizeof(DataRequest));
			// response_buffer->push(data, sizeof(DataRequest));
		}
		
		// if file transfer, write to file
		else if ((*(REQUEST_TYPE_PREFIX*)buffer) == FILE_REQ_TYPE) {
			FileRequest* fr = (FileRequest*)buffer;
			string filename = (char*)(fr + 1);
			chan->cwrite(buffer, sizeof(FileRequest) + filename.size() + 1);
			chan->cread(recevBuffer, sizeof(buffer_cap));

			string filename2 = "receive/" + filename;
			FILE* f = fopen(filename2.c_str(), "r+");
			fseek(f, fr->offset, SEEK_SET);
			fwrite(recevBuffer, 1, fr->length, f);
			fclose(f);
		}

		// if quit exit thread/function
		else if ((*(REQUEST_TYPE_PREFIX*)buffer) == QUIT_REQ_TYPE) {
			chan->cwrite(buffer, sizeof(REQUEST_TYPE_PREFIX)); // i dunno if size is correct
			delete chan;
			break;
		}
	}
}

// Param: Histogram Collection reference, Histogram Buffer reference, Optional
void histogram_thread_function (HistogramCollection* hc, BoundedBuffer* response_buffer){
	char buffer[1024];
	char recevBuffer[MAX_MESSAGE]; // change MAX_MESSAGE to int val from user input
	double ecgVal = 0.0;

	while(true) {
		response_buffer->pop(buffer, 1024);
		hc->update(((DataRequest*)buffer)->person, ecgVal);
	}
}

FIFORequestChannel* createChannels(FIFORequestChannel* mainchan) {
	Request nc (NEWCHAN_REQ_TYPE);
	mainchan->cwrite(&nc, sizeof(nc));
	char chanName[1024];
	mainchan->cread(chanName, sizeof(chanName));
	
	FIFORequestChannel* newchan = new FIFORequestChannel(chanName, FIFORequestChannel::CLIENT_SIDE);
	return newchan;
}

int main(int argc, char *argv[]){
	int opt;
	int n = 100; // number of requests per patient
	int p = 1; // number of patients from 1->15
	int w = 100; // default number of worker threads
	int b = MAX_MESSAGE; // capacity of request buffer
	int m = MAX_MESSAGE; // capacity of message buffer
	int h = 100; // number of histogram threads
	double t = 0.0;
	int e = 1;
	string filename = "";
	bool reqFile = false;
	// take all the arguments first because some of these may go to the server
	// n-> # of req per patient, p-> # of patients, w-> # worker threads, b-> request buffer cap
	// m-> receive message buffer cap, f-> file
	while ((opt = getopt(argc, argv, "n:p:w:b:m:f:")) != -1) {
		switch (opt) {
			case 'n':
				n = atoi(optarg);
				break;
			case 'p':
				p = atoi(optarg);
				break;
			case 'w':
				w = atoi(optarg);
				break;
			case 'b':
				b = atoi(optarg);
				break;
			case 'm':
				m = atoi(optarg);
				break;
			case 'f':
				filename = optarg;
				reqFile = true;
				break;
		}
	}

	int pid = fork ();
	if (pid < 0){
		EXITONERROR ("Could not create a child process for running the server");
	}
	if (!pid){ // The server runs in the child process
		char* args[] = {"./server", nullptr};
		if (execvp(args[0], args) < 0){
			EXITONERROR ("Could not launch the server");
		}
	}
	FIFORequestChannel chan ("control", FIFORequestChannel::CLIENT_SIDE);
	BoundedBuffer request_buffer(b);
	BoundedBuffer response_buffer(b);
	HistogramCollection hc;

	for (int i = 0; i < p; i++) { // initialize histogram collection
		Histogram* h = new Histogram(10, -2.0, 2.0);
		hc.add(h);
	}

	// worker channels = # worker threads
	FIFORequestChannel* workerChans[w];
	for (int i = 0; i < w; i++) {
		workerChans[i] = createChannels(&chan);
	}
	
	struct timeval start, end;
    gettimeofday (&start, 0);

	// 1. Create all your threads
	// 2. Join patient and file threads
	// 3. push w quit messages to req buffer
	// 4. join worker threads
	// 5. join histogram threads

	// create all threads
	// patient threads
	thread patient[p];
	for (int i = 0; i < p; i++) {
		patient[i] = thread(patient_thread_function, i+1, n, &request_buffer);
	}

	// worker threads
	thread workers[w];
	for (size_t i = 0; i < w; i++) {
		workers[i] = thread(worker_thread_function, &request_buffer, &response_buffer, workerChans[i], &hc, m);
	}

	if (reqFile) {
		cout << "File Request\n" << endl;
		thread fileThread(file_thread_function, filename, &request_buffer, &chan, m);
		// join patient and file threads
		fileThread.join();
	} else {
		// join patient and file threads
		for (size_t i = 0; i < p; i++) {
			patient[i].join();
		}
	}

	// push w quit messages to req buffer
	for (size_t i = 0; i < w; i++) {
		REQUEST_TYPE_PREFIX rtp = QUIT_REQ_TYPE;
	}

	for (int i = 0; i < w; i++) {
		
	}

	// how to ensure quit is last
	// join patient and worker thread to make sure they are done and then pass quit
    gettimeofday (&end, 0);

    // print the results and time difference
	hc.print ();
    int secs = (end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)/(int) 1e6;
    int usecs = (int)(end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)%((int) 1e6);
    cout << "Took " << secs << " seconds and " << usecs << " micro seconds" << endl;
	
	// closing the channel    
    Request q (QUIT_REQ_TYPE);
    chan.cwrite (&q, sizeof (Request));
	// client waiting for the server process, which is the child, to terminate
	wait(0);
	cout << "Client process exited" << endl;
}
