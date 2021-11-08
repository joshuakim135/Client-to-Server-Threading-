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
	DataRequest d(p, 0, 1);

	// delete received file for corresponding patient if it exists

	// push data to buffer
	for (int i = 0; i < n; i++) {
		vector<char> data((char*)&d, (char*)&d + sizeof(DataRequest));
		request_buffer->push(data);
		d.seconds += 0.004;
	}
}

struct Response {
	int p;
	double ecg;
};

// Parameters: filename, filelenght, Req Buffer reference, buffer capacity
void file_thread_function(string filename, BoundedBuffer* request_buffer, FIFORequestChannel* chan, size_t buffer_cap) {
	string filepath = "received/" + filename;
	FILE* fp = fopen(filepath.c_str(), "w");
	fclose(fp);

	int len = sizeof(FileRequest) + filename.size() + 1;
	char buffer[len];
	FileRequest* fr = new (buffer)FileRequest (0,0);
	strcpy(buffer+sizeof(FileRequest), filename.c_str());
	int64 rem;
	chan->cread(&rem, sizeof(rem));

	while (rem) {
		fr->length = min(rem, (int64)buffer_cap);
		vector<char> data (buffer, buffer + sizeof(buffer));
		request_buffer->push(data);
		rem -= fr->length;
		fr->offset += fr->length;
	}
	
	/*
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

	while (rem != 0) {
		frp->length = min(rem, (int64)buffer_cap);
		vector<char> data((char*)&buffer, (char*)&buffer + sizeof(DataRequest));
		request_buffer->push(data);
		frp->offset += frp->length;
		rem -= frp->length;
	}
	*/
}

// Parameter: Request Buffer reference, Histogram Buffer reference
void worker_thread_function(BoundedBuffer* request_buffer, BoundedBuffer* response_buffer, FIFORequestChannel* chan, size_t buffer_cap) {
	char response[buffer_cap];
	while(true) {
		vector<char> req = request_buffer->pop();
		char* request = (char*)req.data();
		chan->cwrite(request, req.size());
		Request* m = (Request*)request;
		if (m->getType() == DATA_REQ_TYPE) {
			int p = ((DataRequest*)request)->person;
			double ecgVal = 0;
			chan->cread(&ecgVal, sizeof(double));
			Response r {p, ecgVal};
			vector<char> data ((char*)&r, ((char*)&r) + sizeof(r));
			response_buffer->push(data);
		} else if (m->getType() == FILE_REQ_TYPE) {
			chan->cread(response, sizeof(response));
			FileRequest* fr = (FileRequest*) request;
			string file_name = (char*)(fr+1);
			file_name = "received/" + file_name;
			FILE* fp = fopen(file_name.c_str(), "r+");
			fseek (fp, fr->offset, SEEK_SET);
			fwrite(response, 1, fr->length, fp);
			fclose(fp);
		} else if (m->getType() == QUIT_REQ_TYPE) {
			break;
		}
	}
	
	/*
	int fperson;
	int rem;
	int readSize;

	while(true) {
		vector<char> req = request_buffer->pop();
		char* request = req.data();
		REQUEST_TYPE_PREFIX m = *(REQUEST_TYPE_PREFIX*) request;
		if (m == DATA_REQ_TYPE) {
			DataRequest* dr = (DataRequest*)request;
			double ecgVal;

			chan->cwrite(&dr, sizeof(DataRequest));
			chan->cread(&ecgVal, sizeof(double));

			pair<int, double> pairData (dr->person, ecgVal);
			vector<char> data((char*)&pairData, (char*)&pairData+sizeof(pair<int, double>));
			response_buffer->push(data);
		} else if(m == FILE_REQ_TYPE) {
			FileRequest* fr = (FileRequest*) request;
			string file_name = request + sizeof(FileRequest);
			const int len = sizeof(FileRequest) + file_name.size() + 1;
			chan->cwrite(request, len);
			char buffer[buffer_cap];
			
			fperson = open(("./received/" + filename).c_str(), O_CREAT | O_WRONLY | O_DSYNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
			
			rem = 0;
			while (rem < fr->length) {
				readSize = chan->cread(buffer, fr->length);
				rem += readSize;
				lseek(fperson, fr->offset, 0);
				write(fperson, buffer, readSize);
			}
			
		} else if (m == QUIT_REQ_TYPE) {
			Request q = *(Request*) request;
			chan->cwrite(&q, sizeof(Request));
			delete chan;
			break;
		}
	}
	*/
}

