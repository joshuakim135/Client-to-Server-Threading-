#include "common.h"
#include "FIFOreqchannel.h"
#include "BoundedBuffer.h"
#include "HistogramCollection.h"
#include <sys/wait.h>
#include <thread>

using namespace std;

struct getResponse{
	int patient;
	double ecgValue;
};

void patient_thread_function(int patientNum, int size, BoundedBuffer* request_buffer){
	DataRequest d(patientNum, 0, 1);
	
	for (int i = 0; i < size;i++){
		vector<char> data((char*)&d, (char*)&d + sizeof(DataRequest));
		request_buffer->push(data);
		d.seconds = d.seconds + 0.004;
	}
}

void file_thread(string fileName, BoundedBuffer* requestBuff, FIFORequestChannel* chan, size_t bufferCap){
	string filepath = "received/" + fileName;
	FILE* fp = fopen(filepath.c_str(), "w");
	fclose(fp);
	int64 remainder;
	int length = sizeof(FileRequest) + fileName.size() + 1;
	char buffer[length];

	FileRequest* fileR = new (buffer)FileRequest (0,0);
	strcpy(buffer + sizeof(FileRequest), fileName.c_str());

	chan->cread(&remainder , sizeof(remainder));

	int szBuffer = sizeof(buffer);
	while (remainder) {
		fileR->length = min(remainder, (int64)bufferCap);
		vector<char> data(buffer, buffer+szBuffer);
		requestBuff->push(data);

		remainder = remainder-fileR->length;
		remainder -= fileR->length;
		fileR->offset = fileR->offset+fileR->length;
	}
}

void worker_thread_function(BoundedBuffer* requestBuff, BoundedBuffer* respondBuff, FIFORequestChannel* chan, size_t bufferCap){
	char response[bufferCap];
	while (true) {
		vector<char> req = requestBuff->pop();
		char* request = (char*) req.data();

		chan->cwrite(request, req.size());
		Request* m = (Request*)request;
		
		if (m->getType() == DATA_REQ_TYPE) {
			/*
			DataRequest* dataR = (DataRequest*) m;
			
			chan->cwrite(dataR, sizeof(DataRequest));
			int p = ((DataRequest*) request)->person;
			double ecgVal = 0;
			chan->cread(&ecgVal, sizeof(double));
			
			getResponse r {p, ecgVal};
			vector<char> data ((char*)&r, ((char*)&r) + sizeof(r));
			respondBuff->push(data);
			*/
			DataRequest* dataR = (DataRequest*) m;
			chan->cwrite(dataR, sizeof(DataRequest));
			double ecg;
			chan->cread(&ecg, sizeof(double));
			getResponse r{dataR->person, ecg};
			vector<char> data ((char*)&r, ((char*)&r) + sizeof(r));
			respondBuff->push(data);
		} else if (m->getType() == FILE_REQ_TYPE) {
			chan->cread(response, sizeof(response));
			FileRequest* fr = (FileRequest*) request;
			string file_name = (char*) (fr+1);
			file_name = "received/" + file_name;
			FILE* fp = fopen(file_name.c_str(), "r+");
			fseek(fp, fr->offset, SEEK_SET);
			fwrite(response, 1, fr->length, fp);
			fclose(fp);
		} else if (m->getType() == QUIT_REQ_TYPE){
			delete chan;
			break;
		}
	}
}

void histogram_thread_function (HistogramCollection* hc, BoundedBuffer* response_buffer){
	while (true) {
		vector<char> response = response_buffer->pop();
		getResponse* r = (getResponse*)response.data();
		if (r->patient == -1) {
			break;
		}
		hc->update (r->patient, r->ecgValue);
	}
}

int main(int argc, char *argv[]){
	int opt;
	int n = 100; // number of requests per patient
	int p = 1; // number of patients from 1->15
	int w = 10; // default number of worker threads
	int b = 1024; // capacity of request buffer
	int m = MAX_MESSAGE; // capacity of message buffer
	int h = 15; // number of histogram threads
	string filename = "";
	bool reqFile = false;

	// take all the arguments first because some of these may go to the server
	// n-> # of req per patient, p-> # of patients, w-> # worker threads, b-> request buffer cap
	// m-> receive message buffer cap, f-> file
	while ((opt = getopt(argc, argv, "n:p:w:b:m:f:h:")) != -1) {
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
			case 'h':
				h = atoi(optarg);
				break;
		}
	}

	int pid = fork ();
	if (pid < 0){
		EXITONERROR ("Could not create a child process for running the server");
	}
	if (!pid){ // The server runs in the child process
		char serverName[] = "./server";
		char* args[] = {"./server", "-m",(char*)to_string(m).c_str(), nullptr};
		
		if (execvp(args[0], args) < 0){
			EXITONERROR ("Could not launch the server");
		}
	}

	FIFORequestChannel chan ("control", FIFORequestChannel::CLIENT_SIDE);
	BoundedBuffer request_buffer(b);
	BoundedBuffer response_buffer(b);
	HistogramCollection hc;

	struct timeval start, end;
    gettimeofday (&start, 0);

	
	for (int i = 0; i < p; i++) { // initialize histogram collection
		Histogram* h = new Histogram(10, -2.0, 2.0);
		hc.add(h);
	}
	
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
		workers[i] = thread(worker_thread_function, &request_buffer, &response_buffer, workerChans[i], m);
	}
	
	thread histograms[h]; // histogram threads
	for (size_t i = 0; i < h; i++) {
		histograms[i] = thread(histogram_thread_function, &hc, &response_buffer);
	}
	
	if (reqFile) { // file threads if necessary
		std::cout << "File Request\n" << endl;
		thread fileThread(file_thread, filename, &request_buffer, &chan, m);
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
	for (size_t i = 0; i < h; i++) {
		getResponse rtp{-1,0};
		vector<char> quitmsg((char*)&rtp, ((char*)&rtp) + sizeof(rtp));
		response_buffer.push(quitmsg);
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