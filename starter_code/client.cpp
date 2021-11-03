#include "common.h"
#include "FIFOreqchannel.h"
#include "BoundedBuffer.h"
#include "HistogramCollection.h"
#include <sys/wait.h>

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
void file_thread_function(string filename, BoundedBuffer* request_buffer, FIFORequestChannel* chan, size_t capacity) {
	// 1 thread if file transfer is requested
	// for the 1 thread
		// create a file request (instance of FileRequest with filename included)
		// Push packet to request buffer
		// Go back to a for all windows of the file
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
	// FileRequest* f = (FileRequest*) buffer;
}

// Parameter: Request Buffer reference, Histogram Buffer reference
void worker_thread_function(BoundedBuffer* request_buffer, HistogramCollection* hc, FIFORequestChannel* chan) {
	char buffer[1024];
	char recevBuffer[MAX_MESSAGE]; // change MAX_MESSAGE to int val from user input
	double ecgVal = 0.0;

	while(true) {
		request_buffer->pop(buffer, 1024);

		// if patient packet, push response to histogram bufer
		if ((*(REQUEST_TYPE_PREFIX*)buffer) == DATA_REQ_TYPE) {
			chan->cwrite(buffer, sizeof(DataRequest));
			chan->cread(&ecgVal, sizeof(double));
			hc->update(((DataRequest*)buffer)->person, ecgVal);
		}
		
		// if file transfer, write to file
		else if ((*(REQUEST_TYPE_PREFIX*)buffer) == FILE_REQ_TYPE) {
			FileRequest* fr = (FileRequest*)buffer;
			string filename = (char*)(fr + 1);
			chan->cwrite(buffer, sizeof(FileRequest) + filename.size() + 1);
			chan->cread(recevBuffer, sizeof(MAX_MESSAGE));

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
void histogram_thread_function (/*add necessary arguments*/){
	// as many threads as the -h flag
	// for each thread
		// read from histogram buffer
		// use the histogram update() function with the value popped from the histogram buffer
		// go back to a (i assume read from histogram buffer)
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
	int b = 10; // size of bounded buffer, note: this is different from another variable buffercapacity/m
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
	HistogramCollection hc;
	// in main
	// 1. Create a Histogram Collection
	// 2. create p histograms
	// 2. Call the HistogramCollection's add function for references of said p histograms
	// 4. Use HistogramCollection for all use

	struct timeval start, end;
    gettimeofday (&start, 0);

	// 1. Create all your threads
	// 2. Join patient and file threads
	// 3. push w quit messages to req buffer
	// 4. join worker threads
	// 5. join histogram threads

    /* Start all threads here */
	// create thread
	// - thread<thread_name> (function_name, function_parameter_1, function_parameter_2, etc..)
	// joining thread
	// - <thread_name>.join();
	// - waits for thread to finish before cont. code
	/* Join all threads here */

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