// Param: Histogram Collection reference, Histogram Buffer reference, Optional
void histogram_thread_function (HistogramCollection* hc, BoundedBuffer* response_buffer){
	while (true) {
		vector<char> response = response_buffer->pop();
		Response* r = (Response*)response.data();
		if (r->p == -1) {
			break;
		}
		hc->update (r->p, r->ecg);
	}
}

int main(int argc, char *argv[]){
	int opt;
	int n = 100; // number of requests per patient
	int p = 1; // number of patients from 1->15
	int w = 10; // default number of worker threads
	int b = 20; // capacity of request buffer
	int m = MAX_MESSAGE; // capacity of message buffer
	int h = 15; // number of histogram threads
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
	// FIFORequestChannel* workerChans[w];
	FIFORequestChannel* workerChans[w];
	for (int i = 0; i < w; i++) {
		Request nc(NEWCHAN_REQ_TYPE);
		char chanName[1024];
		chan.cwrite(&nc, sizeof(nc));	
		chan.cread(chanName, 1024);
		workerChans[i] = new FIFORequestChannel (chanName, FIFORequestChannel::CLIENT_SIDE);
	}
	
	struct timeval start, end;
    gettimeofday (&start, 0);

	// 1. create all threads
	thread patient[p]; // patient threads
	if (!reqFile) {
		for (int i = 0; i < p; i++) {
			patient[i] = thread(patient_thread_function, i+1, n, &request_buffer);
		}
	}
	thread workers[w]; // worker threads
	for (size_t i = 0; i < w; i++) {
		workers[i] = thread(worker_thread_function, &request_buffer, &response_buffer, workerChans[i], m, filename);
	}
	thread histograms[p]; // histogram threads
	for (size_t i = 0; i < h; i++) {
		histograms[i] = thread(histogram_thread_function, &hc, &response_buffer);
	}
	
	if (reqFile) { // file threads if necessary
		std::cout << "File Request\n" << endl;
		thread fileThread(file_thread_function, filename, &request_buffer, &chan, m);
		// join patient and file threads
		fileThread.join();
	}

	// 2. join patient and file threads
	for (size_t i = 0; i < p; i++) {
			patient[i].join();
	}

	// 3. push w quit messages to req buffer
	for (size_t i = 0; i < w; i++) {
		REQUEST_TYPE_PREFIX rtp = QUIT_REQ_TYPE;
		vector<char> quitmsg((char*)&rtp, ((char*)&rtp) + sizeof(rtp));
		request_buffer.push(quitmsg);
	}
	
	// 4/5. join worker threads, join histogram threads
	for (int i = 0; i < w; i++) {
		workers[i].join();
	}
	
	for (int i = 0; i < h; i++) {
		histograms[i].join();
	}
	
    gettimeofday (&end, 0);

    // print the results and time difference
	hc.print ();
    int secs = (end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)/(int) 1e6;
    int usecs = (int)(end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)%((int) 1e6);
    std::cout << "Took " << secs << " seconds and " << usecs << " micro seconds" << endl;
	
	// closing the channel    
    Request q (QUIT_REQ_TYPE);
    chan.cwrite (&q, sizeof (Request));
	// client waiting for the server process, which is the child, to terminate
	wait(0);
	
	std::cout << "Client process exited" << endl;
}
